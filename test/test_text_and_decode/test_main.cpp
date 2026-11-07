// Status line (B3: profile, test build, boot reason, uptime, hold on every line),
// the live block, the command reader, the sensor table and the byte decoders.
#include <unity.h>
#include <string.h>
#include "wineador/status.h"
#include "wineador/commands.h"
#include "wineador/decode.h"
#include "wineador/sensor_table.h"
#include "wineador/summary.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

namespace {
bool has(const char* s, const char* part) { return strstr(s, part) != nullptr; }
StatusInfo sample() {
  StatusInfo s;
  s.timestamp = "2026-11-07T13:00:15Z";
  s.uptimeMs = 90500;
  s.bootReason = "POWERON";
  s.holdActive = true;
  s.holdRemainingMs = 12000;
  s.holdLengthMs = 30000;
  s.fanDutyPct = 20;
  s.fanRpm = 540;
  return s;
}
}

// ---------------------------------------------------------------- status line
void test_duration() {
  char b[24];
  TEST_ASSERT_EQUAL_STRING("0:00:00", formatDuration(b, sizeof(b), 0));
  TEST_ASSERT_EQUAL_STRING("0:01:30", formatDuration(b, sizeof(b), 90999));
  TEST_ASSERT_EQUAL_STRING("27:46:40", formatDuration(b, sizeof(b), 100000000ULL));
  TEST_ASSERT_EQUAL_STRING("1193:03:20", formatDuration(b, sizeof(b), 4295000000ULL));  // past the millis wrap
}

void test_status_line_real_cabinet_build() {
  StatusInfo s = sample();
  char b[400];
  formatStatusLine(b, sizeof(b), s);
  TEST_ASSERT_TRUE(has(b, "2026-11-07T13:00:15Z"));
  TEST_ASSERT_TRUE(has(b, "up 0:01:30"));
  TEST_ASSERT_TRUE(has(b, "| REAL |"));
  TEST_ASSERT_FALSE(has(b, "BENCH"));
  TEST_ASSERT_FALSE(has(b, "TEST-BUILD"));
  TEST_ASSERT_TRUE(has(b, "boot POWERON"));
  TEST_ASSERT_TRUE(has(b, "hold 0:00:12 left"));
  TEST_ASSERT_TRUE(has(b, "fan 20% 540rpm"));
  TEST_ASSERT_TRUE(has(b, "raw"));
}

void test_status_line_bench_test_build() {
  StatusInfo s = sample();
  s.profile = Profile::Bench;
  s.testBuild = true;
  s.bootReason = "TASK_WATCHDOG";
  s.holdActive = false;
  char b[400];
  formatStatusLine(b, sizeof(b), s);
  TEST_ASSERT_TRUE(has(b, "BENCH-PROFILE TEST-BUILD"));
  TEST_ASSERT_TRUE(has(b, "boot TASK_WATCHDOG"));
  TEST_ASSERT_TRUE(has(b, "hold ended at 0:00:30"));
  TEST_ASSERT_TRUE(has(b, "scripted"));
}

void test_status_line_shows_loads_and_sensors() {
  StatusInfo s = sample();
  s.loads.relayHeating = true; s.loads.heat = true;
  Readings r;
  for (int i = 0; i < kNumSensors; i++) { r.s[i].bus = BusResult::Ok; r.s[i].tempC = 20.0f + i; r.s[i].rhRawPct = 60.0f; }
  r.s[3].bus = BusResult::NoAck;
  s.cab = summarise(r);
  char b[400];
  formatStatusLine(b, sizeof(b), s);
  TEST_ASSERT_TRUE(has(b, "relay HEAT cool off heat ON humid off"));
  TEST_ASSERT_TRUE(has(b, "cab 21.00C 60.0% (3/4, 20.00-22.00)"));
  TEST_ASSERT_TRUE(has(b, "ch6 24.00C"));
  TEST_ASSERT_TRUE(has(b, "sensors 5/6"));
}

void test_status_line_never_overruns() {
  StatusInfo s = sample();
  char b[40];
  memset(b, 'x', sizeof(b));
  formatStatusLine(b, 30, s);
  TEST_ASSERT_EQUAL_CHAR('x', b[30]);     // nothing written past the limit
  TEST_ASSERT_EQUAL_CHAR(0, b[29]);
}

void test_live_block() {
  char b[600];
  LoadOutputs l;
  formatLiveBlock(b, sizeof(b), 1000, "fan kickstart finished", l, 20);
  TEST_ASSERT_TRUE(has(b, "NOW LIVE  (up 0:00:01)  fan kickstart finished"));
  TEST_ASSERT_TRUE(has(b, "RELAY K1 ........ OFF (cooling side)"));
  TEST_ASSERT_TRUE(has(b, "HEATER .......... OFF"));
  TEST_ASSERT_TRUE(has(b, "PLENUM FAN ...... 20%"));
}

// ---------------------------------------------------------------- summary
void test_summary_cabinet_is_ch2_to_ch5_only() {
  Readings r;
  r.s[0] = {BusResult::Ok, 19.0f, 60.0f};
  r.s[1] = {BusResult::Ok, 21.0f, 64.0f};
  r.s[4] = {BusResult::Ok, 5.0f, 99.0f};    // chamber: never in the cabinet mean
  r.s[5] = {BusResult::Ok, 30.0f, 40.0f};   // ambient: never in the cabinet mean
  CabinetSummary c = summarise(r);
  TEST_ASSERT_EQUAL_INT(2, c.cabinetOk);
  TEST_ASSERT_EQUAL_INT(4, c.sensorsOk);
  TEST_ASSERT_EQUAL_FLOAT(20.0f, c.meanC);
  TEST_ASSERT_EQUAL_FLOAT(62.0f, c.meanRhPct);
  TEST_ASSERT_EQUAL_FLOAT(19.0f, c.minC);
  TEST_ASSERT_EQUAL_FLOAT(21.0f, c.maxC);
  TEST_ASSERT_TRUE(c.chamberOk);
  TEST_ASSERT_TRUE(c.ambientOk);
}

// ---------------------------------------------------------------- commands
void test_line_reader_and_commands() {
  LineReader lr;
  const char* in = "BOOT\r\n";
  int lines = 0;
  for (const char* p = in; *p; p++) if (lr.feed(*p)) lines++;
  TEST_ASSERT_EQUAL_INT(1, lines);   // \r\n counts once
  TEST_ASSERT_TRUE(parseCommand(lr.line(), false) == Command::Boot);

  TEST_ASSERT_TRUE(parseCommand("  help ", false) == Command::Help);
  TEST_ASSERT_TRUE(parseCommand("stall", true) == Command::Stall);
  TEST_ASSERT_TRUE(parseCommand("stall", false) == Command::StallNotAllowed);   // never in a cabinet build
  TEST_ASSERT_TRUE(parseCommand("boot now", false) == Command::Unknown);
  TEST_ASSERT_TRUE(parseCommand("xyz", false) == Command::Unknown);
  TEST_ASSERT_TRUE(parseCommand("", false) == Command::None);
}

void test_line_reader_overflow() {
  LineReader lr;
  for (int i = 0; i < 100; i++) lr.feed('s');
  TEST_ASSERT_TRUE(lr.feed('\n'));
  TEST_ASSERT_TRUE(parseCommand(lr.line(), true) == Command::Unknown);
  for (const char* p = "boot\n"; *p; p++) lr.feed(*p);   // recovers afterwards
  TEST_ASSERT_TRUE(parseCommand(lr.line(), true) == Command::Boot);
}

// ---------------------------------------------------------------- decode
void test_sht40_crc_datasheet_example() {
  const uint8_t d[2] = {0xBE, 0xEF};
  TEST_ASSERT_EQUAL_HEX8(0x92, sht40Crc(d, 2));
}

void test_sht40_measurement() {
  // T word 0x6666 -> -45 + 175 x 26214/65535 = 25.0 C; RH word 0x8000 -> 56.5%
  uint8_t b[6] = {0x66, 0x66, 0, 0x80, 0x00, 0};
  b[2] = sht40Crc(b, 2);
  b[5] = sht40Crc(b + 3, 2);
  float t, rh;
  TEST_ASSERT_TRUE(decodeSht40Measurement(b, t, rh) == BusResult::Ok);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, t);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 56.5f, rh);
  b[5] ^= 1;
  TEST_ASSERT_TRUE(decodeSht40Measurement(b, t, rh) == BusResult::CrcFail);
}

void test_sht40_rh_not_clamped() {
  // RH word 0xFFFF -> 119% raw. Must stay raw so the 4.3 plausibility check sees it.
  uint8_t b[6] = {0x66, 0x66, 0, 0xFF, 0xFF, 0};
  b[2] = sht40Crc(b, 2);
  b[5] = sht40Crc(b + 3, 2);
  float t, rh;
  decodeSht40Measurement(b, t, rh);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 119.0f, rh);
}

void test_sht40_serial() {
  uint8_t b[6] = {0x0A, 0xD4, 0, 0x09, 0xED, 0};
  b[2] = sht40Crc(b, 2);
  b[5] = sht40Crc(b + 3, 2);
  uint32_t sn = 0;
  TEST_ASSERT_TRUE(decodeSht40Serial(b, sn) == BusResult::Ok);
  TEST_ASSERT_EQUAL_HEX32(0x0AD409ED, sn);
  TEST_ASSERT_EQUAL_INT(0, sensorIndexForSerial(sn));
}

void test_sensor_table() {
  for (int i = 0; i < kNumSensors; i++) {
    TEST_ASSERT_EQUAL_UINT8(i + 2, kSensors[i].channel);
    TEST_ASSERT_EQUAL_INT(i, sensorIndexForSerial(kSensors[i].serial));   // serials unique
  }
  TEST_ASSERT_EQUAL_INT(-1, sensorIndexForSerial(0x12345678));
  TEST_ASSERT_EQUAL_FLOAT(1.0274f, kSensors[2].rhGain);    // ch4, build log
  TEST_ASSERT_EQUAL_FLOAT(-3.878f, kSensors[2].rhOffset);
}

void test_ds3231_time() {
  const uint8_t r[7] = {0x15, 0x00, 0x13, 0x06, 0x07, 0x11, 0x26};
  char b[32];
  TEST_ASSERT_TRUE(formatDs3231Time(r, b, sizeof(b)));
  TEST_ASSERT_EQUAL_STRING("2026-11-07T13:00:15Z", b);
  const uint8_t bad[7] = {0x15, 0x00, 0x13, 0x06, 0x07, 0x13, 0x26};   // month 13
  TEST_ASSERT_FALSE(formatDs3231Time(bad, b, sizeof(b)));
}

void test_unix_time() {
  DateTime a; a.year = 2000;
  TEST_ASSERT_EQUAL_INT64(946684800LL, toUnixSeconds(a));
  DateTime b; b.year = 2026; b.month = 11; b.day = 7; b.hour = 13; b.minute = 0; b.second = 15;
  TEST_ASSERT_EQUAL_INT64(1794056415LL, toUnixSeconds(b));
  DateTime c; c.year = 2028; c.month = 2; c.day = 29; c.hour = 23; c.minute = 59; c.second = 59;   // leap day
  TEST_ASSERT_EQUAL_INT64(1835481599LL, toUnixSeconds(c));
}

void test_status_warnings() {
  StatusInfo s = sample();
  char b[400];
  formatStatusLine(b, sizeof(b), s);
  TEST_ASSERT_FALSE(has(b, "RTC FAIL"));
  TEST_ASSERT_FALSE(has(b, "PULLUPS"));
  s.rtcOk = false; s.i2cPullupsOn = true;
  formatStatusLine(b, sizeof(b), s);
  TEST_ASSERT_TRUE(has(b, "RTC FAIL"));
  TEST_ASSERT_TRUE(has(b, "I2C PULLUPS ON"));
}

void test_status_stale_readings() {
  StatusInfo s = sample();
  char b[400];
  s.readingsAgeMs = 30000; s.staleAfterMs = 30000;   // exactly two read periods: not yet stale
  formatStatusLine(b, sizeof(b), s);
  TEST_ASSERT_FALSE(has(b, "STALE"));
  s.readingsAgeMs = 45000;
  formatStatusLine(b, sizeof(b), s);
  TEST_ASSERT_TRUE(has(b, "readings STALE 45s"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_duration);
  RUN_TEST(test_status_line_real_cabinet_build);
  RUN_TEST(test_status_line_bench_test_build);
  RUN_TEST(test_status_line_shows_loads_and_sensors);
  RUN_TEST(test_status_line_never_overruns);
  RUN_TEST(test_live_block);
  RUN_TEST(test_summary_cabinet_is_ch2_to_ch5_only);
  RUN_TEST(test_line_reader_and_commands);
  RUN_TEST(test_line_reader_overflow);
  RUN_TEST(test_sht40_crc_datasheet_example);
  RUN_TEST(test_sht40_measurement);
  RUN_TEST(test_sht40_rh_not_clamped);
  RUN_TEST(test_sht40_serial);
  RUN_TEST(test_sensor_table);
  RUN_TEST(test_ds3231_time);
  RUN_TEST(test_unix_time);
  RUN_TEST(test_status_warnings);
  RUN_TEST(test_status_stale_readings);
  return UNITY_END();
}
