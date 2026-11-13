// The graphs' figures (spec 5.5), plain logic: readings and hourly rows written by the firmware's
// own logger code read back into points; the hour (every row), day (a point a minute) and
// week/month (hourly) rings; the refill after a boot with new rows waiting; and the reply —
// valid JSON, the same in any piece size, breaks where rows are missing, the duty over the range.
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <string>
#include "wineador/history.h"
#include "wineador/logbook.h"
#include "wineador/hourly.h"
#include "wineador/uk_time.h"
#include "wineador/log_read.h"
#include "../json_check.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

static const int64_t T0 = 1794571200;   // 2026-11-13T12:00:00Z

static void iso(char* out, size_t n, int64_t t) {
  int y, m, d;
  civilFromDays(t / 86400, &y, &m, &d);
  const int64_t s = t % 86400;
  snprintf(out, n, "%04d-%02d-%02dT%02d:%02d:%02dZ", y, m, d, (int)(s / 3600), (int)(s / 60 % 60), (int)(s % 60));
}

// A readings row as the firmware writes it.
static std::string readingsRow(int64_t t, float meanC, bool cool, bool trusted = true, bool testBuild = false,
                               int excluded = -1) {
  char ts[24];
  iso(ts, sizeof(ts), t);
  ReadingsRow r;
  r.timestamp = ts;
  r.timeTrusted = trusted;
  for (int i = 0; i < kNumSensors; i++) {
    r.ch[i].read = true;
    r.ch[i].tempC = meanC + 0.1f * i;
    r.ch[i].rhPct = 62.0f + i;
    r.ch[i].rhRawPct = 63.0f + i;
    r.ch[i].flag = (i == excluded) ? "crc" : "ok";
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
  r.heaterDutyPct = 0;
  r.humid = false;
  char line[1024];
  const int k = formatReadingsRow(line, sizeof(line), r, testBuild);
  return std::string(line, (size_t)k);
}

static std::string hourlyRow(int64_t hourStart, bool trusted = true) {
  char ts[24];
  iso(ts, sizeof(ts), hourStart);
  HourlyStats h;
  h.begin(1, ts, trusted);
  bool healthy[kNumSensors] = {true, true, true, true, true, true};
  for (int k = 0; k < 4; k++) {
    float t[kNumSensors], rh[kNumSensors];
    for (int i = 0; i < kNumSensors; i++) { t[i] = 19.0f + 0.5f * k + 0.1f * i; rh[i] = 60.0f + k; }
    CabinetSummary c;
    c.cabinetOk = 4;
    c.meanC = 19.0f + 0.5f * k;
    c.meanRhPct = 60.0f + k;
    c.controlC = c.meanC;
    h.addSweep(healthy, t, rh, c, 40, 1200);
  }
  LoadOutputs on;
  on.cool = true;
  h.addOnTime(900000, on);    // 15 min of compressor
  on.cool = false;
  on.humid = true;
  h.addOnTime(360000, on);    // 6 min of humidifier
  char line[1500];
  const int k = h.formatRow(line, sizeof(line));
  return std::string(line, (size_t)k);
}

// ---------------------------------------------------------------- rows -> points
void test_readings_rows_read_back() {
  const std::string row = readingsRow(T0, 19.80f, true);
  HistPoint p;
  TEST_ASSERT_TRUE(parseReadingsLine(row.data(), row.size(), &p));
  TEST_ASSERT_EQUAL_INT64(T0, p.t);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 19.80f, p.v[kHCabT]);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 19.50f, p.v[kHCabTMin]);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.20f, p.v[kHCabTMax]);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 61.50f, p.v[kHCabRh]);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.10f, p.v[kHCh3T]);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 67.0f, p.v[kHCh5Rh]);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, p.v[kHCompressor]);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, p.v[kHHeater]);
  TEST_ASSERT_EQUAL_FLOAT(40.0f, p.v[kHFan]);
  // a test build's extra *_real columns don't matter
  const std::string t = readingsRow(T0, 19.80f, false, true, true);
  TEST_ASSERT_TRUE(parseReadingsLine(t.data(), t.size(), &p));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, p.v[kHCompressor]);
  // an excluded channel's values are left out
  const std::string x = readingsRow(T0, 19.80f, false, true, false, 2);
  TEST_ASSERT_TRUE(parseReadingsLine(x.data(), x.size(), &p));
  TEST_ASSERT_TRUE(isnan(p.v[kHCh2T]) && isnan(p.v[kHCh2Rh]));
  TEST_ASSERT_FALSE(isnan(p.v[kHCh1T]));
  // untrusted times can't be placed: left out; header and comments too
  const std::string u = readingsRow(T0, 19.80f, false, false);
  TEST_ASSERT_FALSE(parseReadingsLine(u.data(), u.size(), &p));
  char head[3000];
  LogHeaderInfo hi;
  const int hn = formatReadingsHeader(head, sizeof(head), hi);
  const char* last = head;
  for (const char* c = head; c < head + hn - 1; c++)
    if (*c == '\n') last = c + 1;
  TEST_ASSERT_FALSE(parseReadingsLine(last, strlen(last), &p));        // the column header
  TEST_ASSERT_FALSE(parseReadingsLine(head, strchr(head, '\n') - head, &p));   // "# wineador readings"
}

void test_the_readings_columns_are_where_the_parser_looks() {
  // guards the column numbers in history.cpp against a change to the logger's columns
  char head[3000];
  LogHeaderInfo hi;
  formatReadingsHeader(head, sizeof(head), hi);
  char* last = head;
  for (char* c = head; *c; c++)
    if (*c == '\n' && c[1]) last = c + 1;
  char* f[80];
  const int k = csvSplit(last, f, 80);
  TEST_ASSERT_TRUE(k >= 36);
  TEST_ASSERT_EQUAL_STRING("ch0_t", f[2]);
  TEST_ASSERT_EQUAL_STRING("ch0_flag", f[5]);
  TEST_ASSERT_EQUAL_STRING("ch5_rh", f[23]);
  TEST_ASSERT_EQUAL_STRING("cab_mean_t", f[26]);
  TEST_ASSERT_EQUAL_STRING("cab_min_t", f[27]);
  TEST_ASSERT_EQUAL_STRING("cab_max_t", f[28]);
  TEST_ASSERT_EQUAL_STRING("cab_mean_rh", f[29]);
  TEST_ASSERT_EQUAL_STRING("fan_pwm", f[31]);
  TEST_ASSERT_EQUAL_STRING("compressor", f[33]);
  TEST_ASSERT_EQUAL_STRING("heater_duty", f[34]);
  TEST_ASSERT_EQUAL_STRING("humidifier", f[35]);
  formatHourlyHeader(head, sizeof(head), hi);
  for (char* c = head; *c; c++)
    if (*c == '\n' && c[1]) last = c + 1;
  const int h = csvSplit(last, f, 80);
  TEST_ASSERT_TRUE(h >= 53);
  TEST_ASSERT_EQUAL_STRING("ch0_t_min", f[3]);
  TEST_ASSERT_EQUAL_STRING("ch0_t_max", f[4]);
  TEST_ASSERT_EQUAL_STRING("ch0_t_mean", f[5]);
  TEST_ASSERT_EQUAL_STRING("ch0_rh_mean", f[8]);
  TEST_ASSERT_EQUAL_STRING("ch5_rh_mean", f[38]);
  TEST_ASSERT_EQUAL_STRING("cab_mean_t_mean", f[41]);
  TEST_ASSERT_EQUAL_STRING("cab_mean_rh_mean", f[44]);
  TEST_ASSERT_EQUAL_STRING("fan_pwm_mean", f[48]);
  TEST_ASSERT_EQUAL_STRING("compressor_on_s", f[50]);
  TEST_ASSERT_EQUAL_STRING("heater_on_s", f[51]);
  TEST_ASSERT_EQUAL_STRING("humidifier_on_s", f[52]);
}

void test_hourly_rows_read_back() {
  const std::string row = hourlyRow(T0);
  HistPoint p;
  TEST_ASSERT_TRUE(parseHourlyLine(row.data(), row.size(), &p));
  TEST_ASSERT_EQUAL_INT64(T0, p.t);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 19.75f, p.v[kHCabT]);        // mean of 19.0 .. 20.5
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 19.0f, p.v[kHCabTMin]);      // coldest cabinet sensor (ch0) over the hour
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.8f, p.v[kHCabTMax]);      // warmest cabinet sensor (ch3)
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 61.5f, p.v[kHCabRh]);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.25f, p.v[kHCh5T]);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.25f, p.v[kHCompressor]);  // 900 s of 3600
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.10f, p.v[kHHumidifier]);  // 360 s
  TEST_ASSERT_EQUAL_FLOAT(0.0f, p.v[kHHeater]);
  const std::string u = hourlyRow(T0, false);
  TEST_ASSERT_FALSE(parseHourlyLine(u.data(), u.size(), &p));
}

// ---------------------------------------------------------------- the rings
static HistPoint gRaw[400], gDay[1440], gHourly[744];
static History gH, gEmpty;

// The history every test starts from: empty, with the board's ring sizes.
static History& fresh() {
  History::Storage s;
  s.raw = gRaw; s.rawCap = 400;
  s.day = gDay; s.dayCap = 1440;
  s.hourly = gHourly; s.hourlyCap = 744;
  gH.begin(s);
  return gH;
}

static void addRow(History& h, int64_t t, float c, bool cool) {
  const std::string r = readingsRow(t, c, cool);
  TEST_ASSERT_TRUE(h.addReadingsLine(r.data(), r.size()));
}

void test_storage_fits_the_psram_budget() {
  // the plan's figure: ~250-300 KB of PSRAM for Q5; the history is the largest part
  const size_t bytes = sizeof(HistPoint) * (400 + 1440 + 744);
  TEST_ASSERT_TRUE(bytes < 240 * 1024);
  printf("history storage: %u bytes (%u per point)\n", (unsigned)bytes, (unsigned)sizeof(HistPoint));
}

void test_minute_points_and_time_order() {
  History& h = fresh();
  // four 15 s rows in one minute, then the next minute's first row closes it
  addRow(h, T0 + 0, 19.0f, true);
  addRow(h, T0 + 15, 19.2f, true);
  addRow(h, T0 + 30, 19.4f, false);
  addRow(h, T0 + 45, 19.6f, false);
  addRow(h, T0 + 30, 25.0f, false);      // out of order: ignored
  TEST_ASSERT_EQUAL_INT(4, h.count(History::Ring::Raw));
  TEST_ASSERT_EQUAL_INT(0, h.count(History::Ring::Day));
  addRow(h, T0 + 60, 19.8f, false);
  TEST_ASSERT_EQUAL_INT(1, h.count(History::Ring::Day));
  const HistPoint& m = h.at(History::Ring::Day, 0);
  TEST_ASSERT_EQUAL_INT64(T0, m.t);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 19.3f, m.v[kHCabT]);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 18.7f, m.v[kHCabTMin]);   // the lowest of the minute
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, m.v[kHCabTMax]);   // the highest
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, m.v[kHCompressor]); // on for 2 of the 4 rows
  // the raw ring keeps the newest 400
  for (int i = 0; i < 500; i++) addRow(h, T0 + 120 + i * 15, 19.0f, false);
  TEST_ASSERT_EQUAL_INT(400, h.count(History::Ring::Raw));
  TEST_ASSERT_EQUAL_INT64(T0 + 120 + 499 * 15, h.at(History::Ring::Raw, 399).t);
}

void test_the_refill_after_a_boot() {
  History& h = fresh();
  // the loop has already logged two rows when the SD task starts the refill
  addRow(h, T0 + 3600, 20.0f, false);
  addRow(h, T0 + 3615, 20.1f, false);
  h.beginRefill(History::Kind::Readings);
  TEST_ASSERT_TRUE(h.refilling(History::Kind::Readings));
  TEST_ASSERT_EQUAL_INT(0, h.count(History::Ring::Raw));
  // the card's rows go in oldest first, while a new row arrives and waits
  for (int i = 0; i < 60; i++) {
    const std::string r = readingsRow(T0 + i * 60, 19.0f, false);
    TEST_ASSERT_TRUE(h.refillLine(History::Kind::Readings, r.data(), r.size()));
    if (i == 30) addRow(h, T0 + 3630, 20.2f, false);
  }
  const std::string dup = readingsRow(T0 + 3600, 20.0f, false);   // the card also has the boot's first row
  h.refillLine(History::Kind::Readings, dup.data(), dup.size());
  h.endRefill(History::Kind::Readings);
  TEST_ASSERT_FALSE(h.refilling(History::Kind::Readings));
  const int n = h.count(History::Ring::Raw);
  TEST_ASSERT_EQUAL_INT(63, n);   // 60 + the boot's row (from the card; the waiting copy is a repeat) + 2 waiting
  for (int i = 1; i < n; i++) TEST_ASSERT_TRUE(h.at(History::Ring::Raw, i).t > h.at(History::Ring::Raw, i - 1).t);
  TEST_ASSERT_EQUAL_INT64(T0 + 3630, h.at(History::Ring::Raw, n - 1).t);
  TEST_ASSERT_EQUAL_UINT32(0, h.waitingDropped());
  // a header line or a comment in the refill is simply not a row
  TEST_ASSERT_FALSE(h.refillLine(History::Kind::Readings, "# wineador readings", 19));
}

void test_rows_waiting_during_a_long_refill_drop_the_oldest() {
  History& h = fresh();
  h.beginRefill(History::Kind::Readings);
  for (int i = 0; i < 40; i++) addRow(h, T0 + i * 15, 20.0f, false);
  TEST_ASSERT_EQUAL_UINT32(8, h.waitingDropped());
  h.endRefill(History::Kind::Readings);
  TEST_ASSERT_EQUAL_INT(32, h.count(History::Ring::Raw));
  TEST_ASSERT_EQUAL_INT64(T0 + 8 * 15, h.at(History::Ring::Raw, 0).t);
}

void test_hourly_refill() {
  History& h = fresh();
  const std::string live = hourlyRow(T0 + 7200);
  TEST_ASSERT_TRUE(h.addHourlyLine(live.data(), live.size()));
  h.beginRefill(History::Kind::Hourly);
  for (int i = 0; i < 2; i++) {
    const std::string r = hourlyRow(T0 + i * 3600);
    TEST_ASSERT_TRUE(h.refillLine(History::Kind::Hourly, r.data(), r.size()));
  }
  h.endRefill(History::Kind::Hourly);
  TEST_ASSERT_EQUAL_INT(3, h.count(History::Ring::Hourly));
  TEST_ASSERT_EQUAL_INT64(T0 + 7200, h.at(History::Ring::Hourly, 2).t);
}

// ---------------------------------------------------------------- the reply
static std::string reply(History& h, HistRange r, int64_t now, int64_t since, size_t piece = 0) {
  HistoryReply p(h, r, now, since);
  return piece ? jsoncheck::inPieces(p, piece) : jsoncheck::whole(p);
}

void test_reply_is_the_same_in_any_piece_size() {
  History& h = fresh();
  for (int i = 0; i < 200; i++) addRow(h, T0 + i * 15, 19.0f + 0.01f * i, (i / 20) % 2 == 0);
  const int64_t now = T0 + 200 * 15;
  const std::string whole = reply(h, HistRange::Hour, now, 0);
  TEST_ASSERT_TRUE(jsoncheck::valid(whole));
  HistoryReply probe(h, HistRange::Hour, now, 0);
  for (size_t piece : {probe.maxStep() + 1, (size_t)257, (size_t)1000, (size_t)4096}) {
    TEST_ASSERT_EQUAL_STRING(whole.c_str(), reply(h, HistRange::Hour, now, 0, piece).c_str());
  }
  // one value per point in every column
  TEST_ASSERT_EQUAL_INT(200, probe.points());
  TEST_ASSERT_EQUAL_INT(kHistFields + 1, jsoncheck::count(whole, "\":["));   // t and the fields, nothing else
  TEST_ASSERT_TRUE(whole.find("\"range\":\"hour\",\"step_s\":15") != std::string::npos);
  printf("hour reply: %u bytes for 200 points\n", (unsigned)whole.size());
}

// Columns of one reply, each as its list of values.
static int columnLength(const std::string& j, const char* key) {
  const std::string k = std::string("\"") + key + "\":[";
  const size_t at = j.find(k);
  if (at == std::string::npos) return -1;
  const size_t end = j.find(']', at);
  const std::string col = j.substr(at + k.size(), end - at - k.size());
  return col.empty() ? 0 : jsoncheck::count(col, ",") + 1;
}

void test_rows_arriving_mid_reply_wait_until_it_is_sent() {
  History& h = fresh();
  // full rings: raw (400 rows), and the day ring a day of minutes
  int64_t t = T0;
  for (int i = 0; i < 1500; i++, t += 60) addRow(h, t, 19.0f + 0.001f * i, i % 7 == 0);
  TEST_ASSERT_EQUAL_INT(400, h.count(History::Ring::Raw));
  for (HistRange r : {HistRange::Hour, HistRange::Day}) {
    std::string before;
    {
      HistoryReply whole(h, r, t, 0);
      before = jsoncheck::whole(whole);
    }
    std::string pieces;
    {
      HistoryReply p(h, r, t, 0);
      char buf[300];
      TEST_ASSERT_TRUE(h.replyOpen());
      TEST_ASSERT_FALSE(h.beginRefill(History::Kind::Readings));   // no refill under an open reply
      int k = 0;
      while (!p.done()) {
        const size_t n = p.fill(buf, sizeof(buf));
        pieces.append(buf, n);
        if (++k % 3 == 0) addRow(h, t + k * 60, 30.0f, true);     // rows keep arriving meanwhile
      }
    }
    TEST_ASSERT_EQUAL_STRING(before.c_str(), pieces.c_str());     // nothing moved under it
    TEST_ASSERT_TRUE(jsoncheck::valid(pieces));
    const int tl = columnLength(pieces, "t");
    for (int f = 0; f < kHistFields; f++) TEST_ASSERT_EQUAL_INT(tl, columnLength(pieces, histFieldName(f)));
    // once the reply has gone, the waiting rows are in
    TEST_ASSERT_FALSE(h.replyOpen());
    TEST_ASSERT_TRUE(h.at(History::Ring::Raw, h.count(History::Ring::Raw) - 1).t > t);
    t = h.at(History::Ring::Raw, h.count(History::Ring::Raw) - 1).t + 60;
  }
  TEST_ASSERT_TRUE(h.beginRefill(History::Kind::Readings));
}

void test_since_keeps_a_break_and_the_whole_range_duty() {
  History& h = fresh();
  for (int i = 0; i < 10; i++) addRow(h, T0 + i * 60, 19.0f, true);           // on
  for (int i = 0; i < 10; i++) addRow(h, T0 + 1800 + i * 60, 19.0f, false);   // off, after a 21-minute hole
  const int64_t now = T0 + 2400;
  // the page has everything up to T0 + 540; the first new point is across the hole
  const std::string s = reply(h, HistRange::Hour, now, T0 + 540);
  TEST_ASSERT_TRUE(jsoncheck::valid(s));
  TEST_ASSERT_EQUAL_INT(11, columnLength(s, "t"));        // the break, then 10 points
  TEST_ASSERT_TRUE(s.find("\"cab_t\":[null,") != std::string::npos);
  // the duty is over the whole hour, not only the new points (which were all off)
  const std::string all = reply(h, HistRange::Hour, now, 0);
  const size_t a = all.find("\"duty\":"), b = s.find("\"duty\":");
  TEST_ASSERT_EQUAL_STRING(all.substr(a, 60).c_str(), s.substr(b, 60).c_str());
  TEST_ASSERT_TRUE(s.find("\"compressor\":0,") == std::string::npos);
}

void test_reply_breaks_at_a_gap_and_takes_since() {
  History& h = fresh();
  for (int i = 0; i < 10; i++) addRow(h, T0 + i * 60, 19.0f, false);
  for (int i = 0; i < 10; i++) addRow(h, T0 + 3000 + i * 60, 19.0f, false);   // a 40-minute hole (a power cut)
  const int64_t now = T0 + 3540;
  const std::string w = reply(h, HistRange::Hour, now, 0);
  TEST_ASSERT_TRUE(jsoncheck::valid(w));
  // every column has 21 entries: 20 points and one break
  const size_t tAt = w.find("\"t\":[");
  const std::string tcol = w.substr(tAt, w.find(']', tAt) - tAt);
  TEST_ASSERT_EQUAL_INT(20, jsoncheck::count(tcol, ","));                     // 21 values
  const size_t cAt = w.find("\"cab_t\":[");
  const std::string ccol = w.substr(cAt, w.find(']', cAt) - cAt);
  TEST_ASSERT_EQUAL_INT(1, jsoncheck::count(ccol, "null"));
  // since: only the newest points (the hour view adds its newest points every 30 s)
  HistoryReply s(h, HistRange::Hour, now, T0 + 3000 + 7 * 60);
  TEST_ASSERT_EQUAL_INT(2, s.points());
  // the day view leaves out what is older than a day
  HistoryReply d(h, HistRange::Day, T0 + 86400 + 3600, 0);
  TEST_ASSERT_EQUAL_INT(0, d.points());
}

void test_duty_over_the_range() {
  History& h = fresh();
  // the compressor runs with 15 s rows for 10 min, then stops: 60 s rows for 30 min
  int64_t t = T0;
  for (int i = 0; i < 40; i++, t += 15) addRow(h, t, 20.0f, true);
  for (int i = 0; i < 30; i++, t += 60) addRow(h, t, 19.5f, false);
  // by time: 10 min on of 40 (not 40 rows of 70)
  const double duty = histDuty(h, History::Ring::Raw, 0, kHCompressor, t);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 0.25, duty);
  const std::string w = reply(h, HistRange::Hour, t, 0);
  TEST_ASSERT_TRUE(w.find("\"duty\":{\"compressor\":0.25") != std::string::npos);
  // the day view's minute points count equally: 10 on of the 40 closed minutes
  const double dd = histDuty(h, History::Ring::Day, 0, kHCompressor, t);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 10.0 / 39.0, dd);
  // nothing in range: null
  gEmpty.begin(History::Storage{});
  TEST_ASSERT_TRUE(reply(gEmpty, HistRange::Week, T0, 0).find("\"compressor\":null") != std::string::npos);
}

void test_week_and_month_from_hourly_rows() {
  History& h = fresh();
  for (int i = 0; i < 800; i++) {
    const std::string r = hourlyRow(T0 + (int64_t)i * 3600);
    h.addHourlyLine(r.data(), r.size());
  }
  TEST_ASSERT_EQUAL_INT(744, h.count(History::Ring::Hourly));
  const int64_t now = T0 + 800 * 3600;
  HistoryReply w(h, HistRange::Week, now, 0), m(h, HistRange::Month, now, 0);
  TEST_ASSERT_EQUAL_INT(167, w.points());   // the hours strictly inside the last 168
  TEST_ASSERT_EQUAL_INT(743, m.points());
  const std::string mj = reply(h, HistRange::Month, now, 0);
  TEST_ASSERT_TRUE(jsoncheck::valid(mj));
  TEST_ASSERT_TRUE(mj.find("\"duty\":{\"compressor\":0.25,\"heater\":0,\"humidifier\":0.1}") != std::string::npos);
  printf("month reply: %u bytes\n", (unsigned)mj.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_readings_rows_read_back);
  RUN_TEST(test_the_readings_columns_are_where_the_parser_looks);
  RUN_TEST(test_hourly_rows_read_back);
  RUN_TEST(test_storage_fits_the_psram_budget);
  RUN_TEST(test_minute_points_and_time_order);
  RUN_TEST(test_the_refill_after_a_boot);
  RUN_TEST(test_rows_waiting_during_a_long_refill_drop_the_oldest);
  RUN_TEST(test_hourly_refill);
  RUN_TEST(test_reply_is_the_same_in_any_piece_size);
  RUN_TEST(test_rows_arriving_mid_reply_wait_until_it_is_sent);
  RUN_TEST(test_since_keeps_a_break_and_the_whole_range_duty);
  RUN_TEST(test_reply_breaks_at_a_gap_and_takes_since);
  RUN_TEST(test_duty_over_the_range);
  RUN_TEST(test_week_and_month_from_hourly_rows);
  return UNITY_END();
}
