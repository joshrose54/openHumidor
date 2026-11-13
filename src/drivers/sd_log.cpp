#include "sd_log.h"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <string.h>
#include <time.h>
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "pins.h"
#include "board.h"
#include "sd_card.h"
#include "sd_power.h"
#include "heap.h"
#include "wineador/logbook.h"
#include "wineador/event_text.h"
#include "wineador/line_ring.h"
#include "wineador/sd_power_seq.h"

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

// A record of every write attempt that failed, for the loop to log (B4 SD investigation).
constexpr int kFails = 16;
FailRecord gFails[kFails];             // guarded by gMux
int gFailHead = 0, gFailCount = 0;
uint32_t gFailSeq = 0, gFailsLost = 0;
volatile bool gWifiUp = false;
volatile bool gPaused = false;         // Rev N: no card operation while the radio is on (5.4)

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

// What one attempt did: the step it got to (the one that failed, if it failed),
// whether the file got longer before it failed (a partial write), and where in the
// file the data went.
struct Attempt {
  const char* stage = "";
  bool grew = false;
  size_t before = 0;                   // the file's size when opened (the write's offset)
  size_t len = 0;                      // bytes of data
  size_t headerLen = 0;                // header bytes written first (a new file)
};

// One attempt at a write: folders, the right part file, the header into a new file,
// then the data, then the check that it all reached the card.
bool appendOnce(const char* path, const char* header, const Payload& data, bool newlineFirst, Attempt& a) {
  a = Attempt{};
  a.stage = "folder";
  if (!makeFolders(path)) return false;
  // FAT32 holds no file of 4 GiB or more: if this write would pass the limit, move
  // on to the next part (-p2, -p3 ...). The part in use is remembered per file.
  a.stage = "open";
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
  a.before = before;
  a.len = len;
  bool ok = true;
  size_t extra = 0;                    // header or newline written before the data
  if (f.size() == 0 && header) {
    a.stage = "header";
    a.headerLen = strlen(header);
    extra = a.headerLen;
    ok = f.write((const uint8_t*)header, strlen(header)) == strlen(header);
  } else if (newlineFirst) {
    // A retry after a partial write: start a fresh line, so the fragment stays one
    // visibly broken row instead of running into the next.
    a.stage = "data";
    extra = 1;
    ok = f.write((const uint8_t*)"\n", 1) == 1;
  }
  if (ok) {
    a.stage = "data";
    ok = data.writeTo(f);
  }
  // Every write verified (5.4). The last part of a write sits in the file's buffer
  // until it is flushed, and an error then is not reported (B4 SD investigation: 5 in
  // 6,000 writes lost that way). So flush, then check the file has grown by exactly
  // what was written, before closing it. A short (or long) file fails the attempt
  // like any other error, and the caller retries.
  f.flush();
  const size_t got = f.size();         // after a write this reads the size back from the card
  f.close();
  a.grew = got > before;
  if (ok) {
    a.stage = "verify";
    ok = got == before + extra + len;
  }
  if (ok) a.stage = "data";            // worked: report the last real step
  return ok;
}

// Keep a record of one failed attempt (B4 SD investigation), for the loop to log.
void record(bool stress, uint32_t writeNo, bool retried, bool recovered, const Attempt& a, uint32_t attemptMs) {
  FailRecord r;
  r.stress = stress;
  r.writeNo = writeNo;
  r.retried = retried;
  r.recovered = recovered;
  strncpy(r.stage, a.stage, sizeof(r.stage) - 1);
  r.attemptMs = attemptMs;
  r.fileOffset = (uint32_t)a.before;
  r.len = (uint32_t)a.len;
  r.heapFree = heap::freeData();      // internal RAM only (5.2): on Rev Q the plain call adds PSRAM
  r.heapLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  r.wifiUp = gWifiUp;
  withStats([&](Stats&) {
    r.seq = ++gFailSeq;
    if (gFailCount == kFails) {        // full: the oldest goes
      gFailHead = (gFailHead + 1) % kFails;
      gFailCount--;
      gFailsLost++;
    }
    gFails[(gFailHead + gFailCount) % kFails] = r;
    gFailCount++;
  });
}

// The one place the card is written. A failed write is retried ONCE after a remount
// (a single transfer error, B4 night 1, should lose nothing); only a second failure
// marks the card failed. A recovered retry is counted and logged.
void noteEvent(const char* type, const char* detail);   // below: an event pushed from inside a write

bool appendFile(const char* path, const char* header, const Payload& data) {
  gBusySince = millis();
  gBusy = true;
  const uint32_t t0 = millis();
  Attempt a;
  bool ok = appendOnce(path, header, data, false, a);
  const uint32_t firstMs = millis() - t0;
  if (!ok) {
    const Attempt first = a;
    bool retried = gRetry && mountCard();
    // Rev Q (3A.11): when the retry's remount fails, power-cycle the card once (off at least
    // 100 ms, on, remount) before giving up. Only this task touches the card, so it can wait.
    int cycled = 0;                    // 1 remounted after a power cycle, -1 the remount failed even so
    if (gRetry && !retried && sdpower::fitted()) {
      retried = sdpower::cycle() && mountCard();
      withStats([&](Stats& s) { s.powerCycles++; });
      cycled = retried ? 1 : -1;
    }
    if (retried) ok = appendOnce(path, header, data, first.grew, a);
    if (cycled == 1 && ok) withStats([&](Stats& s) { s.powerCycleSaves++; });
    if (cycled != 0) {
      // Logged NOW, before setError() below publishes the failure: so the events file reads
      // "power-cycled", then "SD FLUSH FAILURE" (BP2 test 38), in the order they happened.
      char d[200];
      snprintf(d, sizeof(d), "%s (off %lu ms, on, remount) - %s "
               "(%lu power cycles since boot)", wineador::kEvSdPowerCycled, (unsigned long)wineador::SdPowerSeq::kCycleOffMs,
               cycled == -1 ? "remount FAILED" : ok ? "remount OK, write retried OK" : "remount OK but the retried write failed",
               (unsigned long)stats().powerCycles);
      noteEvent("sd", d);
    }
    record(false, 0, retried, ok, first, firstMs);
    if (ok) {
      withStats([&](Stats& s) {
        s.retries++;
        snprintf(s.lastRetry, sizeof(s.lastRetry), "%s at %s%s", path, first.stage, first.grew ? " (partial)" : "");
      });
      gRetryNote = true;
    } else {
      char what[48];
      snprintf(what, sizeof(what), "write failed (%s%s):", retried ? a.stage : first.stage, retried ? ", retried" : "");
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
// An event from inside a write (no flush: we may be in the middle of one). Its own buffer, so
// it never disturbs gScratch. Pushed before the failure is published, so it is logged first.
void noteEvent(const char* type, const char* detail) {
  static char line[320];
  char ts[24];
  const time_t t = time(nullptr);
  struct tm tmv;
  gmtime_r(&t, &tmv);
  strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tmv);
  wineador::formatEventRow(line, sizeof(line), ts, true, (uint64_t)(esp_timer_get_time() / 1000), type, detail);
  gEvents.push(line, strlen(line));
}

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
          snprintf(d, sizeof(d), "%s %s (%llu MB)", wineador::kEvSdRetention, paths[i],
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
  snprintf(d, sizeof(d), "%s: %u events, %u readings rows, %u hourly rows "
                         "(dropped while out, since boot: %lu events, %lu rows, %lu hourly)",
           wineador::kEvSdCardBack, (unsigned)gEvents.count(), (unsigned)gRows.count(), (unsigned)gHourly.count(), (unsigned long)st.eventsDropped,
           (unsigned long)st.rowsDropped, (unsigned long)st.hourlyDropped);
  // Kept events first, then this note (so it follows them in time), then the rest.
  flushEvents();
  selfEvent("sd", d);
  flushHourly();
  flushReadings(true);
}

// ---------------------------------------------------------------- stress test
// Test builds: 16-row batches of a realistic test-build readings row, back to back,
// into /stress/. Counts writes, failures, retries and the step that failed, keeps a
// record of every failed attempt, and the lowest free heap.
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
  // One realistic test-build row, 16 times = one batch (~5.4 KB), from the heap only
  // while the test runs.
  constexpr size_t kBatch = 16 * 470;
  char* batch = (char*)malloc(kBatch);
  size_t len = 0;
  if (batch) {
    for (int r = 0; r < 16; r++) {
      int k = snprintf(batch + len, kBatch - len, "2026-11-09T12:00:%02dZ,1", r);
      len += k;
      for (int c = 0; c < 12; c++) { k = snprintf(batch + len, kBatch - len, ",20.%02d,62.%02d,64.50,ok", c, r); len += k; }
      k = snprintf(batch + len, kBatch - len, ",20.12,19.98,20.30,62.10,20.12,40,912,1,0,0,cool,3.5,97800,64\n");
      len += k;
    }
  }
  const bool mounted = batch && mountCard();
  const char* kHead = "# wineador SD stress test\n";
  uint32_t ok = 0, fail = 0, retried = 0, worst = 0;
  size_t expected = 0;                 // the size the file should have at the next write
  bool expectedKnown = false;
  while (mounted && ok + fail < gStressTarget && !gStressStop) {
    if (gPaused) {                     // as in normal running: no card write while the radio is on
      withStats([](Stats& s) { s.stress.pausedMs += 50; });
      vTaskDelay(pdMS_TO_TICKS(50));
      Msg m;
      if (xQueueReceive(gQ, &m, 0) == pdTRUE) {
        if (m.kind == Kind::Reading) gRows.push(m.text, strlen(m.text));
        else if (m.kind == Kind::Event) gEvents.push(m.text, strlen(m.text));
        else gHourly.push(m.text, strlen(m.text));
      }
      continue;
    }
    Payload p;
    p.text = batch;
    p.len = len;
    const uint32_t t0 = millis();
    gBusySince = t0;
    gBusy = true;
    Attempt a;
    bool good = appendOnce(path, kHead, p, false, a);
    const uint32_t firstMs = millis() - t0;
    const Attempt first = a;
    // An independent check of the verify step: the file must be the size the last
    // verified write left it.
    const bool mismatch = expectedKnown && first.before != expected;
    bool didRetry = false;
    if (!good) {
      // with the retry: remount and try once more; without: count it, remount, carry on
      const bool remounted = mountCard();
      if (gRetry && remounted) {
        didRetry = true;
        good = appendOnce(path, kHead, p, first.grew, a);
        if (good) retried++;
      }
    }
    gBusy = false;
    const uint32_t ms = millis() - t0;
    if (ms > worst) worst = ms;
    if (good) ok++;
    else fail++;
    const bool firstFailed = !good || didRetry;
    if (firstFailed) record(true, ok + fail, didRetry, good, first, firstMs);
    if (good) {
      expected = a.before + a.headerLen + (didRetry && first.grew ? 1 : 0) + len;
      expectedKnown = true;
    } else {
      expectedKnown = false;           // after a lost write the size is not known; start again
    }
    const uint32_t heapNow = heap::freeData();      // internal RAM only (5.2)
    const uint32_t largestNow = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    withStats([&](Stats& s) {
      StressStats& x = s.stress;
      x.writes = ok + fail;
      x.failures = fail;
      x.retries = retried;
      x.worstMs = worst;
      if (firstFailed) {
        if (strcmp(first.stage, "folder") == 0) x.atFolder++;
        else if (strcmp(first.stage, "open") == 0) x.atOpen++;
        else if (strcmp(first.stage, "header") == 0) x.atHeader++;
        else if (strcmp(first.stage, "verify") == 0) x.atVerify++;
        else x.atData++;
      }
      if (mismatch) x.sizeMismatch++;
      if (x.heapMin == 0 || heapNow < x.heapMin) { x.heapMin = heapNow; x.heapMinLargest = largestNow; }
      x.heapMinEver = heap::lowest();     // internal RAM only (5.2)
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
    if (gPaused) {                     // the radio is on: everything waits in the buffers
      publishBuffers();
      continue;
    }
    if (!cardOk() && millis() - gLastRemount >= kRemountMs) tryRemount();
    if (cardOk() && !gRetentionDone) retention();
    if (cardOk() && millis() - gLastSpace >= kSpaceRefreshMs) refreshSpace();
    if (gRetryNote) {
      gRetryNote = false;
      Stats st = stats();
      char d[160];
      snprintf(d, sizeof(d), "%s (%s; %lu retries since boot)", wineador::kEvSdRetriedOk, st.lastRetry,
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
  // Rev Q: in PSRAM (5.4); if no PSRAM was found, Rev N's sizes in internal RAM (and an event).
  const bool psram = board::kOutageInPsram && heap::psramTotal() > 0;
  auto ring = [psram](size_t bytes, uint16_t lines) {
    char* b = psram ? (char*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : (char*)malloc(bytes);
    return new LineRing(b, b ? bytes : 0, b ? lines : 0);
  };
  if (psram || !board::kOutageInPsram) {
    gEvP = ring(board::kSdEventsBytes, board::kSdEventsKept);
    gRowP = ring(board::kSdRowsBytes, board::kSdRowsKept);
    gHrP = ring(board::kSdHourlyBytes, board::kSdHourlyKept);
  } else {
    gEvP = ring(board_revn::kSdEventsBytes, board_revn::kSdEventsKept);
    gRowP = ring(board_revn::kSdRowsBytes, board_revn::kSdRowsKept);
    gHrP = ring(board_revn::kSdHourlyBytes, board_revn::kSdHourlyKept);
  }
  withStats([&](Stats& s) {
    s.buffersInPsram = psram;
    s.psramMissing = board::kOutageInPsram && !psram;
  });
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

bool takeFail(FailRecord* out) {
  bool got = false;
  withStats([&](Stats&) {
    if (gFailCount == 0) return;
    *out = gFails[gFailHead];
    gFailHead = (gFailHead + 1) % kFails;
    gFailCount--;
    got = true;
  });
  return got;
}

uint32_t failsLost() {
  uint32_t n;
  withStats([&](Stats&) { n = gFailsLost; });
  return n;
}

void setWifiUp(bool up) { gWifiUp = up; }

void setPaused(bool paused) {
  gPaused = paused;
  withStats([&](Stats& s) { s.paused = paused; });
}

}  // namespace sdlog
