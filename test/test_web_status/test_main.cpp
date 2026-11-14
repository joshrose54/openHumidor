// GET /api/status (spec 5.5), plain logic: the reply from a snapshot — valid JSON in any piece
// size; "--" (null) by the LCD's rule; the mode icons and alerts copied from the LCD's own logic so
// the two agree; Fault; the zone; blowers and ion on Overview; disturbances null while detection
// is off; the time in UK local, "--:--" while the clock isn't trusted.
#include <unity.h>
#include <string.h>
#include <string>
#include "wineador/web_status.h"
#include "wineador/lcd_ui.h"
#include "wineador/uk_time.h"
#include "../json_check.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

static const int64_t T0 = 1794571200;   // 2026-11-13T12:00:00Z (GMT: UK time 12:00)

static StatusSnapshot normal() {
  StatusSnapshot s;
  s.build.version = "2.0.0";
  s.uptimeS = 7200;
  s.clockOk = true;
  s.utcNowS = T0 + 32 * 60;
  s.nModes = 1;
  s.modes[0] = ModeIcon::Cooling;
  s.readingsOk = true;
  s.cabinetOk = 4;
  s.meanC = 20.6f; s.minC = 20.3f; s.maxC = 20.9f; s.spreadC = 0.6f; s.meanRhPct = 62.1f; s.controlC = 20.6f;
  s.readingsAgeS = 7;
  s.cool = true;
  s.compressorOnForS = 420;
  s.compressorStoppedSinceBoot = true;
  s.compressorStoppedAgoS = 1830;
  s.fanPct = 40; s.fanRpm = 1230;
  s.hasBlowers = true;   // Rev Q: the blowers in boost, holding; the ion generator waiting for 12:00 UTC
  s.blowerPct = 100; s.blowerMode = BlowerMode::Boost; s.blowerWhy = BlowerWhy::None;
  s.blowerSpreadC = 0.42f; s.blowerSpreadRhPct = 1.3f; s.blowerIdleInS = 215; s.blowerIdleIsFull = true;
  s.ionState = IonState::Waiting; s.ionNextFromClock = true; s.ionNextUtcS = 1794571200;   // 12:00 UTC = 12:00 GMT
  s.ionLastSkip = IonWhy::BootHold; s.ionLastSkipUtcS = 1794528000;   // 00:00 that day
  s.nGuards = 1;
  s.guards[0].load = 1; s.guards[0].reason = "crossover"; s.guards[0].leftS = 120;
  s.weekReady = true;
  s.crossovers = 2; s.strips = 1; s.humidRuns = 14; s.disturbances = 3;
  s.sdOk = true;
  s.sdFreeBytes = 59000000000ULL;
  s.sdTotalBytes = 59700000000ULL;
  s.wifiRssi = -61;
  s.bootReason = "POWERON";
  s.hasV12 = true;
  s.v12NowV = 12.08f;
  s.v12LowTodayV = 11.96f;
  s.heapFreeBytes = 182000;
  for (int i = 0; i < kNumSensors; i++) {
    s.sensors[i].state = SensorState::Healthy;
    s.sensors[i].read = true;
    s.sensors[i].tempC = 20.5f + 0.1f * i;
    s.sensors[i].rhPct = 62.0f;
    s.sensors[i].rhRawPct = 63.4f;
    s.sensors[i].ageS = 7;
  }
  return s;
}

static std::string json(const StatusSnapshot& s) {
  StatusReply r(s);
  return jsoncheck::whole(r);
}

static bool has(const std::string& j, const char* part) { return j.find(part) != std::string::npos; }

void test_status_reply_in_any_piece_size() {
  const StatusSnapshot s = normal();
  const std::string w = json(s);
  TEST_ASSERT_TRUE(jsoncheck::valid(w));
  for (size_t piece : {(size_t)481, (size_t)700, (size_t)4096}) {
    StatusReply r(s);
    TEST_ASSERT_EQUAL_STRING(w.c_str(), jsoncheck::inPieces(r, piece).c_str());
  }
  printf("status reply: %u bytes\n", (unsigned)w.size());
}

void test_status_reply_content() {
  const std::string w = json(normal());
  TEST_ASSERT_TRUE(has(w, "\"time\":{\"utc_s\":1794573120,\"uk\":\"12:32\",\"uk_date\":\"2026-11-13\",\"trusted\":true}"));
  TEST_ASSERT_TRUE(has(w, "\"build\":{\"board\":\"rev-q\",\"profile\":\"real\",\"test\":false,\"version\":\"2.0.0\"}"));
  TEST_ASSERT_TRUE(has(w, "\"modes\":[\"cooling\"],\"fault\":null,\"boot_hold_s\":0"));
  TEST_ASSERT_TRUE(has(w, "\"cabinet\":{\"t\":20.6,\"rh\":62.1,\"t_min\":20.3,\"t_max\":20.9,\"spread_t\":0.6,"
                          "\"control_t\":20.6,\"zone\":\"cool\",\"healthy\":4,\"age_s\":7}"));
  TEST_ASSERT_TRUE(has(w, "\"setpoints\":{\"t\":20,\"rh\":62}"));
  TEST_ASSERT_TRUE(has(w, "\"compressor\":{\"on\":true,\"stripping\":false,\"for_s\":420,\"stopped_ago_s\":1830}"));
  TEST_ASSERT_TRUE(has(w, "\"fan\":{\"pwm\":40,\"rpm\":1230},\"interlock\":\"cooling\""));
  TEST_ASSERT_TRUE(has(w, "\"blowers\":{\"pwm\":100,\"mode\":\"boost\",\"why\":null,\"spread_t\":0.42,\"spread_rh\":1.3,"
                          "\"idle_in_s\":215,\"idle_is_full\":true}"));
  TEST_ASSERT_TRUE(has(w, "\"ion\":{\"on\":false,\"state\":\"waiting\",\"why\":null,\"left_s\":null,\"next\":\"12:00\","
                          "\"next_in_s\":null,\"last_skipped\":{\"why\":\"boot hold\",\"at\":\"00:00\"}}"));
  {   // a board without them (Rev N): both null
    StatusSnapshot n = normal();
    n.hasBlowers = false;
    const std::string x = json(n);
    TEST_ASSERT_TRUE(has(x, "\"blowers\":null,\"ion\":null}"));
  }
  TEST_ASSERT_TRUE(has(w, "\"guards\":[{\"load\":\"heater\",\"reason\":\"crossover\",\"left_s\":120}]"));
  TEST_ASSERT_TRUE(has(w, "{\"ch\":3,\"name\":\"mid_rear\""));
  TEST_ASSERT_TRUE(has(w, "\"role\":\"chamber\""));
  TEST_ASSERT_TRUE(has(w, "\"role\":\"ambient\""));
  // disturbance detection is off until B6: shown as null, not 0
  TEST_ASSERT_TRUE(has(w, "\"week\":{\"crossovers\":2,\"strips\":1,\"humid_runs\":14,\"disturbances\":null,\"ready\":true}"));
  TEST_ASSERT_TRUE(has(w, "\"update\":{\"window_open\":false,\"window_s\":0,\"updating\":false,\"pct\":0}"));
  // the Board section (5.5)
  TEST_ASSERT_TRUE(has(w, "\"board\":{\"version\":\"2.0.0\",\"uptime_s\":7200,\"restart_reason\":\"POWERON\",\"v12_now\":12.08,"
                          "\"v12_low_today\":11.96,\"wifi_rssi\":-61,\"heap_free_bytes\":182000,\"card_free_bytes\":59000000000,"
                          "\"card_total_bytes\":59700000000}"));
}

void test_dashes_and_untrusted_time() {
  StatusSnapshot s = normal();
  s.readingsOk = false;              // fewer than 3 healthy, or not fresh (the LCD's "--")
  s.clockOk = false;
  s.weekReady = false;
  s.sensors[2].read = false;
  s.sensors[2].state = SensorState::Excluded;
  snprintf(s.sensors[2].reason, sizeof(s.sensors[2].reason), "absent");
  const std::string w = json(s);
  TEST_ASSERT_TRUE(jsoncheck::valid(w));
  TEST_ASSERT_TRUE(has(w, "\"cabinet\":{\"t\":null,\"rh\":null"));
  TEST_ASSERT_TRUE(has(w, "\"zone\":null"));
  TEST_ASSERT_TRUE(has(w, "\"uk\":\"--:--\",\"uk_date\":null,\"trusted\":false"));
  TEST_ASSERT_TRUE(has(w, "\"week\":{\"crossovers\":null"));
  TEST_ASSERT_TRUE(has(w, "\"state\":\"excluded\",\"reason\":\"absent\",\"t\":null"));
  // no 12V sense (Rev N), the card out: nulls, not zeros
  StatusSnapshot n = normal();
  n.hasV12 = false;
  n.sdOk = false;
  const std::string nw = json(n);
  TEST_ASSERT_TRUE(has(nw, "\"v12_now\":null,\"v12_low_today\":null"));
  TEST_ASSERT_TRUE(has(nw, "\"card_free_bytes\":null,\"card_total_bytes\":null"));
}

void test_zones() {
  TEST_ASSERT_EQUAL_STRING("cool", zoneName(20.6f, 20.0f, 0.5f, 1.5f));
  TEST_ASSERT_EQUAL_STRING("dead", zoneName(20.5f, 20.0f, 0.5f, 1.5f));
  TEST_ASSERT_EQUAL_STRING("dead", zoneName(18.5f, 20.0f, 0.5f, 1.5f));
  TEST_ASSERT_EQUAL_STRING("heat", zoneName(18.4f, 20.0f, 0.5f, 1.5f));
}

void test_modes_and_alerts_are_the_lcds() {
  LcdUi ui;
  ui.begin(0, 20);
  UiInputs in;
  in.nowMs = 1000;
  in.uptimeMs = 1000;
  in.cabinetOk = 4;
  in.fresh = true;
  in.meanC = 18.0f;
  in.meanRhPct = 58.0f;
  in.heat = true;
  in.humid = true;
  in.clockOk = true;
  in.utcNowS = T0;
  in.alerts.sensorsKnown = true;
  in.alerts.excludedMask = 0x04;   // ch2 excluded
  ui.update(in);
  StatusSnapshot s = normal();
  copyFromLcd(&s, ui, in.uptimeMs, in.clockOk, in.utcNowS);
  std::string w = json(s);
  TEST_ASSERT_TRUE(has(w, "\"modes\":[\"heating\",\"humidifying\"],\"fault\":null"));
  TEST_ASSERT_EQUAL_INT(1, s.nAlerts);
  TEST_ASSERT_EQUAL_STRING(ui.alerts().at(0).title, s.alerts[0].title);
  TEST_ASSERT_TRUE(has(w, "\"since\":\"12:00\",\"rank\":3,\"fault\":false"));
  // a Fault replaces every other icon, and leads the alerts
  in.nowMs = in.uptimeMs = 5000;
  in.alerts.sensorFault = SensorFault::Cabinet;
  in.alerts.excludedMask = 0x07;
  ui.update(in);
  copyFromLcd(&s, ui, in.uptimeMs, in.clockOk, in.utcNowS);
  w = json(s);
  TEST_ASSERT_TRUE(jsoncheck::valid(w));
  TEST_ASSERT_TRUE(has(w, "\"modes\":[\"fault\"]"));
  TEST_ASSERT_TRUE(s.nAlerts >= 4);
  TEST_ASSERT_TRUE(has(w, "\"fault\":\""));
  TEST_ASSERT_EQUAL_INT(0, s.alerts[0].rank);
  TEST_ASSERT_EQUAL_STRING(ui.alerts().at(0).title, s.alerts[0].title);   // the LCD's alert list
  TEST_ASSERT_EQUAL_STRING("Too few sensors", ui.view().alertText);         // its line, without "Fault: "
  // every alert slot at once still fits, and in pieces
  in.alerts.excludedMask = 0x3F;
  in.alerts.fanStalled = true;
  in.alerts.settingsBad = true;
  in.alerts.dip12v = true;
  in.alerts.heaterIneffective = in.alerts.coolingIneffective = in.alerts.humidIneffective = true;
  in.alerts.sdFailed = true;
  in.alerts.clockBad = true;
  ui.update(in);
  copyFromLcd(&s, ui, in.uptimeMs, false, in.utcNowS);
  StatusReply a(s), b(s);
  const std::string all = jsoncheck::whole(a);
  TEST_ASSERT_TRUE(jsoncheck::valid(all));
  TEST_ASSERT_EQUAL_STRING(all.c_str(), jsoncheck::inPieces(b, b.maxStep() + 1).c_str());
  TEST_ASSERT_EQUAL_INT(kAlertSlots, s.nAlerts);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_status_reply_in_any_piece_size);
  RUN_TEST(test_status_reply_content);
  RUN_TEST(test_dashes_and_untrusted_time);
  RUN_TEST(test_zones);
  RUN_TEST(test_modes_and_alerts_are_the_lcds);
  return UNITY_END();
}
