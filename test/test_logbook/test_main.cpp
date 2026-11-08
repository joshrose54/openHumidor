// SD logging logic (stage 6, spec 5.4): paths by UTC month and schema, headers
// with each serial's gain and offset, rows, CSV quoting, retention, the
// CRC/timeout limit, guard-block events, the hourly file.
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include "wineador/logbook.h"
#include "wineador/hourly.h"
#include "wineador/sensor_table.h"
#include "wineador/settings.h"
#include "wineador/sensor_health.h"
#include <initializer_list>

using namespace wineador;

void setUp() {}
void tearDown() {}

static int countChar(const char* s, char c) { int n = 0; for (; *s; s++) if (*s == c) n++; return n; }

// The column header is the last line of a header block.
static const char* lastLine(const char* s) {
  const char* end = s + strlen(s) - 1;     // the final newline
  const char* p = end - 1;
  while (p > s && *p != '\n') p--;
  return (*p == '\n') ? p + 1 : s;
}

void test_paths() {
  char p[64];
  logPath(p, sizeof(p), LogKind::Readings, 2026, 9);
  TEST_ASSERT_EQUAL_STRING("/logs/2026/2026-09-readings.csv", p);
  logPath(p, sizeof(p), LogKind::Events, 2026, 11);
  TEST_ASSERT_EQUAL_STRING("/logs/2026/2026-11-events.csv", p);
  logPath(p, sizeof(p), LogKind::Hourly, 2026, 11);
  TEST_ASSERT_EQUAL_STRING("/logs/hourly.csv", p);
  logPath(p, sizeof(p), LogKind::Readings, 2027, 1, 2);
  TEST_ASSERT_EQUAL_STRING("/logs/2027/2027-01-readings-s2.csv", p);   // a new schema, a new file
  logYearDir(p, sizeof(p), 2026);
  TEST_ASSERT_EQUAL_STRING("/logs/2026", p);
}

void test_parse_readings_name() {
  int y = 0, m = 0;
  TEST_ASSERT_TRUE(parseReadingsName("2026-09-readings.csv", &y, &m));
  TEST_ASSERT_EQUAL_INT(2026, y);
  TEST_ASSERT_EQUAL_INT(9, m);
  TEST_ASSERT_TRUE(parseReadingsName("2027-01-readings-s2.csv", &y, &m));
  TEST_ASSERT_FALSE(parseReadingsName("2026-09-events.csv", &y, &m));
  TEST_ASSERT_FALSE(parseReadingsName("hourly.csv", &y, &m));
  TEST_ASSERT_FALSE(parseReadingsName("2026-13-readings.csv", &y, &m));
  TEST_ASSERT_FALSE(parseReadingsName("._2026-09-readings.csv", &y, &m));   // macOS litter
}

void test_readings_header_has_profile_serials_and_columns() {
  char h[3000];
  LogHeaderInfo info;
  info.firmware = "0.1.0-stage6-BENCH-TEST";
  info.profile = Profile::Bench;
  info.testBuild = true;
  formatReadingsHeader(h, sizeof(h), info);
  TEST_ASSERT_NOT_NULL(strstr(h, "# wineador readings\n# schema 1\n# firmware 0.1.0-stage6-BENCH-TEST\n# profile bench TEST-BUILD"));
  TEST_ASSERT_NOT_NULL(strstr(h, "# ch2 serial 0x0AD409ED top_front rh_gain 1.0067 rh_offset -2.556"));
  TEST_ASSERT_NOT_NULL(strstr(h, "# ch7 serial 0x0E42EFBA ambient rh_gain 1.0151 rh_offset -3.475"));
  const char* cols = lastLine(h);
  TEST_ASSERT_EQUAL_INT(0, strncmp(cols, "timestamp,time_trusted,ch2_t,ch2_rh,ch2_rh_raw,ch2_flag", 54));
  TEST_ASSERT_NOT_NULL(strstr(cols, ",loop_max_ms,heap_free,sd_worst_ms,"));
  TEST_ASSERT_NOT_NULL(strstr(cols, ",ch7_bus_real\n"));
  // the cabinet build has no *_real columns
  info.testBuild = false;
  info.profile = Profile::Real;
  formatReadingsHeader(h, sizeof(h), info);
  TEST_ASSERT_NULL(strstr(h, "_real"));
  TEST_ASSERT_NOT_NULL(strstr(h, "# profile real\n"));
}

void test_readings_row_matches_header_columns() {
  for (bool tb : {false, true}) {
    char h[3000], r[1500];
    LogHeaderInfo info;
    info.testBuild = tb;
    formatReadingsHeader(h, sizeof(h), info);
    ReadingsRow row;
    row.timestamp = "2026-11-08T16:00:00Z";
    row.timeTrusted = true;
    for (int i = 0; i < kNumSensors; i++) {
      row.ch[i].read = (i != 2);                       // ch4 failed this sweep
      row.ch[i].tempC = 20.0f + i;
      row.ch[i].rhRawPct = 64.0f;
      row.ch[i].rhPct = correctedRh(i, 64.0f);
      row.ch[i].flag = (i == 2) ? "absent" : "ok";
      row.real[i] = row.ch[i];
    }
    row.cab.cabinetOk = 3;
    row.cab.meanC = 20.5f;
    row.fanPct = 40;
    row.cool = true;
    row.loopMaxUs = 3456;
    formatReadingsRow(r, sizeof(r), row, tb);
    TEST_ASSERT_EQUAL_INT(countChar(lastLine(h), ','), countChar(r, ','));   // same number of columns
    TEST_ASSERT_NOT_NULL(strstr(r, "2026-11-08T16:00:00Z,1,20.00,"));
    TEST_ASSERT_NOT_NULL(strstr(r, ",,,,absent,"));      // a failed read: values blank, flag kept
    TEST_ASSERT_NOT_NULL(strstr(r, ",40,0,1,0,0,cool,3.4,"));
  }
}

void test_rh_column_is_gain_times_raw_plus_offset() {
  // The firmware fills rhPct from correctedRh(); check the file would show the
  // build log's line for each serial.
  for (int i = 0; i < kNumSensors; i++) {
    const float raw = 65.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, kSensors[i].rhGain * raw + kSensors[i].rhOffset, correctedRh(i, raw));
  }
}

void test_event_row_and_quoting() {
  char e[600];
  formatEventRow(e, sizeof(e), "2026-11-08T16:00:01Z", false, 123456, "strip", "RH STRIP STOP: temperature floor. Mean RH 75.2%, vp 1.610");
  TEST_ASSERT_EQUAL_STRING("2026-11-08T16:00:01Z,0,123456,strip,\"RH STRIP STOP: temperature floor. Mean RH 75.2%, vp 1.610\"\n", e);
  formatEventRow(e, sizeof(e), "T", true, 1, "x", "say \"hi\"\nthere");
  TEST_ASSERT_EQUAL_STRING("T,1,1,x,\"say \"\"hi\"\" there\"\n", e);
  formatEventRow(e, sizeof(e), "T", true, 1, "boot", "POWERON");
  TEST_ASSERT_EQUAL_STRING("T,1,1,boot,POWERON\n", e);
  char h[600];
  formatEventsHeader(h, sizeof(h), LogHeaderInfo{});
  TEST_ASSERT_EQUAL_STRING("timestamp,time_trusted,uptime_ms,type,detail\n", lastLine(h));
}

void test_retention() {
  const uint64_t GB = 1024ULL * 1024 * 1024;
  LogFileInfo f[4] = {{2026, 3, 2 * GB}, {2025, 12, 3 * GB}, {2026, 11, 1 * GB}, {2026, 1, 2 * GB}};
  // 10 % or more free: nothing deleted
  TEST_ASSERT_EQUAL_INT(0, retentionPlan(f, 4, 60 * GB, 6 * GB, 2026, 11));
  // 5 GB free of 60 (8 %): delete oldest until > 12 GB free (20 %): 2025-12 (+3 = 8), 2026-01 (+2 = 10), 2026-03 (+2 = 12 — not above), stop at current
  LogFileInfo g[4] = {{2026, 3, 2 * GB}, {2025, 12, 3 * GB}, {2026, 11, 1 * GB}, {2026, 1, 2 * GB}};
  const int del = retentionPlan(g, 4, 60 * GB, 5 * GB, 2026, 11);
  TEST_ASSERT_EQUAL_INT(3, del);
  TEST_ASSERT_EQUAL_INT(2025, g[0].year);
  TEST_ASSERT_EQUAL_INT(12, g[0].month);
  TEST_ASSERT_EQUAL_INT(1, g[1].month);
  TEST_ASSERT_EQUAL_INT(3, g[2].month);
  TEST_ASSERT_EQUAL_INT(11, g[3].month);                // the current month is never deleted
  // only the current month left: nothing to delete even if full
  LogFileInfo one[1] = {{2026, 11, 50 * GB}};
  TEST_ASSERT_EQUAL_INT(0, retentionPlan(one, 1, 60 * GB, 1 * GB, 2026, 11));
  // stops as soon as above 20 %
  LogFileInfo h[3] = {{2026, 1, 20 * GB}, {2026, 2, 5 * GB}, {2026, 3, 5 * GB}};
  TEST_ASSERT_EQUAL_INT(1, retentionPlan(h, 3, 60 * GB, 3 * GB, 2026, 11));
}

void test_bus_event_limiter() {
  BusEventLimiter lim;
  uint32_t t = 1000;
  int logged = 0;
  for (int k = 0; k < 25; k++) { if (lim.allow(1, t)) logged++; t += 15000; }
  TEST_ASSERT_EQUAL_INT(10, logged);
  TEST_ASSERT_TRUE(lim.allow(3, t));                     // other channels have their own count
  int idx; uint32_t more;
  TEST_ASSERT_FALSE(lim.takeSuppressed(t, &idx, &more)); // hour not over
  t = 1000 + 3600000UL;
  TEST_ASSERT_TRUE(lim.takeSuppressed(t, &idx, &more));
  TEST_ASSERT_EQUAL_INT(1, idx);
  TEST_ASSERT_EQUAL_UINT32(15, more);
  TEST_ASSERT_FALSE(lim.takeSuppressed(t, &idx, &more));
  TEST_ASSERT_TRUE(lim.allow(1, t + 1));                 // a new hour: logging again
}

void test_guard_block_tracker() {
  GuardBlockTracker g;
  GuardDecisions d;
  GuardBlockTracker::Change c;
  char s[120];
  d.heat.demanded = true; d.heat.block = GuardBlock::Crossover; d.heat.waitMs = 11500;
  g.update(d);
  TEST_ASSERT_TRUE(g.take(c));
  GuardBlockTracker::format(s, sizeof(s), c);
  TEST_ASSERT_EQUAL_STRING("heater blocked: crossover (12 s left)", s);
  for (int i = 0; i < 100; i++) g.update(d);             // same reason every pass: no more events
  TEST_ASSERT_FALSE(g.take(c));
  d.heat.block = GuardBlock::MinOff;                     // reason changes
  g.update(d);
  TEST_ASSERT_TRUE(g.take(c));
  TEST_ASSERT_TRUE(c.from == GuardBlock::Crossover && c.to == GuardBlock::MinOff);
  d.heat.block = GuardBlock::None; d.heat.run = true;    // clears: started
  g.update(d);
  TEST_ASSERT_TRUE(g.take(c));
  GuardBlockTracker::format(s, sizeof(s), c);
  TEST_ASSERT_EQUAL_STRING("heater block cleared (started; was min-off)", s);
  // a load not asked for is never "blocked", whatever its guards say
  d.humid.demanded = false; d.humid.block = GuardBlock::BootHold;
  g.update(d);
  TEST_ASSERT_FALSE(g.take(c));
}

void test_hourly_row() {
  HourlyStats h;
  h.begin(1234, "2026-11-08T16:00:00Z", true);
  bool ok[kNumSensors] = {true, true, true, true, true, false};
  float t[kNumSensors] = {20, 20, 20, 20, 20, 0}, rh[kNumSensors] = {62, 62, 62, 62, 62, 0};
  CabinetSummary c;
  c.cabinetOk = 4; c.meanC = 20; c.meanRhPct = 62; c.controlC = 20;
  h.addSweep(ok, t, rh, c, 20, 400);
  t[0] = 22; c.meanC = 21; c.controlC = 21;
  h.addSweep(ok, t, rh, c, 40, 900);
  LoadOutputs on; on.cool = true;
  for (int i = 0; i < 90; i++) h.addOnTime(1000, on);
  char r[1500], hdr[3000];
  h.formatRow(r, sizeof(r));
  formatHourlyHeader(hdr, sizeof(hdr), LogHeaderInfo{});
  TEST_ASSERT_EQUAL_INT(countChar(lastLine(hdr), ','), countChar(r, ','));
  TEST_ASSERT_NOT_NULL(strstr(r, "2026-11-08T16:00:00Z,1,2,20.00,22.00,21.00,62.00,62.00,62.00,"));
  TEST_ASSERT_NOT_NULL(strstr(r, ",,,,,,,"));            // ch7 not healthy: blank
  TEST_ASSERT_NOT_NULL(strstr(r, ",30.0,650,90,0,0\n"));  // fan mean, rpm mean, compressor 90 s
}

void test_worst_case_rows_fit_the_sd_queue_line() {
  // The SD task takes lines of up to 599 characters (src/drivers/sd_log.cpp kLine = 600).
  char r[2000];
  ReadingsRow row;
  row.timestamp = "2026-11-08T16:00:00Z";
  for (int i = 0; i < kNumSensors; i++) {
    row.ch[i].read = true; row.ch[i].tempC = -10.55f; row.ch[i].rhPct = 100.0f; row.ch[i].rhRawPct = -100.55f;
    row.ch[i].flag = exclusionName(Exclusion::Implausible);
    for (Exclusion e : {Exclusion::NotYetRead, Exclusion::Divergence, Exclusion::WrongSerial, Exclusion::BusFault})
      if (strlen(exclusionName(e)) > strlen(row.ch[i].flag)) row.ch[i].flag = exclusionName(e);
    row.real[i] = row.ch[i];
    row.real[i].flag = busResultName(BusResult::ShortRead);
  }
  row.cab.cabinetOk = 4; row.cab.meanC = row.cab.minC = row.cab.maxC = row.cab.controlC = -10.55f; row.cab.meanRhPct = 100.0f;
  row.fanPct = 100; row.fanRpm = 99999; row.cool = true; row.heaterDutyPct = 100; row.humid = true; row.relayHeating = true;
  row.loopMaxUs = 999999; row.heapFree = 999999; row.sdWorstMs = 99999;
  const int len = formatReadingsRow(r, sizeof(r), row, true);
  TEST_ASSERT_LESS_THAN_INT(599, len);
  HourlyStats h;
  h.begin(1, "2026-11-08T16:00:00Z", true);
  bool ok[kNumSensors] = {true, true, true, true, true, true};
  float t[kNumSensors], rh[kNumSensors];
  for (int i = 0; i < kNumSensors; i++) { t[i] = -10.55f; rh[i] = 100.0f; }
  h.addSweep(ok, t, rh, row.cab, 100, 99999);
  LoadOutputs on; on.cool = on.humid = true;
  h.addOnTime(3600000UL, on);
  TEST_ASSERT_LESS_THAN_INT(599, h.formatRow(r, sizeof(r)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_paths);
  RUN_TEST(test_parse_readings_name);
  RUN_TEST(test_readings_header_has_profile_serials_and_columns);
  RUN_TEST(test_readings_row_matches_header_columns);
  RUN_TEST(test_rh_column_is_gain_times_raw_plus_offset);
  RUN_TEST(test_event_row_and_quoting);
  RUN_TEST(test_retention);
  RUN_TEST(test_bus_event_limiter);
  RUN_TEST(test_guard_block_tracker);
  RUN_TEST(test_hourly_row);
  RUN_TEST(test_worst_case_rows_fit_the_sd_queue_line);
  return UNITY_END();
}
