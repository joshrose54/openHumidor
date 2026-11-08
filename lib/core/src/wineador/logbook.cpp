#include "wineador/logbook.h"
#include "wineador/sensor_table.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

namespace wineador {

namespace {
struct Out {
  char* p;
  size_t n;
  size_t len = 0;
  Out(char* p_, size_t n_) : p(p_), n(n_) { if (n) p[0] = 0; }
  void add(const char* fmt, ...) {
    if (len + 1 >= n) return;
    va_list a;
    va_start(a, fmt);
    int w = vsnprintf(p + len, n - len, fmt, a);
    va_end(a);
    if (w > 0) len = (len + w < n) ? len + w : n - 1;
  }
};
const char* profileWord(Profile p) { return p == Profile::Bench ? "bench" : "real"; }

void headerCommon(Out& o, const char* what, const LogHeaderInfo& h) {
  o.add("# wineador %s\n", what);
  o.add("# schema %d\n", kLogSchema);
  o.add("# firmware %s\n", h.firmware);
  o.add("# profile %s%s\n", profileWord(h.profile), h.testBuild ? " TEST-BUILD (scripted readings; real ones in *_real columns)" : "");
  o.add("# timestamps UTC; time_trusted 0 = the clock lost time and NTP has not corrected it yet\n");
}
}  // namespace

// ---------------------------------------------------------------- CSV
int csvField(char* out, size_t n, const char* text) {
  Out o(out, n);
  bool quote = false;
  for (const char* c = text; *c; c++)
    if (*c == ',' || *c == '"' || *c == '\n' || *c == '\r') { quote = true; break; }
  if (!quote) { o.add("%s", text); return (int)o.len; }
  o.add("\"");
  for (const char* c = text; *c; c++) {
    if (*c == '"') o.add("\"\"");
    else if (*c == '\n' || *c == '\r') o.add(" ");
    else o.add("%c", *c);
  }
  o.add("\"");
  return (int)o.len;
}

// ---------------------------------------------------------------- paths
int logPath(char* out, size_t n, LogKind k, int year, int month, int schema) {
  if (k == LogKind::Hourly) return snprintf(out, n, "/logs/hourly.csv");
  const char* what = k == LogKind::Readings ? "readings" : "events";
  if (schema <= 1) return snprintf(out, n, "/logs/%04d/%04d-%02d-%s.csv", year, year, month, what);
  return snprintf(out, n, "/logs/%04d/%04d-%02d-%s-s%d.csv", year, year, month, what, schema);
}

int logYearDir(char* out, size_t n, int year) { return snprintf(out, n, "/logs/%04d", year); }

bool parseReadingsName(const char* name, int* year, int* month) {
  // YYYY-MM-readings.csv or YYYY-MM-readings-sN.csv
  if (strlen(name) < 20) return false;
  for (int i = 0; i < 4; i++) if (name[i] < '0' || name[i] > '9') return false;
  if (name[4] != '-' || name[5] < '0' || name[5] > '9' || name[6] < '0' || name[6] > '9') return false;
  if (strncmp(name + 7, "-readings", 9) != 0) return false;
  const char* rest = name + 16;
  if (strcmp(rest, ".csv") != 0 && !(rest[0] == '-' && rest[1] == 's' && strstr(rest, ".csv"))) return false;
  const int y = atoi(name), m = (name[5] - '0') * 10 + (name[6] - '0');
  if (m < 1 || m > 12) return false;
  *year = y;
  *month = m;
  return true;
}

// ---------------------------------------------------------------- headers
int formatReadingsHeader(char* out, size_t n, const LogHeaderInfo& h) {
  Out o(out, n);
  headerCommon(o, "readings", h);
  for (int i = 0; i < kNumSensors; i++) {
    const SensorInfo& s = kSensors[i];
    o.add("# ch%u serial 0x%08lX %s rh_gain %.4f rh_offset %.3f\n", (unsigned)s.channel, (unsigned long)s.serial,
          s.position, s.rhGain, s.rhOffset);
  }
  o.add("timestamp,time_trusted");
  for (int i = 0; i < kNumSensors; i++) {
    const unsigned c = kSensors[i].channel;
    o.add(",ch%u_t,ch%u_rh,ch%u_rh_raw,ch%u_flag", c, c, c, c);
  }
  o.add(",cab_mean_t,cab_min_t,cab_max_t,cab_mean_rh,control_t,fan_pwm,fan_rpm,compressor,heater_duty,humidifier,"
        "interlock,loop_max_ms,heap_free,sd_worst_ms");
  if (h.testBuild)
    for (int i = 0; i < kNumSensors; i++) {
      const unsigned c = kSensors[i].channel;
      o.add(",ch%u_t_real,ch%u_rh_real,ch%u_rh_raw_real,ch%u_bus_real", c, c, c, c);
    }
  o.add("\n");
  return (int)o.len;
}

int formatEventsHeader(char* out, size_t n, const LogHeaderInfo& h) {
  Out o(out, n);
  headerCommon(o, "events", h);
  o.add("# uptime_ms: since this boot (for timing checks across clock corrections)\n");
  o.add("timestamp,time_trusted,uptime_ms,type,detail\n");
  return (int)o.len;
}

int formatHourlyHeader(char* out, size_t n, const LogHeaderInfo& h) {
  Out o(out, n);
  headerCommon(o, "hourly", h);
  o.add("# one row per UTC hour: min/max/mean of each value over the hour's sensor reads (healthy sensors only);\n");
  o.add("# *_on_s = seconds that load was on in the hour\n");
  o.add("hour,time_trusted,samples");
  for (int i = 0; i < kNumSensors; i++) {
    const unsigned c = kSensors[i].channel;
    o.add(",ch%u_t_min,ch%u_t_max,ch%u_t_mean,ch%u_rh_min,ch%u_rh_max,ch%u_rh_mean", c, c, c, c, c, c);
  }
  o.add(",cab_mean_t_min,cab_mean_t_max,cab_mean_t_mean,cab_mean_rh_min,cab_mean_rh_max,cab_mean_rh_mean,"
        "control_t_min,control_t_max,control_t_mean,fan_pwm_mean,fan_rpm_mean,compressor_on_s,heater_on_s,humidifier_on_s\n");
  return (int)o.len;
}

// ---------------------------------------------------------------- rows
namespace {
void addChannel(Out& o, const ChannelLog& c) {
  if (c.read) o.add(",%.2f,%.2f,%.2f,%s", c.tempC, c.rhPct, c.rhRawPct, c.flag);
  else        o.add(",,,,%s", c.flag);
}
}  // namespace

int formatReadingsRow(char* out, size_t n, const ReadingsRow& r, bool testBuild) {
  Out o(out, n);
  o.add("%s,%d", r.timestamp, r.timeTrusted ? 1 : 0);
  for (int i = 0; i < kNumSensors; i++) addChannel(o, r.ch[i]);
  const CabinetSummary& c = r.cab;
  if (c.cabinetOk > 0) o.add(",%.2f,%.2f,%.2f,%.2f,%.2f", c.meanC, c.minC, c.maxC, c.meanRhPct, c.controlC);
  else                 o.add(",,,,,");
  o.add(",%u,%lu,%d,%u,%d,%s,%lu.%lu,%lu,%lu", (unsigned)r.fanPct, (unsigned long)r.fanRpm, r.cool ? 1 : 0,
        (unsigned)r.heaterDutyPct, r.humid ? 1 : 0, r.relayHeating ? "heat" : "cool",
        (unsigned long)(r.loopMaxUs / 1000), (unsigned long)((r.loopMaxUs % 1000) / 100), (unsigned long)r.heapFree,
        (unsigned long)r.sdWorstMs);
  if (testBuild)
    for (int i = 0; i < kNumSensors; i++) addChannel(o, r.real[i]);
  o.add("\n");
  return (int)o.len;
}

int formatEventRow(char* out, size_t n, const char* timestamp, bool trusted, uint64_t uptimeMs, const char* type,
                   const char* detail) {
  Out o(out, n);
  o.add("%s,%d,%llu,%s,", timestamp, trusted ? 1 : 0, (unsigned long long)uptimeMs, type);
  char field[400];
  csvField(field, sizeof(field), detail);
  o.add("%s\n", field);
  return (int)o.len;
}

// ---------------------------------------------------------------- retention
int retentionPlan(LogFileInfo* f, int count, uint64_t totalBytes, uint64_t freeBytes, int curYear, int curMonth) {
  if (totalBytes == 0 || freeBytes * 10 >= totalBytes) return 0;           // 10 % or more free: nothing
  // oldest first (insertion sort: a handful of files)
  for (int i = 1; i < count; i++) {
    LogFileInfo k = f[i];
    int j = i - 1;
    while (j >= 0 && (f[j].year * 12 + f[j].month) > (k.year * 12 + k.month)) { f[j + 1] = f[j]; j--; }
    f[j + 1] = k;
  }
  int del = 0;
  uint64_t free = freeBytes;
  while (del < count && free * 5 <= totalBytes) {                          // until above 20 %
    if (f[del].year == curYear && f[del].month == curMonth) break;        // never the month in use
    free += f[del].bytes;
    del++;
  }
  return del;
}

// ---------------------------------------------------------------- bus events
bool BusEventLimiter::allow(int i, uint32_t nowMs) {
  if (i < 0 || i >= kNumSensors) return false;
  Ch& c = ch_[i];
  if (!c.open) { c.open = true; c.sinceMs = nowMs; c.logged = 0; c.more = 0; }
  if (c.logged < kPerHour) { c.logged++; return true; }
  c.more++;
  return false;
}

bool BusEventLimiter::takeSuppressed(uint32_t nowMs, int* index, uint32_t* count) {
  for (int i = 0; i < kNumSensors; i++) {
    Ch& c = ch_[i];
    if (!c.open || elapsedMs(nowMs, c.sinceMs) < 3600000UL) continue;
    const uint32_t more = c.more;
    c.open = false;
    if (more > 0) { *index = i; *count = more; return true; }
  }
  return false;
}

// ---------------------------------------------------------------- guard blocks
void GuardBlockTracker::update(const GuardDecisions& d) {
  const LoadDecision* ld[3] = {&d.cool, &d.heat, &d.humid};
  for (int L = 0; L < 3; L++) {
    const LoadDecision& x = *ld[L];
    const GuardBlock now = (x.demanded && !x.run) ? x.block : GuardBlock::None;
    if (now == last_[L]) continue;
    if (qn_ < 6) {
      Change c;
      c.load = L;
      c.from = last_[L];
      c.to = now;
      c.started = (now == GuardBlock::None && x.run);
      c.waitMs = x.waitMs;
      q_[qn_++] = c;
    }
    last_[L] = now;
  }
}

bool GuardBlockTracker::take(Change& c) {
  if (qn_ == 0) return false;
  c = q_[0];
  for (int i = 1; i < qn_; i++) q_[i - 1] = q_[i];
  qn_--;
  return true;
}

int GuardBlockTracker::format(char* out, size_t n, const Change& c) {
  static const char* names[3] = {"compressor", "heater", "humidifier"};
  const char* name = (c.load >= 0 && c.load < 3) ? names[c.load] : "?";
  if (c.to != GuardBlock::None) {
    if (c.waitMs) return snprintf(out, n, "%s blocked: %s (%lu s left)", name, guardBlockName(c.to),
                                  (unsigned long)((c.waitMs + 999) / 1000));
    return snprintf(out, n, "%s blocked: %s", name, guardBlockName(c.to));
  }
  return snprintf(out, n, "%s block cleared (%s; was %s)", name, c.started ? "started" : "no longer asked for",
                  guardBlockName(c.from));
}

}  // namespace wineador
