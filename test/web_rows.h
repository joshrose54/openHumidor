#pragma once
// For the native web tests: log rows exactly as the firmware's own writers make them (logbook.h,
// hourly.h), so what the tests read back is what a card would hold.
#include <stdio.h>
#include <string>
#include "wineador/logbook.h"
#include "wineador/hourly.h"
#include "wineador/uk_time.h"

namespace webrows {

using namespace wineador;

inline void iso(char* out, size_t n, int64_t t) {
  int y, m, d;
  civilFromDays(t / 86400, &y, &m, &d);
  const int64_t s = t % 86400;
  snprintf(out, n, "%04d-%02d-%02dT%02d:%02d:%02dZ", y, m, d, (int)(s / 3600), (int)(s / 60 % 60), (int)(s % 60));
}

inline std::string readingsRow(int64_t t, float meanC, bool cool) {
  char ts[24];
  iso(ts, sizeof(ts), t);
  ReadingsRow r;
  r.timestamp = ts;
  r.timeTrusted = true;
  for (int i = 0; i < kNumSensors; i++) {
    r.ch[i].read = true;
    r.ch[i].tempC = meanC + 0.1f * i;
    r.ch[i].rhPct = 62.0f;
    r.ch[i].rhRawPct = 63.0f;
    r.ch[i].flag = "ok";
    r.real[i] = r.ch[i];
  }
  r.cab.cabinetOk = 4;
  r.cab.meanC = meanC;
  r.cab.minC = meanC - 0.3f;
  r.cab.maxC = meanC + 0.4f;
  r.cab.meanRhPct = 61.5f;
  r.cab.controlC = meanC;
  r.fanPct = 40;
  r.fanRpm = 1230;
  r.cool = cool;
  char line[1024];
  const int k = formatReadingsRow(line, sizeof(line), r, false);
  return std::string(line, (size_t)k);
}

inline std::string hourlyRow(int64_t hourStart) {
  char ts[24];
  iso(ts, sizeof(ts), hourStart);
  HourlyStats h;
  h.begin(1, ts, true);
  bool healthy[kNumSensors] = {true, true, true, true, true, true};
  float t[kNumSensors], rh[kNumSensors];
  for (int i = 0; i < kNumSensors; i++) { t[i] = 19.5f + 0.1f * i; rh[i] = 61.0f; }
  CabinetSummary c;
  c.cabinetOk = 4;
  c.meanC = 19.5f;
  c.meanRhPct = 61.0f;
  c.controlC = c.meanC;
  h.addSweep(healthy, t, rh, c, 40, 1200);
  char line[1500];
  const int k = h.formatRow(line, sizeof(line));
  return std::string(line, (size_t)k);
}

inline std::string eventRow(int64_t t, const char* type, const char* detail, bool trusted = true) {
  char ts[24];
  iso(ts, sizeof(ts), t);
  char line[600];
  const int k = formatEventRow(line, sizeof(line), ts, trusted, 1000, type, detail);
  return std::string(line, (size_t)k);
}

}  // namespace webrows
