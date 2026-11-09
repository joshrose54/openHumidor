#include "sd_log.h"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <string.h>
#include <time.h>
#include "esp_timer.h"
#include "pins.h"
#include "board.h"
#include "sd_card.h"
#include "wineador/logbook.h"
#include "wineador/line_ring.h"

namespace sdlog {

using wineador::LogKind;
using wineador::LineRing;

namespace {

enum class Kind : uint8_t { Reading, Event, Hourly };
constexpr size_t kLine = 600;          // longest line (a test-build readings row is up to ~460)
constexpr int kQueueLen = 16;
constexpr uint32_t kRemountMs = 30000;
constexpr uint32_t kSpaceRefreshMs = 3600000UL;

struct Msg {
  Kind kind;
  char text[kLine];
};

QueueHandle_t gQ = nullptr;
portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
Stats gStats;                          // guarded by gMux
volatile bool gStarted = false;
volatile bool gBusy = false;
volatile uint32_t gBusySince = 0;
volatile uint32_t gWorstSinceTake = 0;
bool gRetryNote = false;               // a write was recovered by a retry: log it (from the task loop)
int gYear = 0, gMonth = 0;

// The card's settings, read at every mount: normally 1 MHz with the retry on. Only
// the test-build stress test changes them, and puts them back when it ends.
volatile uint32_t gSpiHz = sdcard::kSpiHz;
volatile bool gRetry = true;

char* gHeadR = nullptr;
char* gHeadE = nullptr;
char* gHeadH = nullptr;

// The outage buffers (5.4), sized per board (board.h), taken from the heap once at
// begin() (the ESP32's static memory region is too small for them). Task-owned.
LineRing* gEvP = nullptr;
LineRing* gRowP = nullptr;
LineRing* gHrP = nullptr;
#define gEvents (*gEvP)
#define gRows (*gRowP)
#define gHourly (*gHrP)
uint32_t gQueueDropsRows = 0, gQueueDropsEvents = 0;   // lines the queue had no room for
char gScratch[kLine];                  // one line at a time, out of a buffer and onto the card
uint32_t gLastRemount = 0, gLastSpace = 0;
bool gRetentionDone = false;

// Stress test (test builds): set by the loop, run by the task.
volatile bool gStressRequest = false, gStressStop = false;
uint32_t gStressKhz = 0, gStressTarget = 0;
bool gStressRetry = true;

char* dupe(const char* s) {
  const size_t n = strlen(s) + 1;
  char* p = (char*)malloc(n);
  if (p) memcpy(p, s, n);
  return p;
}

template <typename F> void withStats(F f) {
  portENTER_CRITICAL(&gMux);
  f(gStats);
  portEXIT_CRITICAL(&gMux);
}

void setError(const char* what, const char* path) {
  withStats([&](Stats& s) {
    snprintf(s.lastError, sizeof(s.lastError), "%s %s", what, path);
    if (!s.failed) s.failures++;
    s.failed = true;
    s.mounted = false;
  });
}

void publishBuffers() {
  withStats([](Stats& s) {
    s.rowsBuffered = gRows.count();
    s.rowsDropped = gRows.dropped() + gQueueDropsRows;
    s.eventsPending = gEvents.count();
    s.eventsDropped = gEvents.dropped() + gQueueDropsEvents;
    s.hourlyPending = gHourly.count();
    s.hourlyDropped = gHourly.dropped();
    if (gRows.count() > s.rowsBufferedMax) s.rowsBufferedMax = gRows.count();
    if (gEvents.count() > s.eventsPendingMax) s.eventsPendingMax = gEvents.count();
    if (gHourly.count() > s.hourlyPendingMax) s.hourlyPendingMax = gHourly.count();
  });
}

// "2026-11-08T..." -> year, month. Anything else: the clock's month.
void monthOf(const char* line, int* y, int* m) {
  if (strlen(line) >= 7 && line[4] == '-' && isdigit((unsigned char)line[0]) && isdigit((unsigned char)line[5])) {
    *y = atoi(line);
    *m = (line[5] - '0') * 10 + (line[6] - '0');
    if (*m >= 1 && *m <= 12) return;
  }
  *y = gYear;
  *m = gMonth;
}

// The part number in use for each file (part 1 = the file itself), for the files
// written at any one time; a new month's file starts at part 1 again.
int* partFor(const char* path) {
  struct Slot { char path[40]; int part; };
  static Slot slots[5] = {};
  static int next = 0;
  for (Slot& sl : slots)
    if (strcmp(sl.path, path) == 0) return &sl.part;
  Slot& sl = slots[next];
  next = (next + 1) % 5;
  strncpy(sl.path, path, sizeof(sl.path) - 1);
  sl.path[sizeof(sl.path) - 1] = 0;
  sl.part = 1;
  return &sl.part;
}

bool mountCard() {
  SD.end();
  return SD.begin(pins::kSdCs, sdcard::spi(), gSpiHz);
}

// Makes every folder on the way to `path` ("/logs", "/logs/2026", "/stress").
bool makeFolders(const char* path) {
  char dir[40];
  for (const char* p = strchr(path + 1, '/'); p; p = strchr(p + 1, '/')) {
    const size_t dl = (size_t)(p - path);
    if (dl >= sizeof(dir)) return false;
    memcpy(dir, path, dl);
    dir[dl] = 0;
    if (!SD.exists(dir) && !SD.mkdir(dir)) return false;
  }
  return true;
}

// The data of one write: lines [0, n) of a buffer, or a single block of text.
struct Payload {
  const LineRing* ring = nullptr;
  uint16_t n = 0;
  const char* text = nullptr;
  size_t len = 0;
  size_t bytes() const {
    if (!ring) return len;
    size_t b = 0;
    for (uint16_t i = 0; i < n; i++) b += ring->peek(i, gScratch, sizeof(gScratch));
    return b;
  }
  bool writeTo(File& f) const {
    if (!ring) return f.write((const uint8_t*)text, len) == len;
    for (uint16_t i = 0; i < n; i++) {
      const size_t l = ring->peek(i, gScratch, sizeof(gScratch));
      if (f.write((const uint8_t*)gScratch, l) != l) return false;
    }
    return true;
  }
};

// One attempt at a write: folders, the right part file, the header into a new file,
// then the data. `stage` gets the step that failed; `grew` whether the file got
// longer (a partial write) before it failed.
bool appendOnce(const char* path, const char* header, const Payload& data, bool newlineFirst, const char** stage,
                bool* grew) {
  *grew = false;
  *stage = "folder";
  if (!makeFolders(path)) return false;
  // FAT32 holds no file of 4 GiB or more: if this write would pass the limit, move
  // on to the next part (-p2, -p3 ...). The part in use is remembered per file.
  *stage = "open";
  File f;
  char partPath[48];
  int* part = partFor(path);
  const size_t len = data.bytes();
  for (;;) {
    wineador::logPartPath(partPath, sizeof(partPath), path, *part);
    f = SD.open(partPath, FILE_APPEND);
    if (!f) return false;
    const uint64_t add = len + 1 + (f.size() == 0 && header ? strlen(header) : 0);
    if (!wineador::logFileFull(f.size(), add)) break;
    f.close();
    if (++*part > 99) return false;
  }
  const size_t before = f.size();
  bool ok = true;
  if (f.size() == 0 && header) {
    *stage = "header";
    ok = f.write((const uint8_t*)header, strlen(header)) == strlen(header);
  } else if (newlineFirst) {
    // A retry after a partial write: start a fresh line, so the fragment stays one
    // visibly broken row instead of running into the next.
    *stage = "data";
    ok = f.write((const uint8_t*)"\n", 1) == 1;
  }
  if (ok) {
    *stage = "data";
    ok = data.writeTo(f);
  }
  *grew = f.size() > before;
  f.close();
  return ok;
}

// The one place the card is written. A failed write is retried ONCE after a remount
// (a single transfer error, B4 night 1, should lose nothing); only a second failure
// marks the card failed. A recovered retry is counted and logged.
bool appendFile(const char* path, const char* header, const Payload& data) {
  gBusySince = millis();
  gBusy = true;
  const uint32_t t0 = millis();
  const char* stage = "";
  bool grew = false;
  bool ok = appendOnce(path, header, data, false, &stage, &grew);
  if (!ok) {
    const char* firstStage = stage;
    const bool firstGrew = grew;
    if (gRetry && mountCard()) ok = appendOnce(path, header, data, firstGrew, &stage, &grew);
    if (ok) {
      withStats([&](Stats& s) {
        s.retries++;
        snprintf(s.lastRetry, sizeof(s.lastRetry), "%s at %s%s", path, firstStage, firstGrew ? " (partial)" : "");
      });
      gRetryNote = true;
    } else {
      char what[48];
      snprintf(what, sizeof(what), "write failed (%s%s):", gRetry ? stage : firstStage, gRetry ? ", retried" : "");
      setError(what, path);
    }
  }
  const uint32_t ms = millis() - t0;
  gBusy = false;
  if (ms > gWorstSinceTake) gWorstSinceTake = ms;
  withStats([&](Stats& s) {
    s.lastWriteMs = ms;
    if (ms > s.worstWriteMs) s.worstWriteMs = ms;
  });
  return ok;
}

bool cardOk() {
  bool ok;
  portENTER_CRITICAL(&gMux);
  ok = gStats.mounted && !gStats.failed;
  portEXIT_CRITICAL(&gMux);
  return ok;
}

// How many of the oldest lines (from 0) go to the same file as line 0.
uint16_t runLength(const LineRing& ring, LogKind kind, char* path, size_t pathLen) {
  int y, m;
  ring.peek(0, gScratch, sizeof(gScratch));
  monthOf(gScratch, &y, &m);
  wineador::logPath(path, pathLen, kind, y, m);
  uint16_t k = 1;
  for (; k < ring.count(); k++) {
    int y2, m2;
    ring.peek(k, gScratch, sizeof(gScratch));
    monthOf(gScratch, &y2, &m2);
    if (y2 != y || m2 != m) break;
  }
  return k;
}

// Events: every one, oldest first, as soon as the card can take them.
void flushEvents() {
  while (!gEvents.empty() && cardOk()) {
    char path[40];
    Payload p;
    p.ring = &gEvents;
    p.n = runLength(gEvents, LogKind::Events, path, sizeof(path));
    if (!appendFile(path, gHeadE, p)) break;
    const uint16_t n = p.n;
    gEvents.pop(n);
    withStats([&](Stats& s) { s.eventsWritten += n; });
  }
}

// Hourly rows: one file, as soon as the card can take them.
void flushHourly() {
  while (!gHourly.empty() && cardOk()) {
    char path[40];
    wineador::logPath(path, sizeof(path), LogKind::Hourly, 0, 0);
    Payload p;
    p.ring = &gHourly;
    p.n = gHourly.count();
    if (!appendFile(path, gHeadH, p)) break;
    const uint16_t n = p.n;
    gHourly.pop(n);
    withStats([&](Stats& s) { s.hourlyWritten += n; });
  }
}

// Readings: in batches of 16 (wear, 5.4); after an outage (`all`), everything kept.
void flushReadings(bool all) {
  while (!gRows.empty() && cardOk()) {
    char path[40];
    const uint16_t run = runLength(gRows, LogKind::Readings, path, sizeof(path));
    // Wait for a full batch, unless told to write everything or the month changed.
    if (!all && run == gRows.count() && run < wineador::kReadingsBatch) break;
    Payload p;
    p.ring = &gRows;
    p.n = run;
    withStats([&](Stats& s) { strncpy(s.readingsPath, path, sizeof(s.readingsPath) - 1); });
    if (!appendFile(path, gHeadR, p)) break;
    gRows.pop(run);
    withStats([&](Stats& s) {
      s.rowsWritten += run;
      s.flushes++;
      s.lastFlushRows = run;
      s.lastFlushUptimeMs = (uint64_t)(esp_timer_get_time() / 1000);
    });
  }
}

void refreshSpace() {
  gBusySince = millis();
  gBusy = true;
  const uint64_t total = SD.totalBytes(), used = SD.usedBytes();
  gBusy = false;
  withStats([&](Stats& s) { s.totalBytes = total; s.freeBytes = total > used ? total - used : 0; });
  gLastSpace = millis();
}

// An event the task writes itself (retention, recovery, retries): same format as
// the loop's, into the same buffer, so it lands in order.
void selfEvent(const char* type, const char* detail) {
  char ts[24];
  const time_t t = time(nullptr);
  struct tm tmv;
  gmtime_r(&t, &tmv);
  strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tmv);
  wineador::formatEventRow(gScratch, sizeof(gScratch), ts, true, (uint64_t)(esp_timer_get_time() / 1000), type, detail);
  gEvents.push(gScratch, strlen(gScratch));
  flushEvents();
}

void retention() {
  gRetentionDone = true;
  refreshSpace();
  Stats s = stats();
  static wineador::LogFileInfo files[48];   // static: off the task's stack
  static char paths[48][40];
  int n = 0;
  File logs = SD.open("/logs");
  if (logs && logs.isDirectory()) {
    for (File yd = logs.openNextFile(); yd && n < 48; yd = logs.openNextFile()) {
      if (yd.isDirectory()) {
        for (File f = yd.openNextFile(); f && n < 48; f = yd.openNextFile()) {
          int y, m;
          if (!f.isDirectory() && wineador::parseReadingsName(f.name(), &y, &m)) {
            files[n].year = y;
            files[n].month = m;
            files[n].bytes = f.size();
            snprintf(paths[n], sizeof(paths[n]), "%s/%s", yd.path(), f.name());
            n++;
          }
          f.close();
        }
      }
      yd.close();
    }
  }
  if (logs) logs.close();
  static wineador::LogFileInfo sorted[48];
  for (int i = 0; i < n; i++) sorted[i] = files[i];
  const int del = wineador::retentionPlan(sorted, n, s.totalBytes, s.freeBytes, gYear, gMonth);
  int deleted = 0;
  for (int k = 0; k < del; k++) {
    for (int i = 0; i < n; i++) {
      if (files[i].year == sorted[k].year && files[i].month == sorted[k].month && files[i].bytes == sorted[k].bytes) {
        char d[160];
        if (SD.remove(paths[i])) {
          deleted++;
          snprintf(d, sizeof(d), "retention: free space under 10%%, deleted %s (%llu MB)", paths[i],
                   (unsigned long long)(files[i].bytes / (1024ULL * 1024ULL)));
        } else {
          snprintf(d, sizeof(d), "retention: could not delete %s", paths[i]);
        }
        selfEvent("sd", d);
        files[i].year = 0;   // done
        break;
      }
    }
  }
  if (del > 0) refreshSpace();
  withStats([&](Stats& st) { st.retentionDeleted = deleted; });
}

void tryRemount() {
  gLastRemount = millis();
  gBusySince = millis();
  gBusy = true;
  const bool ok = mountCard();
  gBusy = false;
  if (!ok) return;
  withStats([](Stats& s) { s.mounted = true; s.failed = false; });
  refreshSpace();
  publishBuffers();
  Stats st = stats();
  char d[200];
  snprintf(d, sizeof(d), "SD card back: remounted; writing what was kept: %u events, %u readings rows, %u hourly rows "
                         "(dropped while out, since boot: %lu events, %lu rows, %lu hourly)",
           (unsigned)gEvents.count(), (unsigned)gRows.count(), (unsigned)gHourly.count(), (unsigned long)st.eventsDropped,
           (unsigned long)st.rowsDropped, (unsigned long)st.hourlyDropped);
  // Kept events first, then this note (so it follows them in time), then the rest.
  flushEvents();
  selfEvent("sd", d);
  flushHourly();
  flushReadings(true);
}

// ---------------------------------------------------------------- stress test
bool retriedNow(uint32_t retried, uint32_t prev) { return retried != prev; }

// Test builds: 16-row batches of a realistic test-build readings row, back to back,
// into /stress/. Counts writes, failures, retries and the step that failed.
void stressRun() {
  gStressRequest = false;
  gStressStop = false;
  const uint32_t normalHz = gSpiHz;
  const bool normalRetry = gRetry;
  gSpiHz = gStressKhz * 1000UL;
  gRetry = gStressRetry;
  withStats([&](Stats& s) {
    s.stress = StressStats{};
    s.stress.active = true;
    s.stress.khz = gStressKhz;
    s.stress.retry = gStressRetry;
    s.stress.target = gStressTarget;
  });
  char path[40];
  snprintf(path, sizeof(path), "/stress/stress-%luk-%s.csv", (unsigned long)gStressKhz, gStressRetry ? "retry" : "noretry");
  // One realistic test-build row (~460 characters), 16 times = one batch (~7.4 KB),
  // from the heap only while the test runs.
  constexpr size_t kBatch = 16 * 470;
  char* batch = (char*)malloc(kBatch);
  if (!batch) {
    gSpiHz = normalHz;
    gRetry = normalRetry;
    withStats([](Stats& s) { s.stress.active = false; s.stress.done = true; });
    return;
  }
  size_t len = 0;
  for (int r = 0; r < 16; r++) {
    int k = snprintf(batch + len, kBatch - len, "2026-11-09T12:00:%02dZ,1", r);
    len += k;
    for (int c = 0; c < 12; c++) { k = snprintf(batch + len, kBatch - len, ",20.%02d,62.%02d,64.50,ok", c, r); len += k; }
    k = snprintf(batch + len, kBatch - len, ",20.12,19.98,20.30,62.10,20.12,40,912,1,0,0,cool,3.5,97800,64\n");
    len += k;
  }
  const bool mounted = mountCard();
  uint32_t ok = 0, fail = 0, retried = 0, worst = 0, prevRetried = 0;
  while (mounted && ok + fail < gStressTarget && !gStressStop) {
    Payload p;
    p.text = batch;
    p.len = len;
    const uint32_t t0 = millis();
    gBusySince = t0;
    gBusy = true;
    const char* stage = "";
    bool grew = false;
    bool good = appendOnce(path, "# wineador SD stress test\n", p, false, &stage, &grew);
    const char* firstStage = stage;
    if (!good) {
      // with the retry: remount and try once more; without: count it, remount, carry on
      const bool remounted = mountCard();
      if (gRetry && remounted) {
        good = appendOnce(path, "# wineador SD stress test\n", p, grew, &stage, &grew);
        if (good) retried++;
      }
    }
    gBusy = false;
    const uint32_t ms = millis() - t0;
    if (ms > worst) worst = ms;
    if (good) ok++;
    else fail++;
    // The step the FIRST attempt failed at, whenever it failed (recovered by the retry or not).
    const bool firstFailed = !good || retriedNow(retried, prevRetried);
    prevRetried = retried;
    withStats([&](Stats& s) {
      StressStats& x = s.stress;
      x.writes = ok + fail;
      x.failures = fail;
      x.retries = retried;
      x.worstMs = worst;
      if (firstFailed) {
        if (strcmp(firstStage, "folder") == 0) x.atFolder++;
        else if (strcmp(firstStage, "open") == 0) x.atOpen++;
        else if (strcmp(firstStage, "header") == 0) x.atHeader++;
        else x.atData++;
      }
    });
    vTaskDelay(1);                       // let the idle task run
    // pass a queued line through now and then, so logging carries on
    Msg m;
    if (xQueueReceive(gQ, &m, 0) == pdTRUE) {
      if (m.kind == Kind::Reading) gRows.push(m.text, strlen(m.text));
      else if (m.kind == Kind::Event) gEvents.push(m.text, strlen(m.text));
      else gHourly.push(m.text, strlen(m.text));
    }
  }
  free(batch);
  gSpiHz = normalHz;
  gRetry = normalRetry;
  const bool back = mountCard();
  withStats([&](Stats& s) {
    s.stress.active = false;
    s.stress.done = true;
    s.mounted = back;
    s.failed = !back;
  });
}

void task(void*) {
  for (;;) {
    if (gStressRequest) { stressRun(); publishBuffers(); }
    Msg m;
    const bool got = xQueueReceive(gQ, &m, pdMS_TO_TICKS(500)) == pdTRUE;
    if (!gStarted) {
      if (got) xQueueSendToFront(gQ, &m, 0);   // keep it until we may write
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if (got) {
      // Into its buffer first: written in order, or kept while the card is out
      // (the oldest dropped and counted when a buffer is full).
      LineRing& r = m.kind == Kind::Reading ? gRows : m.kind == Kind::Event ? gEvents : gHourly;
      r.push(m.text, strlen(m.text));
    }
    if (!cardOk() && millis() - gLastRemount >= kRemountMs) tryRemount();
    if (cardOk() && !gRetentionDone) retention();
    if (cardOk() && millis() - gLastSpace >= kSpaceRefreshMs) refreshSpace();
    if (gRetryNote) {
      gRetryNote = false;
      Stats st = stats();
      char d[160];
      snprintf(d, sizeof(d), "SD write error, retried after a remount: OK (%s; %lu retries since boot)", st.lastRetry,
               (unsigned long)st.retries);
      selfEvent("sd", d);
    }
    flushEvents();
    flushHourly();
    flushReadings(false);
    publishBuffers();
  }
}

void send(Kind k, const char* line) {
  if (!gQ) return;
  static Msg m;                    // only the loop task sends: one static buffer, no big stack copy
  m.kind = k;
  strncpy(m.text, line, kLine - 1);
  m.text[kLine - 1] = 0;
  if (xQueueSend(gQ, &m, 0) != pdTRUE) {
    if (k == Kind::Reading) gQueueDropsRows++;
    else gQueueDropsEvents++;
  }
}

}  // namespace

void begin(const char* readingsHeader, const char* eventsHeader, const char* hourlyHeader) {
  if (gQ) return;
  gHeadR = dupe(readingsHeader);
  gHeadE = dupe(eventsHeader);
  gHeadH = dupe(hourlyHeader);
  // The outage buffers, once, from the heap (sizes per board).
  // If the heap ever could not supply one, that buffer holds nothing (lines are dropped and counted).
  auto ring = [](size_t bytes, uint16_t lines) {
    char* b = (char*)malloc(bytes);
    return new LineRing(b, b ? bytes : 0, b ? lines : 0);
  };
  gEvP = ring(board::kSdEventsBytes, board::kSdEventsKept);
  gRowP = ring(board::kSdRowsBytes, board::kSdRowsKept);
  gHrP = ring(board::kSdHourlyBytes, board::kSdHourlyKept);
  const bool mounted = sdcard::mounted();     // mounted at boot by sdcard::begin
  withStats([&](Stats& s) {
    s.mounted = mounted;
    s.failed = !mounted;
    if (!mounted) snprintf(s.lastError, sizeof(s.lastError), "no card at boot");
    s.spiKhz = sdcard::kSpiHz / 1000;
  });
  gLastRemount = millis();
  gQ = xQueueCreate(kQueueLen, sizeof(Msg));
  // Core 0, priority 0 — the same as core 0's idle task, so the two share the core
  // tick by tick. The watchdog watches that idle task (watchdog.cpp): a card call
  // that takes seconds (a remount with no card, free space on a big card) must
  // never starve it into a reset. WiFi and the network stack run above both.
  xTaskCreatePinnedToCore(task, "sdlog", 8192, nullptr, 0, nullptr, 0);
}

void start(int year, int month) {
  gYear = year;
  gMonth = month;
  gStarted = true;
}

bool started() { return gStarted; }

void reading(const char* line) { send(Kind::Reading, line); }
void event(const char* line) { send(Kind::Event, line); }
void hourly(const char* line) { send(Kind::Hourly, line); }

Stats stats() {
  Stats s;
  portENTER_CRITICAL(&gMux);
  s = gStats;
  portEXIT_CRITICAL(&gMux);
  return s;
}

uint32_t takeWorstMs() {
  const uint32_t w = gWorstSinceTake;
  gWorstSinceTake = 0;
  return w;
}

bool writing() { return gBusy; }

uint32_t stalledMs(uint32_t nowMs) { return gBusy ? nowMs - gBusySince : 0; }

bool stressStart(uint32_t khz, bool retry, uint32_t writes) {
  if (gStressRequest || stats().stress.active) return false;
  gStressKhz = khz;
  gStressRetry = retry;
  gStressTarget = writes;
  gStressRequest = true;
  return true;
}

void stressStop() { gStressStop = true; }

}  // namespace sdlog
