#include "sd_log.h"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <string.h>
#include <time.h>
#include "esp_timer.h"
#include "pins.h"
#include "sd_card.h"
#include "wineador/logbook.h"

namespace sdlog {

using wineador::LogKind;

namespace {

enum class Kind : uint8_t { Reading, Event, Hourly };
constexpr size_t kLine = 600;          // longest line (a test-build readings row is up to ~460)
constexpr int kQueueLen = 24;
constexpr int kPending = 24;           // events kept while the card is out
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
int gYear = 0, gMonth = 0;

char* gHeadR = nullptr;
char* gHeadE = nullptr;
char* gHeadH = nullptr;

// Owned by the task only:
char gBatch[wineador::kReadingsBatch * kLine];
size_t gBatchLen = 0;
int gBatchRows = 0;
char gBatchPath[40] = {};
char gPending[kPending][kLine];
int gPendHead = 0, gPendCount = 0;
uint32_t gLastRemount = 0, gLastSpace = 0;
bool gRetentionDone = false;

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

// The one place the card is written. Makes the folders, writes the header into a
// new (empty) file, then the data. Timed. False on any failure.
bool appendFile(const char* path, const char* header, const char* data, size_t len) {
  gBusySince = millis();
  gBusy = true;
  const uint32_t t0 = millis();
  bool ok = true;
  if (!SD.exists("/logs") && !SD.mkdir("/logs")) ok = false;
  if (ok) {
    char dir[24];
    const char* slash = strrchr(path, '/');
    const size_t dl = slash ? (size_t)(slash - path) : 0;
    if (dl > 0 && dl < sizeof(dir)) {
      memcpy(dir, path, dl);
      dir[dl] = 0;
      if (strcmp(dir, "/logs") != 0 && !SD.exists(dir) && !SD.mkdir(dir)) ok = false;
    }
  }
  File f;
  if (ok) {
    f = SD.open(path, FILE_APPEND);
    if (!f) ok = false;
  }
  if (ok && f.size() == 0 && header) ok = f.write((const uint8_t*)header, strlen(header)) == strlen(header);
  if (ok) ok = f.write((const uint8_t*)data, len) == len;
  if (f) f.close();
  const uint32_t ms = millis() - t0;
  gBusy = false;
  if (ms > gWorstSinceTake) gWorstSinceTake = ms;
  withStats([&](Stats& s) {
    s.lastWriteMs = ms;
    if (ms > s.worstWriteMs) s.worstWriteMs = ms;
  });
  if (!ok) setError("write failed:", path);
  return ok;
}

bool cardOk() {
  bool ok;
  portENTER_CRITICAL(&gMux);
  ok = gStats.mounted && !gStats.failed;
  portEXIT_CRITICAL(&gMux);
  return ok;
}

void keepEvent(const char* line) {
  if (gPendCount == kPending) {
    gPendHead = (gPendHead + 1) % kPending;
    gPendCount--;
    withStats([](Stats& s) { s.eventsDropped++; });
  }
  strncpy(gPending[(gPendHead + gPendCount) % kPending], line, kLine - 1);
  gPending[(gPendHead + gPendCount) % kPending][kLine - 1] = 0;
  gPendCount++;
  withStats([](Stats& s) { s.eventsPending = gPendCount; });
}

bool writeEvent(const char* line) {
  int y, m;
  monthOf(line, &y, &m);
  char path[40];
  wineador::logPath(path, sizeof(path), LogKind::Events, y, m);
  if (!appendFile(path, gHeadE, line, strlen(line))) return false;
  withStats([](Stats& s) { s.eventsWritten++; });
  return true;
}

void flushBatch() {
  if (gBatchRows == 0) return;
  if (!cardOk() || !appendFile(gBatchPath, gHeadR, gBatch, gBatchLen)) {
    withStats([&](Stats& s) { s.rowsDropped += gBatchRows; s.rowsBuffered = 0; });
  } else {
    const int rows = gBatchRows;
    withStats([&](Stats& s) {
      s.rowsWritten += rows;
      s.flushes++;
      s.lastFlushRows = rows;
      s.lastFlushUptimeMs = (uint64_t)(esp_timer_get_time() / 1000);
      s.rowsBuffered = 0;
    });
  }
  gBatchLen = 0;
  gBatchRows = 0;
}

void addReading(const char* line) {
  int y, m;
  monthOf(line, &y, &m);
  char path[40];
  wineador::logPath(path, sizeof(path), LogKind::Readings, y, m);
  if (strcmp(path, gBatchPath) != 0) {            // a new month: finish the old file first
    flushBatch();
    strncpy(gBatchPath, path, sizeof(gBatchPath) - 1);
    withStats([&](Stats& s) { strncpy(s.readingsPath, path, sizeof(s.readingsPath) - 1); });
  }
  if (!cardOk()) { withStats([](Stats& s) { s.rowsDropped++; }); return; }
  const size_t n = strlen(line);
  if (gBatchLen + n >= sizeof(gBatch)) flushBatch();
  memcpy(gBatch + gBatchLen, line, n);
  gBatchLen += n;
  gBatchRows++;
  withStats([&](Stats& s) { s.rowsBuffered = gBatchRows; });
  if (gBatchRows >= wineador::kReadingsBatch) flushBatch();
}

void refreshSpace() {
  gBusySince = millis();
  gBusy = true;
  const uint64_t total = SD.totalBytes(), used = SD.usedBytes();
  gBusy = false;
  withStats([&](Stats& s) { s.totalBytes = total; s.freeBytes = total > used ? total - used : 0; });
  gLastSpace = millis();
}

// An event the task writes itself (retention, recovery): same format as the loop's.
void selfEvent(const char* type, const char* detail) {
  char ts[24], line[kLine];
  const time_t t = time(nullptr);
  struct tm tmv;
  gmtime_r(&t, &tmv);
  strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tmv);
  wineador::formatEventRow(line, sizeof(line), ts, true, (uint64_t)(esp_timer_get_time() / 1000), type, detail);
  if (!cardOk() || !writeEvent(line)) keepEvent(line);
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
  // retentionPlan sorts files; keep the paths alongside by sorting an index copy
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
  SD.end();
  const bool ok = SD.begin(pins::kSdCs, sdcard::spi(), 4000000);
  gBusy = false;
  if (!ok) return;
  Stats before = stats();
  withStats([](Stats& s) { s.mounted = true; s.failed = false; });
  refreshSpace();
  char d[160];
  snprintf(d, sizeof(d), "SD card back: remounted; %d kept events written now, %lu events and %lu readings rows lost while out",
           gPendCount, (unsigned long)before.eventsDropped, (unsigned long)before.rowsDropped);
  // kept events first, in order
  while (gPendCount > 0 && cardOk()) {
    if (!writeEvent(gPending[gPendHead])) break;
    gPendHead = (gPendHead + 1) % kPending;
    gPendCount--;
  }
  withStats([](Stats& s) { s.eventsPending = gPendCount; });
  selfEvent("sd", d);
}

void task(void*) {
  for (;;) {
    Msg m;
    const bool got = xQueueReceive(gQ, &m, pdMS_TO_TICKS(500)) == pdTRUE;
    if (!gStarted) {
      if (got) xQueueSendToFront(gQ, &m, 0);   // keep it until we may write
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if (!cardOk() && millis() - gLastRemount >= kRemountMs) tryRemount();
    if (cardOk() && !gRetentionDone) retention();
    if (cardOk() && millis() - gLastSpace >= kSpaceRefreshMs) refreshSpace();
    if (!got) continue;
    switch (m.kind) {
      case Kind::Reading: addReading(m.text); break;
      case Kind::Event:
        if (!cardOk() || !writeEvent(m.text)) keepEvent(m.text);
        break;
      case Kind::Hourly: {
        char path[40];
        wineador::logPath(path, sizeof(path), LogKind::Hourly, 0, 0);
        if (cardOk() && appendFile(path, gHeadH, m.text, strlen(m.text))) withStats([](Stats& s) { s.hourlyWritten++; });
        break;
      }
    }
  }
}

void send(Kind k, const char* line) {
  if (!gQ) return;
  static Msg m;                    // only the loop task sends: one static buffer, no big stack copy
  m.kind = k;
  strncpy(m.text, line, kLine - 1);
  m.text[kLine - 1] = 0;
  if (xQueueSend(gQ, &m, 0) != pdTRUE) {
    withStats([&](Stats& s) { if (k == Kind::Reading) s.rowsDropped++; else s.eventsDropped++; });
  }
}

}  // namespace

void begin(const char* readingsHeader, const char* eventsHeader, const char* hourlyHeader) {
  if (gQ) return;
  gHeadR = dupe(readingsHeader);
  gHeadE = dupe(eventsHeader);
  gHeadH = dupe(hourlyHeader);
  const bool mounted = sdcard::mounted();     // mounted at boot by sdcard::begin
  withStats([&](Stats& s) { s.mounted = mounted; s.failed = !mounted; if (!mounted) snprintf(s.lastError, sizeof(s.lastError), "no card at boot"); });
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

}  // namespace sdlog
