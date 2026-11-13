// Status line (B3: profile, test build, boot reason, uptime, hold on every line),
// the live block, the command reader, the sensor table and the byte decoders.
#include <unity.h>
#include <string.h>
#include "wineador/status.h"
#include "wineador/commands.h"
#include "wineador/decode.h"
#include "wineador/sensor_table.h"
#include "wineador/summary.h"
#include "wineador/reset_log.h"
#include "wineador/clock_state.h"
#include "wineador/decode.h"

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
  TEST_ASSERT_TRUE(has(b, "| real"));          // real readings, RH calibrated (stage 3)
  TEST_ASSERT_FALSE(has(b, "UNTRUSTED"));
}

void test_status_line_clock_health_and_net() {
  StatusInfo s = sample();
  s.timeTrusted = false;
  s.rtcLostTime = true;
  s.healthText = "| excl ch4 absent | sensor hold 12s";
  s.netText = "wifi up ntp waiting";
  char b[600];
  formatStatusLine(b, sizeof(b), s);
  TEST_ASSERT_TRUE(has(b, "2026-11-07T13:00:15Z UNTRUSTED up 0:01:30"));
  TEST_ASSERT_TRUE(has(b, "| RTC LOST TIME"));
  TEST_ASSERT_TRUE(has(b, "/6 | excl ch4 absent | sensor hold 12s"));
  TEST_ASSERT_TRUE(has(b, "| wifi up ntp waiting"));
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
  TEST_ASSERT_TRUE(has(b, "ch4 24.00C"));
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
void test_summary_cabinet_is_ch0_to_ch3_only() {
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

void test_stage2_commands() {
  int arg = 0;
  TEST_ASSERT_TRUE(parseCommand("exercise", false) == Command::Exercise);   // every build
  TEST_ASSERT_TRUE(parseCommand("EXERCISE", true) == Command::Exercise);
  TEST_ASSERT_TRUE(parseCommand("run 2", true, &arg) == Command::Run);
  TEST_ASSERT_EQUAL_INT(2, arg);
  TEST_ASSERT_TRUE(parseCommand("  run   13 ", true, &arg) == Command::Run);
  TEST_ASSERT_EQUAL_INT(13, arg);
  TEST_ASSERT_TRUE(parseCommand("run", true, &arg) == Command::Run);
  TEST_ASSERT_EQUAL_INT(-1, arg);
  TEST_ASSERT_TRUE(parseCommand("run x", true, &arg) == Command::Run);
  TEST_ASSERT_EQUAL_INT(-1, arg);
  TEST_ASSERT_TRUE(parseCommand("run 2", false, &arg) == Command::TestOnly);   // never in a cabinet build
  TEST_ASSERT_TRUE(parseCommand("stop", true) == Command::Stop);
  TEST_ASSERT_TRUE(parseCommand("stop", false) == Command::TestOnly);
  TEST_ASSERT_TRUE(parseCommand("exercise now", false) == Command::Unknown);
}

void test_status_line_guard_text_and_boot_demand() {
  StatusInfo s;
  s.testBuild = true;
  s.bootDemand = true;
  s.guardText = "demand heat+humid | held: heat boot-hold 12s";
  char b[600];
  formatStatusLine(b, sizeof(b), s);
  TEST_ASSERT_TRUE(has(b, "TEST-BUILD BOOT-DEMAND"));
  TEST_ASSERT_TRUE(has(b, "| demand heat+humid | held: heat boot-hold 12s |"));
}

void test_status_fan_readback() {
  StatusInfo s;
  s.fanDutyPct = 20; s.fanRpm = 420; s.fanPwmReadback = 51; s.fanPulses = 70; s.fanEdges = 777;
  char b[600];
  formatStatusLine(b, sizeof(b), s);
  TEST_ASSERT_TRUE(has(b, "fan 20% 420rpm pwm-hw 51/255 e/p 11.1"));
  s.fanPwmReadback = -1;
  formatStatusLine(b, sizeof(b), s);
  TEST_ASSERT_FALSE(has(b, "pwm-hw"));
}

void test_resets_commands() {
  TEST_ASSERT_TRUE(parseCommand("resets", true) == Command::Resets);
  TEST_ASSERT_TRUE(parseCommand("Resets Clear", true) == Command::ResetsClear);
  TEST_ASSERT_TRUE(parseCommand("resets", false) == Command::TestOnly);
  TEST_ASSERT_TRUE(parseCommand("resets now", true) == Command::Unknown);
}

void test_fan_command() {
  int arg = 0;
  TEST_ASSERT_TRUE(parseCommand("fan 10", true, &arg) == Command::Fan);
  TEST_ASSERT_EQUAL_INT(10, arg);                  // parsed; the refusal is the control's job
  TEST_ASSERT_TRUE(parseCommand("FAN  0 ", true, &arg) == Command::Fan);
  TEST_ASSERT_EQUAL_INT(0, arg);
  TEST_ASSERT_TRUE(parseCommand("fan auto", true, &arg) == Command::Fan);
  TEST_ASSERT_EQUAL_INT(-1, arg);
  TEST_ASSERT_TRUE(parseCommand("fan", true, &arg) == Command::Fan);
  TEST_ASSERT_EQUAL_INT(-2, arg);
  TEST_ASSERT_TRUE(parseCommand("fan 5x", true, &arg) == Command::Fan);
  TEST_ASSERT_EQUAL_INT(-2, arg);
  TEST_ASSERT_TRUE(parseCommand("fan 50", false, &arg) == Command::TestOnly);   // never in a cabinet build
}

void test_humidcool_command() {
  int arg = 0;
  TEST_ASSERT_TRUE(parseCommand("humidcool on", true, &arg) == Command::HumidCool);
  TEST_ASSERT_EQUAL_INT(1, arg);
  TEST_ASSERT_TRUE(parseCommand("HUMIDCOOL Off", true, &arg) == Command::HumidCool);
  TEST_ASSERT_EQUAL_INT(0, arg);
  TEST_ASSERT_TRUE(parseCommand("humidcool", true, &arg) == Command::HumidCool);
  TEST_ASSERT_EQUAL_INT(-2, arg);
  TEST_ASSERT_TRUE(parseCommand("humidcool yes", true, &arg) == Command::HumidCool);
  TEST_ASSERT_EQUAL_INT(-2, arg);
  TEST_ASSERT_TRUE(parseCommand("humidcool on", false, &arg) == Command::TestOnly);   // never in a cabinet build
}

void test_status_line_humid_text() {
  StatusInfo s;
  s.humidText = "humid CALL | calls 1 runs 2";
  char buf[900];
  formatStatusLine(buf, sizeof(buf), s);
  TEST_ASSERT_NOT_NULL(strstr(buf, "| humid CALL | calls 1 runs 2"));
}

void test_stage6_serial_interface_commands() {
  CommandArgs a;
  TEST_ASSERT_TRUE(parseCommand("status", false, a) == Command::Status);
  TEST_ASSERT_TRUE(parseCommand("Sensors", false, a) == Command::Sensors);
  TEST_ASSERT_TRUE(parseCommand("log", false, a) == Command::Log);
  TEST_ASSERT_TRUE(parseCommand("settings", false, a) == Command::Settings);
  TEST_ASSERT_TRUE(parseCommand("events", false, a) == Command::Events);
  TEST_ASSERT_EQUAL_INT(-1, a.arg);
  TEST_ASSERT_TRUE(parseCommand("events 25", false, a) == Command::Events);
  TEST_ASSERT_EQUAL_INT(25, a.arg);
  TEST_ASSERT_TRUE(parseCommand("events lots", false, a) == Command::Events);
  TEST_ASSERT_EQUAL_INT(-2, a.arg);
  TEST_ASSERT_TRUE(parseCommand("set temp 19.5", false, a) == Command::SetTemp);
  TEST_ASSERT_EQUAL_FLOAT(19.5f, a.f);
  TEST_ASSERT_EQUAL_INT(-1, a.arg);
  TEST_ASSERT_TRUE(parseCommand("set temp warm", false, a) == Command::SetTemp);
  TEST_ASSERT_EQUAL_INT(-2, a.arg);
  TEST_ASSERT_TRUE(parseCommand("SET RH 63", false, a) == Command::SetRh);
  TEST_ASSERT_EQUAL_FLOAT(63.0f, a.f);
  TEST_ASSERT_TRUE(parseCommand("set heater off", false, a) == Command::SetHeater);
  TEST_ASSERT_EQUAL_INT(0, a.arg);
  TEST_ASSERT_TRUE(parseCommand("set humid on", false, a) == Command::SetHumid);
  TEST_ASSERT_EQUAL_INT(1, a.arg);
  TEST_ASSERT_TRUE(parseCommand("set humid maybe", false, a) == Command::SetHumid);
  TEST_ASSERT_EQUAL_INT(-2, a.arg);
  TEST_ASSERT_TRUE(parseCommand("set defaults", false, a) == Command::SetDefaults);
  TEST_ASSERT_TRUE(parseCommand("set colour blue", false, a) == Command::Unknown);
  TEST_ASSERT_TRUE(parseCommand("exclude ch1 on", false, a) == Command::Exclude);
  TEST_ASSERT_EQUAL_INT(1, a.arg);
  TEST_ASSERT_EQUAL_INT(1, a.arg2);
  TEST_ASSERT_TRUE(parseCommand("exclude 5 off", false, a) == Command::Exclude);
  TEST_ASSERT_EQUAL_INT(5, a.arg);
  TEST_ASSERT_EQUAL_INT(0, a.arg2);
  TEST_ASSERT_TRUE(parseCommand("exclude ch0 on", false, a) == Command::Exclude);
  TEST_ASSERT_EQUAL_INT(0, a.arg);                        // channels are 0-5 (spec 2.3)
  TEST_ASSERT_TRUE(parseCommand("exclude ch6 on", false, a) == Command::Exclude);
  TEST_ASSERT_EQUAL_INT(-2, a.arg);                       // no such channel (Rev N's old 2-7 numbering)
  TEST_ASSERT_TRUE(parseCommand("exclude ch8 on", false, a) == Command::Exclude);
  TEST_ASSERT_EQUAL_INT(-2, a.arg);                       // no such channel
  TEST_ASSERT_TRUE(parseCommand("exclude ch1", false, a) == Command::Exclude);
  TEST_ASSERT_EQUAL_INT(-2, a.arg);
  // test-build only
  TEST_ASSERT_TRUE(parseCommand("crc ch2", true, a) == Command::InjectCrc);
  TEST_ASSERT_EQUAL_INT(2, a.arg);
  TEST_ASSERT_TRUE(parseCommand("timeout 4", true, a) == Command::InjectTimeout);
  TEST_ASSERT_EQUAL_INT(4, a.arg);
  TEST_ASSERT_TRUE(parseCommand("crc ch2", false, a) == Command::TestOnly);
  TEST_ASSERT_TRUE(parseCommand("run soak", true, a) == Command::Run);
  TEST_ASSERT_EQUAL_INT(kSoakScenario, a.arg);
  TEST_ASSERT_TRUE(parseCommand("status now", false, a) == Command::Unknown);
  // the SD stress test
  TEST_ASSERT_TRUE(parseCommand("sdstress 4000 noretry 2000", true, a) == Command::SdStress);
  TEST_ASSERT_EQUAL_INT(4000, a.arg);
  TEST_ASSERT_EQUAL_INT(0, a.arg2);
  TEST_ASSERT_EQUAL_INT(2000, a.arg3);
  TEST_ASSERT_TRUE(parseCommand("sdstress 1000 retry 2500", true, a) == Command::SdStress);
  TEST_ASSERT_EQUAL_INT(1, a.arg2);
  TEST_ASSERT_TRUE(parseCommand("sdstress stop", true, a) == Command::SdStress);
  TEST_ASSERT_EQUAL_INT(-1, a.arg);
  TEST_ASSERT_TRUE(parseCommand("sdstress 100 retry 10", true, a) == Command::SdStress);
  TEST_ASSERT_EQUAL_INT(-2, a.arg);                       // below 400 kHz
  TEST_ASSERT_TRUE(parseCommand("sdstress 1000 maybe 10", true, a) == Command::SdStress);
  TEST_ASSERT_EQUAL_INT(-2, a.arg);
  TEST_ASSERT_TRUE(parseCommand("sdstress 1000 retry 10", false, a) == Command::TestOnly);
  // the NTP-only radio window, on demand
  TEST_ASSERT_TRUE(parseCommand("ntpsync", true, a) == Command::NtpSync);
  TEST_ASSERT_TRUE(parseCommand("ntpsync", false, a) == Command::TestOnly);
  TEST_ASSERT_TRUE(parseCommand("ntpsync now", true, a) == Command::Unknown);
}

void test_status_line_control_text() {
  StatusInfo s;
  s.controlText = "ctl 20.12C mean idle | xover 0";
  char buf[800];
  formatStatusLine(buf, sizeof(buf), s);
  TEST_ASSERT_NOT_NULL(strstr(buf, "| ctl 20.12C mean idle | xover 0"));
}

void test_reset_log_ring_and_format() {
  ResetLog log;
  log.magic = 0;                        // garbage from flash: add() starts a fresh list
  for (int i = 0; i < 13; i++) { ResetEntry x; x.reason = (uint8_t)i; log.add(x); }
  TEST_ASSERT_TRUE(log.valid());
  TEST_ASSERT_EQUAL_UINT32(13, log.boots);
  TEST_ASSERT_EQUAL_UINT8(ResetLog::kMax, log.count);
  TEST_ASSERT_EQUAL_UINT32(4, log.at(0).bootNo);              // oldest kept
  TEST_ASSERT_EQUAL_UINT32(13, log.at(ResetLog::kMax - 1).bootNo);

  char b[160];
  ResetEntry x; x.bootNo = 7; x.prevKnown = 1; x.prevAliveS = 171;
  LoadOutputs l; l.heat = true; l.relayHeating = true; l.humid = true;
  x.prevLoads = loadMask(l);
  formatResetEntry(b, sizeof(b), x, "BROWNOUT");
  TEST_ASSERT_EQUAL_STRING("boot 7  BROWNOUT      previous boot last alive at 0:02:51, live: heat humid K1-heat", b);
  x.prevKnown = 0;
  formatResetEntry(b, sizeof(b), x, "POWERON");
  TEST_ASSERT_EQUAL_STRING("boot 7  POWERON       previous boot: heartbeat lost", b);
}

void test_clock_summary_lost_time_corrected() {
  ClockState c;
  c.osfKnown = true; c.osfAtBoot = true;
  char b[240];
  formatClockSummary(b, sizeof(b), c);
  TEST_ASSERT_EQUAL_STRING("OSF SET at this boot (the DS3231 lost time) | NTP not synced yet | timestamps UNTRUSTED", b);
  c.ntpSynced = true; c.syncs = 1; c.firstSyncAtMs = 3000;
  c.corrected = true; c.rtcWritten = true; c.correctedAtMs = 4000;
  c.lastCorrectionS = -841234567;
  formatClockSummary(b, sizeof(b), c);
  TEST_ASSERT_EQUAL_STRING("OSF SET at this boot (the DS3231 lost time) | NTP first sync at 0:00:03, 1 sync | "
                           "DS3231 was -841234567 s (about 26.7 years) out; written at 0:00:04, OSF cleared | timestamps trusted", b);
}

void test_clock_summary_normal_boot() {
  ClockState c;
  c.osfKnown = true;
  c.ntpSynced = true; c.syncs = 2; c.firstSyncAtMs = 2000;
  c.corrected = true; c.lastCorrectionS = -1;
  char b[240];
  formatClockSummary(b, sizeof(b), c);
  TEST_ASSERT_EQUAL_STRING("OSF clear at this boot | NTP first sync at 0:00:02, 2 syncs | DS3231 within 2 s (-1 s), not written | timestamps trusted", b);
  ClockState u;                                  // status register never read
  formatClockSummary(b, sizeof(b), u);
  TEST_ASSERT_EQUAL_STRING("OSF not read this boot | NTP not synced yet | timestamps trusted", b);
}

void test_rtc_needs_write_and_ds3231_round_trip() {
  int64_t e = 0;
  TEST_ASSERT_FALSE(rtcNeedsWrite(1000, 998, &e));   // 2 s: not more than 2
  TEST_ASSERT_TRUE(rtcNeedsWrite(1000, 997, &e));
  TEST_ASSERT_EQUAL_INT64(3, e);
  TEST_ASSERT_TRUE(rtcNeedsWrite(946684800, 1794186000, &e));   // 2000-01-01 against 2026-11-08
  DateTime dt = fromUnixSeconds(1794186061);          // 2026-11-08T...
  uint8_t r[7];
  encodeDs3231(dt, r);
  DateTime back;
  TEST_ASSERT_TRUE(decodeDs3231(r, back));
  TEST_ASSERT_EQUAL_INT64(1794186061, toUnixSeconds(back));
  TEST_ASSERT_EQUAL_UINT16(2026, back.year);
  TEST_ASSERT_EQUAL_UINT8(11, back.month);
}

void test_summary_max_rh() {
  Readings r;
  for (int i = 0; i < kNumSensors; i++) { r.s[i].bus = BusResult::Ok; r.s[i].rhRawPct = 60.0f; }
  r.s[2].rhRawPct = 71.0f;
  r.s[kAmbientIndex].rhRawPct = 90.0f;   // room air: never counts
  CabinetSummary c = summarise(r);
  TEST_ASSERT_EQUAL_FLOAT(71.0f, c.maxRhPct);
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
    TEST_ASSERT_EQUAL_UINT8(i, kSensors[i].channel);                      // channels 0-5 (spec 2.3)
    TEST_ASSERT_EQUAL_INT(i, sensorIndexForSerial(kSensors[i].serial));   // serials unique
  }
  TEST_ASSERT_EQUAL_INT(-1, sensorIndexForSerial(0x12345678));
  TEST_ASSERT_EQUAL_FLOAT(1.0274f, kSensors[2].rhGain);    // ch2, build log
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
  RUN_TEST(test_summary_cabinet_is_ch0_to_ch3_only);
  RUN_TEST(test_line_reader_and_commands);
  RUN_TEST(test_fan_command);
  RUN_TEST(test_status_line_control_text);
  RUN_TEST(test_stage6_serial_interface_commands);
  RUN_TEST(test_humidcool_command);
  RUN_TEST(test_status_line_humid_text);
  RUN_TEST(test_line_reader_overflow);
  RUN_TEST(test_stage2_commands);
  RUN_TEST(test_status_line_guard_text_and_boot_demand);
  RUN_TEST(test_summary_max_rh);
  RUN_TEST(test_status_fan_readback);
  RUN_TEST(test_status_line_clock_health_and_net);
  RUN_TEST(test_resets_commands);
  RUN_TEST(test_clock_summary_lost_time_corrected);
  RUN_TEST(test_clock_summary_normal_boot);
  RUN_TEST(test_rtc_needs_write_and_ds3231_round_trip);
  RUN_TEST(test_reset_log_ring_and_format);
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
