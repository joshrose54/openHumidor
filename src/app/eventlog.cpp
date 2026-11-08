#include "eventlog.h"
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_timer.h"
#include "wineador/logbook.h"
#include "drivers/sd_log.h"

namespace eventlog {

namespace {
constexpr int kKeep = 24;
Entry gRing[kKeep];
int gHead = 0, gCount = 0;
uint32_t gTotal = 0;
bool gClockSet = false, gTrusted = false;

void add(bool print, const char* type, const char* fmt, va_list ap) {
  Entry& e = gRing[(gHead + gCount) % kKeep];
  if (gCount == kKeep) gHead = (gHead + 1) % kKeep;
  else gCount++;
  isoNow(e.timestamp, sizeof(e.timestamp));
  e.trusted = gClockSet && gTrusted;
  e.uptimeMs = (uint64_t)(esp_timer_get_time() / 1000);
  strncpy(e.type, type, sizeof(e.type) - 1);
  e.type[sizeof(e.type) - 1] = 0;
  vsnprintf(e.detail, sizeof(e.detail), fmt, ap);
  gTotal++;
  if (print) Serial.printf("  %s\n", e.detail);
  char row[300];
  wineador::formatEventRow(row, sizeof(row), e.timestamp, e.trusted, e.uptimeMs, e.type, e.detail);
  sdlog::event(row);
}
}  // namespace

void setClock(bool set, bool trusted) { gClockSet = set; gTrusted = trusted; }

void log(const char* type, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  add(true, type, fmt, ap);
  va_end(ap);
}

void logQuiet(const char* type, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  add(false, type, fmt, ap);
  va_end(ap);
}

int count() { return gCount; }
const Entry& recent(int i) { return gRing[(gHead + i) % kKeep]; }
uint32_t total() { return gTotal; }

void isoNow(char* out, size_t n) {
  const time_t t = time(nullptr);
  struct tm tmv;
  gmtime_r(&t, &tmv);
  strftime(out, n, "%Y-%m-%dT%H:%M:%SZ", &tmv);
}

bool clockTrusted() { return gClockSet && gTrusted; }

}  // namespace eventlog
