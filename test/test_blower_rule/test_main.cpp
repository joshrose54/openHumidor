// The circulation blowers' speed (spec 4.6b, Rev Q): idle, boost on a wide cabinet spread, back to
// idle after 10 minutes of low spread, full speed in a Fault or without a fresh reading, idle during
// an update (a fault included), and starting in boost after any spell at full speed.
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include "wineador/blower_rule.h"
#include "wineador/settings.h"
#include "wineador/event_text.h"
#include "wineador/event_sentence.h"
#include "wineador/sensor_health.h"
#include "wineador/sensor_table.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

// A test idle duty (the real one stays 100 until BP2 test 22), so idle and boost differ.
static TuningSettings tun(uint8_t idle = 40) {
  TuningSettings t = settingsFor(Profile::Real).tuning;
  t.blowerIdlePct = idle;
  return t;
}

struct Rig {
  BlowerRule b;
  uint32_t now = 1000;
  explicit Rig(const TuningSettings& t) { b.begin(t); }
  // One sweep with this spread (fresh, no fault), then the passes in between.
  uint8_t sweep(float c, float rh, bool fault = false, bool updating = false, int cab = 4) {
    BlowerInputs in;
    in.nowMs = now;
    in.fresh = true;
    in.sweepDone = true;
    in.fault = fault;
    in.updating = updating;
    in.cabinetOk = cab;
    in.spreadC = c;
    in.spreadRhPct = rh;
    return b.update(in);
  }
  uint8_t pass(bool fresh = true, bool fault = false, bool updating = false) {
    BlowerInputs in;
    in.nowMs = now;
    in.fresh = fresh;
    in.fault = fault;
    in.updating = updating;
    in.cabinetOk = 4;
    return b.update(in);
  }
  // Sweeps every 15 s for ms, the same spread each time.
  void hold(uint32_t ms, float c, float rh) {
    for (uint32_t t = 0; t < ms; t += 15000) { now += 15000; sweep(c, rh); }
  }
  void drain() { BlowerRule::Change c; while (b.take(&c)) {} }
};

// The rule starts in boost after boot and gets to idle after 10 min of low spread.
static void toIdle(Rig& r) {
  r.sweep(0.1f, 0.5f);
  r.hold(600000, 0.1f, 0.5f);
  TEST_ASSERT_TRUE(r.b.mode() == BlowerMode::Idle);
  r.drain();
}

void test_the_idle_duty_is_full_speed_until_bp2_test_22() {
  const TuningSettings t = settingsFor(Profile::Real).tuning;
  TEST_ASSERT_EQUAL_UINT8(100, t.blowerIdlePct);
  TEST_ASSERT_EQUAL_UINT8(100, settingsFor(Profile::Bench).tuning.blowerIdlePct);
  // with the default, every state gives full speed - but the rule still runs and says which
  Rig r(t);
  TEST_ASSERT_EQUAL_UINT8(100, r.pass(false));
  TEST_ASSERT_TRUE(r.b.mode() == BlowerMode::Full);
  toIdle(r);
  TEST_ASSERT_EQUAL_UINT8(100, r.b.pct());
  TEST_ASSERT_EQUAL_UINT8(100, r.pass(true, false, true));
  TEST_ASSERT_TRUE(r.b.mode() == BlowerMode::Updating);
}

void test_the_settings_values() {
  const TuningSettings t = settingsFor(Profile::Real).tuning;
  TEST_ASSERT_EQUAL_FLOAT(1.0f, t.blowerBoostAboveC);
  TEST_ASSERT_EQUAL_FLOAT(3.0f, t.blowerBoostAboveRhPct);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, t.blowerIdleBelowC);
  TEST_ASSERT_EQUAL_FLOAT(2.0f, t.blowerIdleBelowRhPct);
  TEST_ASSERT_EQUAL_UINT32(600000, t.blowerIdleHoldMs);
  TEST_ASSERT_EQUAL_UINT32(60000, settingsFor(Profile::Bench).tuning.blowerIdleHoldMs);   // B3 table: 1 min
  // the boot check: never 0 (24/7), at most 100
  Settings s = settingsFor(Profile::Real);
  TEST_ASSERT_NULL(validateSettings(s));
  s.tuning.blowerIdlePct = 0;
  TEST_ASSERT_NOT_NULL(validateSettings(s));
  s.tuning.blowerIdlePct = 101;
  TEST_ASSERT_NOT_NULL(validateSettings(s));
  s.tuning.blowerIdlePct = 35;
  TEST_ASSERT_NULL(validateSettings(s));
  s.tuning.blowerIdleBelowC = 1.0f;   // must sit below the boost threshold
  TEST_ASSERT_NOT_NULL(validateSettings(s));
}

void test_boot_is_full_speed_then_boost_then_idle_after_the_hold() {
  Rig r(tun());
  TEST_ASSERT_TRUE(r.b.mode() == BlowerMode::Full);
  TEST_ASSERT_TRUE(r.b.why() == BlowerWhy::Boot);
  TEST_ASSERT_EQUAL_UINT8(100, r.pass(false));       // no reading yet
  r.drain();
  TEST_ASSERT_EQUAL_UINT8(100, r.sweep(0.1f, 0.5f)); // the first sweep: boost, not straight to idle
  TEST_ASSERT_TRUE(r.b.mode() == BlowerMode::Boost);
  BlowerRule::Change c;
  TEST_ASSERT_TRUE(r.b.take(&c));
  TEST_ASSERT_TRUE(c.e == BlowerRule::Event::Boost);   // no boot full-speed event; the boost says "after boot"
  {
    char t[120];
    formatBlowerEvent(t, sizeof(t), c, tun());
    TEST_ASSERT_EQUAL_STRING("BLOWERS BOOST: after boot; idle after 10 min of low spread", t);
  }
  TEST_ASSERT_FALSE(r.b.take(&c));
  // 9 min 45 s of low spread: still boost
  r.hold(585000, 0.1f, 0.5f);
  TEST_ASSERT_TRUE(r.b.mode() == BlowerMode::Boost);
  TEST_ASSERT_TRUE(r.b.holding());
  TEST_ASSERT_EQUAL_UINT32(15000, r.b.holdLeftMs(r.now));
  r.now += 15000;
  TEST_ASSERT_EQUAL_UINT8(40, r.pass());               // 10 min: idle, between sweeps too
  TEST_ASSERT_TRUE(r.b.mode() == BlowerMode::Idle);
  TEST_ASSERT_TRUE(r.b.take(&c));
  TEST_ASSERT_TRUE(c.e == BlowerRule::Event::Idle);
  TEST_ASSERT_EQUAL_UINT32(600000, c.heldMs);
}

void test_boost_at_once_above_either_threshold_and_strictly_above() {
  Rig r(tun());
  toIdle(r);
  r.now += 15000;
  TEST_ASSERT_EQUAL_UINT8(40, r.sweep(1.0f, 3.0f));    // exactly at both: not above
  TEST_ASSERT_TRUE(r.b.mode() == BlowerMode::Idle);
  r.now += 15000;
  TEST_ASSERT_EQUAL_UINT8(100, r.sweep(1.01f, 0.2f));  // temperature alone
  TEST_ASSERT_TRUE(r.b.mode() == BlowerMode::Boost);
  BlowerRule::Change c;
  TEST_ASSERT_TRUE(r.b.take(&c));
  TEST_ASSERT_TRUE(c.e == BlowerRule::Event::Boost);
  TEST_ASSERT_EQUAL_FLOAT(1.01f, c.spreadC);
  char t[160];
  formatBlowerEvent(t, sizeof(t), c, tun());
  TEST_ASSERT_EQUAL_STRING("BLOWERS BOOST: spread 1.01 C, 0.2 points (above 1.0 C or 3 points)", t);

  Rig q(tun());
  toIdle(q);
  q.now += 15000;
  TEST_ASSERT_EQUAL_UINT8(100, q.sweep(0.2f, 3.1f));   // RH alone
  TEST_ASSERT_TRUE(q.b.mode() == BlowerMode::Boost);
}

void test_between_the_thresholds_the_blowers_stay_as_they_are() {
  Rig r(tun());
  toIdle(r);
  r.hold(3600000, 0.8f, 2.5f);                         // an hour between: stays idle
  TEST_ASSERT_TRUE(r.b.mode() == BlowerMode::Idle);
  r.now += 15000;
  r.sweep(1.5f, 1.0f);
  TEST_ASSERT_TRUE(r.b.mode() == BlowerMode::Boost);
  r.hold(3600000, 0.8f, 1.0f);                         // an hour between: stays boost, never holding
  TEST_ASSERT_TRUE(r.b.mode() == BlowerMode::Boost);
  TEST_ASSERT_FALSE(r.b.holding());
  r.hold(600000, 0.5f, 1.0f);                          // exactly 0.5: not below
  TEST_ASSERT_TRUE(r.b.mode() == BlowerMode::Boost);
  r.hold(600000, 0.1f, 2.0f);                          // RH exactly 2: not below
  TEST_ASSERT_TRUE(r.b.mode() == BlowerMode::Boost);
}

void test_one_sweep_not_low_restarts_the_ten_minutes() {
  Rig r(tun());
  toIdle(r);
  r.now += 15000;
  r.sweep(2.0f, 0);
  r.hold(540000, 0.2f, 1.0f);                          // 9 min low
  r.now += 15000;
  r.sweep(0.7f, 1.0f);                                 // one sweep in between
  TEST_ASSERT_FALSE(r.b.holding());
  r.hold(540000, 0.2f, 1.0f);                          // 9 min low again: still boost
  TEST_ASSERT_TRUE(r.b.mode() == BlowerMode::Boost);
  r.hold(75000, 0.2f, 1.0f);
  TEST_ASSERT_TRUE(r.b.mode() == BlowerMode::Idle);
  // a wide spread while holding: boost again, the hold gone
  r.now += 15000;
  r.sweep(1.2f, 0);
  r.hold(300000, 0.2f, 1.0f);
  r.now += 15000;
  r.sweep(1.2f, 0);
  TEST_ASSERT_FALSE(r.b.holding());
}

void test_full_speed_in_a_fault_and_boost_after_it() {
  Rig r(tun());
  toIdle(r);
  r.now += 15000;
  TEST_ASSERT_EQUAL_UINT8(100, r.sweep(0.1f, 0.5f, true));
  TEST_ASSERT_TRUE(r.b.mode() == BlowerMode::Full);
  TEST_ASSERT_TRUE(r.b.why() == BlowerWhy::Fault);
  BlowerRule::Change c;
  TEST_ASSERT_TRUE(r.b.take(&c));
  TEST_ASSERT_TRUE(c.e == BlowerRule::Event::FullStart);
  char t[120];
  formatBlowerEvent(t, sizeof(t), c, tun());
  TEST_ASSERT_EQUAL_STRING("BLOWERS FULL SPEED: fault", t);
  for (int i = 0; i < 40; i++) { r.now += 15000; TEST_ASSERT_EQUAL_UINT8(100, r.sweep(0.1f, 0.5f, true)); }
  TEST_ASSERT_FALSE(r.b.take(&c));                     // one event for the spell
  r.now += 15000;
  TEST_ASSERT_EQUAL_UINT8(100, r.sweep(0.1f, 0.5f));   // the fault over: boost, not idle
  TEST_ASSERT_TRUE(r.b.mode() == BlowerMode::Boost);
  TEST_ASSERT_TRUE(r.b.take(&c));
  TEST_ASSERT_TRUE(c.e == BlowerRule::Event::FullEnd);
  formatBlowerEvent(t, sizeof(t), c, tun());
  TEST_ASSERT_EQUAL_STRING("BLOWERS FULL SPEED ENDED (fault)", t);
  TEST_ASSERT_TRUE(r.b.take(&c));
  TEST_ASSERT_TRUE(c.e == BlowerRule::Event::Boost);
  formatBlowerEvent(t, sizeof(t), c, tun());
  TEST_ASSERT_EQUAL_STRING("BLOWERS BOOST: after full speed (fault); idle after 10 min of low spread", t);
  r.hold(600000, 0.1f, 0.5f);
  TEST_ASSERT_TRUE(r.b.mode() == BlowerMode::Idle);
}

void test_no_fresh_reading_is_full_speed() {
  Rig r(tun());
  toIdle(r);
  r.now += 31000;
  TEST_ASSERT_EQUAL_UINT8(100, r.pass(false));
  TEST_ASSERT_TRUE(r.b.why() == BlowerWhy::Stale);
  // a fault while stale: the reason changes (an end and a start)
  r.now += 1000;
  r.pass(false, true);
  TEST_ASSERT_TRUE(r.b.why() == BlowerWhy::Fault);
  r.drain();
  r.now += 1000;
  r.sweep(0.1f, 0.5f);
  TEST_ASSERT_TRUE(r.b.mode() == BlowerMode::Boost);
}

void test_an_update_is_idle_even_in_a_fault_then_boost() {
  Rig r(tun());
  toIdle(r);
  r.now += 15000;
  r.sweep(2.0f, 0);                                    // boost...
  r.now += 1000;
  TEST_ASSERT_EQUAL_UINT8(40, r.pass(true, false, true));   // ...an update: idle
  TEST_ASSERT_TRUE(r.b.mode() == BlowerMode::Updating);
  r.now += 1000;
  TEST_ASSERT_EQUAL_UINT8(40, r.pass(false, true, true));   // a fault and no readings: still idle
  r.drain();
  // the update over (abandoned): boost, waiting for 10 min of low spread
  r.now += 1000;
  TEST_ASSERT_EQUAL_UINT8(100, r.sweep(0.1f, 0.5f));
  TEST_ASSERT_TRUE(r.b.mode() == BlowerMode::Boost);
  {
    BlowerRule::Change c;
    TEST_ASSERT_TRUE(r.b.take(&c));
    char t[120];
    formatBlowerEvent(t, sizeof(t), c, tun());
    TEST_ASSERT_EQUAL_STRING("BLOWERS BOOST: after the update; idle after 10 min of low spread", t);
  }
  // a fault, then an update in it: the full-speed spell ends, logged
  r.now += 1000;
  r.sweep(0.1f, 0.5f, true);
  r.drain();
  r.now += 1000;
  r.pass(true, true, true);
  BlowerRule::Change c;
  TEST_ASSERT_TRUE(r.b.take(&c));
  TEST_ASSERT_TRUE(c.e == BlowerRule::Event::FullEnd);
}

void test_the_spread_needs_two_cabinet_sensors() {
  Rig r(tun());
  toIdle(r);
  r.now += 15000;
  r.sweep(3.0f, 9.0f, false, false, 1);                // one sensor: no spread to judge
  TEST_ASSERT_TRUE(r.b.mode() == BlowerMode::Idle);
}

void test_the_spread_leaves_out_excluded_sensors() {
  // The rule takes the health summary's spread: healthy cabinet sensors only, a manual exclude
  // included, corrected RH.
  SensorHealth h;
  Settings s = settingsFor(Profile::Real);
  h.begin(s);
  for (int i = 0; i < kNumSensors; i++) h.serialResult(i, BusResult::Ok, kSensors[i].serial);
  Readings rd;
  for (int i = 0; i < kNumSensors; i++) {
    rd.s[i].bus = BusResult::Ok;
    rd.s[i].tempC = 20.0f;
    rd.s[i].rhRawPct = rawForCorrectedRh(i, 62.0f);
  }
  rd.s[2].tempC = 23.0f;                                // ch2 3 C high
  rd.s[1].rhRawPct = rawForCorrectedRh(1, 66.0f);       // ch1 4 points high (corrected)
  h.update(rd, 15000);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.0f, h.summary().spreadC);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 4.0f, h.summary().spreadRhPct);
  s.user.manualExcludeMask = (uint8_t)((1u << 2) | (1u << 1));
  h.setUser(s.user);
  h.update(rd, 30000);
  TEST_ASSERT_FALSE(h.healthy(2));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, h.summary().spreadC);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f, h.summary().spreadRhPct);
}

void test_the_events_read_as_sentences() {
  char o[200];
  eventSentence(o, sizeof(o), kEvTypeBlowers, "BLOWERS BOOST: spread 1.20 C, 2.0 points (above 1.0 C or 3 points)");
  TEST_ASSERT_EQUAL_STRING("Blowers to boost (full speed): spread 1.20\xC2\xA0\xC2\xB0" "C, 2.0 points (above 1.0\xC2\xA0\xC2\xB0" "C or 3 points)", o);
  eventSentence(o, sizeof(o), kEvTypeBlowers, "BLOWERS FULL SPEED: fault");
  TEST_ASSERT_EQUAL_STRING("Blowers at full speed: fault", o);
  eventSentence(o, sizeof(o), kEvTypeBlowers, "BLOWERS FULL SPEED ENDED (fault)");
  TEST_ASSERT_EQUAL_STRING("Blowers' full speed ended (fault)", o);
  eventSentence(o, sizeof(o), kEvTypeBlowers, "BLOWERS IDLE: spread below 0.5 C and 2 points for 10 min; 100%");
  TEST_ASSERT_EQUAL_STRING("Blowers back to idle: spread below 0.5\xC2\xA0\xC2\xB0" "C and 2 points for 10 min; 100%", o);
}

void test_the_state_line() {
  Rig r(tun());
  char o[64];
  formatBlowerState(o, sizeof(o), r.b, r.now);
  TEST_ASSERT_EQUAL_STRING("100% FULL (no reading yet since boot)", o);
  r.sweep(0.1f, 0.5f);
  r.hold(300000, 0.1f, 0.5f);
  formatBlowerState(o, sizeof(o), r.b, r.now);
  TEST_ASSERT_EQUAL_STRING("100% boost (idle in 5:00)", o);
  r.hold(300000, 0.1f, 0.5f);
  formatBlowerState(o, sizeof(o), r.b, r.now);
  TEST_ASSERT_EQUAL_STRING("40% idle", o);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_idle_duty_is_full_speed_until_bp2_test_22);
  RUN_TEST(test_the_settings_values);
  RUN_TEST(test_boot_is_full_speed_then_boost_then_idle_after_the_hold);
  RUN_TEST(test_boost_at_once_above_either_threshold_and_strictly_above);
  RUN_TEST(test_between_the_thresholds_the_blowers_stay_as_they_are);
  RUN_TEST(test_one_sweep_not_low_restarts_the_ten_minutes);
  RUN_TEST(test_full_speed_in_a_fault_and_boost_after_it);
  RUN_TEST(test_no_fresh_reading_is_full_speed);
  RUN_TEST(test_an_update_is_idle_even_in_a_fault_then_boost);
  RUN_TEST(test_the_spread_needs_two_cabinet_sensors);
  RUN_TEST(test_the_spread_leaves_out_excluded_sensors);
  RUN_TEST(test_the_events_read_as_sentences);
  RUN_TEST(test_the_state_line);
  return UNITY_END();
}
