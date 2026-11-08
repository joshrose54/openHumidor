// Humidifier control (stage 5): call with persistence and release (4.7b), held
// through guard pauses, persistence only outside strips, the vapour-pressure
// ineffective check with its retry hold (5.6), and the humidifier guards it feeds.
#include <unity.h>
#include <string.h>
#include <initializer_list>
#include "wineador/humid_control.h"
#include "wineador/psychro.h"
#include "wineador/guards.h"
#include "wineador/controller.h"
#include "wineador/settings.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

namespace {

CabinetSummary cabRh(float rh, float c = 20.0f) {
  CabinetSummary s;
  s.sensorsOk = 6;
  s.cabinetOk = 4;
  s.meanC = s.minC = s.maxC = s.controlC = c;
  s.meanRhPct = s.maxRhPct = rh;
  s.meanVpKpa = vapourPressureKpa(c, rh);
  s.chamberOk = true;
  s.chamberC = c;
  return s;
}

struct Rig {
  Settings s;
  HumidControl hc;
  HumidInputs in;
  HumidOutput out;
  uint32_t now = 1000;
  explicit Rig(Profile p = Profile::Bench) : s(settingsFor(p)) {
    hc.begin(s, now);
    in.readingsFresh = true;
    in.cab = cabRh(62.0f);
  }
  HumidOutput step(uint32_t dt = 0) {
    now += dt;
    in.nowMs = now;
    out = hc.update(in);
    return out;
  }
  int count(HumidEvent::Type t) {
    int n = 0;
    for (HumidEvent e; hc.takeEvent(e);) if (e.type == t) n++;
    return n;
  }
};

}  // namespace

// ------------------------------------------------------------------ call
void test_call_after_full_persistence_both_profiles() {
  for (Profile p : {Profile::Real, Profile::Bench}) {
    Rig r(p);
    const uint32_t per = r.s.tuning.humidifyPersistMs;
    r.in.cab = cabRh(60.0f);                     // = setpoint - band: triggers (<=)
    r.step(1000);
    const uint32_t t0 = r.now;
    r.step(per - 1000);
    TEST_ASSERT_FALSE(r.out.demand);
    r.step(1000);
    TEST_ASSERT_TRUE(r.out.demand);
    TEST_ASSERT_EQUAL_UINT32(per, r.now - t0);
    TEST_ASSERT_EQUAL_INT(1, r.count(HumidEvent::Type::CallStart));
  }
}

void test_no_call_above_trigger() {
  Rig r;
  r.in.cab = cabRh(60.1f);
  r.step(1000);
  r.step(10 * r.s.tuning.humidifyPersistMs);
  TEST_ASSERT_FALSE(r.out.demand);
  TEST_ASSERT_FALSE(r.hc.persisting());
}

void test_persistence_restarts_after_a_break() {
  Rig r;
  const uint32_t per = r.s.tuning.humidifyPersistMs;
  r.in.cab = cabRh(58.0f);
  r.step(1000);
  r.step(per - 5000);
  r.in.cab = cabRh(60.5f);
  r.step(1000);
  r.in.cab = cabRh(58.0f);
  r.step(1000);
  const uint32_t t0 = r.now;
  while (!r.out.demand && r.now - t0 < 3 * per) r.step(500);
  TEST_ASSERT_UINT32_WITHIN(500, per, r.now - t0);
}

void test_release_at_setpoint_no_minimum_on() {
  Rig r;
  r.in.cab = cabRh(58.0f);
  r.step(1000);
  r.step(r.s.tuning.humidifyPersistMs);
  TEST_ASSERT_TRUE(r.out.demand);
  r.in.live.humid = true;
  r.step(1000);
  r.in.cab = cabRh(61.9f);
  TEST_ASSERT_TRUE(r.step(1000).demand);       // still below setpoint
  r.in.cab = cabRh(62.0f);
  TEST_ASSERT_FALSE(r.step(1000).demand);      // at setpoint: off at once
  TEST_ASSERT_EQUAL_INT(1, r.count(HumidEvent::Type::CallEnd));
}

void test_call_held_through_guard_pauses() {
  // The guards stop the humidifier (max run, ceiling, sensors, stripping): the
  // call stays on and it resumes with no fresh count.
  Rig r;
  r.in.cab = cabRh(56.0f);
  r.step(1000);
  r.step(r.s.tuning.humidifyPersistMs);
  TEST_ASSERT_TRUE(r.out.demand);
  r.in.live.humid = true;  r.step(1000);
  r.in.live.humid = false; r.step(1000);       // paused by a guard
  r.in.stripping = true;   r.step(10000);      // a strip starts meanwhile
  TEST_ASSERT_TRUE(r.out.demand);
  r.in.stripping = false;
  r.in.cab.cabinetOk = 2;                      // sensors short: call left as it is
  r.in.cab.meanRhPct = 63.0f;
  TEST_ASSERT_TRUE(r.step(1000).demand);
  r.in.cab = cabRh(57.0f);
  TEST_ASSERT_TRUE(r.step(1000).demand);
  TEST_ASSERT_EQUAL_INT(1, (int)r.hc.calls());
}

void test_persistence_only_outside_strips() {
  Rig r;
  const uint32_t per = r.s.tuning.humidifyPersistMs;
  r.in.cab = cabRh(57.0f, 22.5f);              // warm cabinet: low RH while a strip runs
  r.in.stripping = true;
  r.step(1000);
  r.step(5 * per);
  TEST_ASSERT_FALSE(r.out.demand);
  TEST_ASSERT_FALSE(r.hc.persisting());
  r.in.stripping = false;                      // strip ends: count from zero
  r.step(1000);
  const uint32_t t0 = r.now;
  while (!r.out.demand && r.now - t0 < 3 * per) r.step(500);
  TEST_ASSERT_UINT32_WITHIN(500, per, r.now - t0);
}

void test_needs_enough_fresh_sensors_and_scripted_suspends() {
  Rig r;
  r.in.cab = cabRh(55.0f);
  r.in.cab.cabinetOk = 2;
  r.step(1000);
  r.step(5 * r.s.tuning.humidifyPersistMs);
  TEST_ASSERT_FALSE(r.out.demand);
  r.in.cab.cabinetOk = 4;
  r.in.readingsFresh = false;
  r.step(5 * r.s.tuning.humidifyPersistMs);
  TEST_ASSERT_FALSE(r.out.demand);
  Rig q;
  q.in.cab = cabRh(55.0f);
  q.in.scriptedDemand = true;
  q.step(1000);
  q.step(5 * q.s.tuning.humidifyPersistMs);
  TEST_ASSERT_FALSE(q.out.demand);
}

// ------------------------------------------------------------------ ineffective
void test_rise_needed_is_half_a_point_at_setpoint() {
  Rig r;
  TEST_ASSERT_FLOAT_WITHIN(0.0005f, 0.0117f, r.hc.riseNeededKpa());   // ~0.012 kPa (5.5)
}

void test_ineffective_off_flag_retry_clear_both_profiles() {
  for (Profile p : {Profile::Real, Profile::Bench}) {
    Rig r(p);
    const TuningSettings& t = r.s.tuning;
    r.in.cab = cabRh(56.0f);
    r.step(1000);
    r.step(t.humidifyPersistMs);
    r.in.live.humid = true;
    r.step(1000);
    // vapour pressure rises less than needed
    CabinetSummary c = cabRh(56.0f);
    c.meanVpKpa += 0.8f * r.hc.riseNeededKpa();
    r.in.cab = c;
    r.step(t.humidIneffectiveWindowMs - 1000);
    TEST_ASSERT_FALSE(r.hc.ineffective());
    r.step(1000);
    TEST_ASSERT_TRUE(r.hc.ineffective());
    TEST_ASSERT_TRUE(r.out.ineffectiveHold);
    TEST_ASSERT_TRUE(r.out.demand);               // the call stays; the guard holds it off
    TEST_ASSERT_EQUAL_INT(1, r.count(HumidEvent::Type::Ineffective));
    r.in.live.humid = false;
    r.step(t.humidIneffectiveRetryMs - 1000);
    TEST_ASSERT_TRUE(r.out.ineffectiveHold);
    r.step(1000);
    TEST_ASSERT_FALSE(r.out.ineffectiveHold);     // retry
    TEST_ASSERT_TRUE(r.hc.ineffective());
    r.in.cab = cabRh(56.0f);
    r.in.live.humid = true;
    r.step(1000);
    c = cabRh(56.0f);
    c.meanVpKpa += r.hc.riseNeededKpa();
    r.in.cab = c;
    r.step(1000);
    TEST_ASSERT_FALSE(r.hc.ineffective());
    TEST_ASSERT_EQUAL_INT(1, r.count(HumidEvent::Type::Effective));
  }
}

void test_check_ignores_rh_moving_with_temperature() {
  // The cabinet warms while the humidifier runs with no water going in: RH falls,
  // vapour pressure is flat. It must fail on vapour pressure, not "pass" or "fail"
  // on RH. And cooling with no water: RH rises, but it still fails.
  Rig r;
  const TuningSettings& t = r.s.tuning;
  r.in.cab = cabRh(56.0f, 20.0f);
  r.step(1000);
  r.step(t.humidifyPersistMs);
  r.in.live.humid = true;
  r.step(1000);
  const float vp = vapourPressureKpa(20.0f, 56.0f);
  CabinetSummary c = cabRh(rhFromVp(18.0f, vp), 18.0f);   // cooled 2 C: RH up ~7 points, same water
  TEST_ASSERT_TRUE(c.meanRhPct > 62.0f);
  c.meanRhPct = 61.0f;                                    // (keep the call on for this check)
  c.meanVpKpa = vp;
  r.in.cab = c;
  r.step(t.humidIneffectiveWindowMs);
  TEST_ASSERT_TRUE(r.hc.ineffective());
}

void test_run_ending_inside_window_no_result() {
  Rig r;
  r.in.cab = cabRh(56.0f);
  r.step(1000);
  r.step(r.s.tuning.humidifyPersistMs);
  r.in.live.humid = true;
  r.step(1000);
  r.step(r.s.tuning.humidIneffectiveWindowMs / 2);
  r.in.live.humid = false;
  r.step(1000);
  r.step(2 * r.s.tuning.humidIneffectiveWindowMs);
  TEST_ASSERT_FALSE(r.hc.ineffective());
}

void test_runs_counted() {
  Rig r;
  for (int i = 0; i < 3; i++) {
    r.in.live.humid = true;  r.step(1000);
    r.in.live.humid = false; r.step(1000);
  }
  TEST_ASSERT_EQUAL_UINT32(3, r.hc.runs());
}

// ------------------------------------------------------------------ guards
namespace {
GuardInputs guardsReady(const CabinetSummary& c) {
  GuardInputs in;
  in.nowMs = minutes(10);
  in.bootHoldActive = false;
  in.fanOk = true;
  in.readingsFresh = true;
  in.cab = c;
  return in;
}
}  // namespace

void test_guard_holds_humidifier_when_ineffective() {
  Settings s = settingsFor(Profile::Bench);
  RuntimeGuards g;
  g.begin(s, 0);
  GuardInputs in = guardsReady(cabRh(56.0f));
  in.humidIneffectiveHold = true;
  Demand d; d.humid = true;
  LoadOutputs live; live.humid = true;
  GuardDecisions out = g.evaluate(in, d, live);
  TEST_ASSERT_FALSE(out.humid.run);
  TEST_ASSERT_TRUE(out.humid.block == GuardBlock::HumidIneffective);
  TEST_ASSERT_EQUAL_STRING("humid-ineffective", guardBlockName(GuardBlock::HumidIneffective));
}

void test_humidify_during_cooling_toggle() {
  Settings s = settingsFor(Profile::Bench);
  RuntimeGuards g;
  g.begin(s, 0);
  GuardInputs in = guardsReady(cabRh(56.0f));
  Demand d; d.cool = true; d.humid = true;
  LoadOutputs live; live.cool = true;
  GuardDecisions out = g.evaluate(in, d, live);
  TEST_ASSERT_FALSE(out.humid.run);                          // setting off: blocked
  TEST_ASSERT_TRUE(out.humid.block == GuardBlock::CompressorRun);
  g.setHumidifyDuringCooling(true);
  out = g.evaluate(in, d, live);
  TEST_ASSERT_TRUE(out.humid.run);                           // on: allowed
  in.strippingActive = true;
  out = g.evaluate(in, d, live);
  TEST_ASSERT_FALSE(out.humid.run);                          // never during stripping, setting or not
  TEST_ASSERT_TRUE(out.humid.block == GuardBlock::Stripping);
}

void test_ceiling_beats_a_low_mean() {
  Settings s = settingsFor(Profile::Bench);
  RuntimeGuards g;
  g.begin(s, 0);
  CabinetSummary c = cabRh(55.0f);
  c.maxRhPct = 70.0f;                                        // one sensor at the ceiling
  GuardInputs in = guardsReady(c);
  Demand d; d.humid = true;
  LoadOutputs live; live.humid = true;
  GuardDecisions out = g.evaluate(in, d, live);
  TEST_ASSERT_FALSE(out.humid.run);
  TEST_ASSERT_TRUE(out.humid.block == GuardBlock::RhCeiling);
}

// ------------------------------------------------------------------ text
void test_event_and_status_text() {
  Settings s = settingsFor(Profile::Bench);
  char buf[300];
  HumidEvent e;
  e.type = HumidEvent::Type::CallStart;
  e.rh = 59.0f;
  e.count = 1;
  formatHumidEvent(buf, sizeof(buf), e, s);
  TEST_ASSERT_NOT_NULL(strstr(buf, "HUMIDIFY CALL START (1 since boot): mean RH 59.0% <= 60.0% for 30 s"));
  e = HumidEvent{};
  e.type = HumidEvent::Type::Ineffective;
  e.vpChange = 0.0012f;
  formatHumidEvent(buf, sizeof(buf), e, s);
  TEST_ASSERT_NOT_NULL(strstr(buf, "HUMIDIFIER INEFFECTIVE: vapour pressure changed +0.0012 kPa in 60 s (needs +0.0117)"));
  Rig r;
  formatHumidText(buf, sizeof(buf), r.hc, r.now);
  TEST_ASSERT_EQUAL_STRING("humid idle | calls 0 runs 0", buf);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_call_after_full_persistence_both_profiles);
  RUN_TEST(test_no_call_above_trigger);
  RUN_TEST(test_persistence_restarts_after_a_break);
  RUN_TEST(test_release_at_setpoint_no_minimum_on);
  RUN_TEST(test_call_held_through_guard_pauses);
  RUN_TEST(test_persistence_only_outside_strips);
  RUN_TEST(test_needs_enough_fresh_sensors_and_scripted_suspends);
  RUN_TEST(test_rise_needed_is_half_a_point_at_setpoint);
  RUN_TEST(test_ineffective_off_flag_retry_clear_both_profiles);
  RUN_TEST(test_check_ignores_rh_moving_with_temperature);
  RUN_TEST(test_run_ending_inside_window_no_result);
  RUN_TEST(test_runs_counted);
  RUN_TEST(test_guard_holds_humidifier_when_ineffective);
  RUN_TEST(test_humidify_during_cooling_toggle);
  RUN_TEST(test_ceiling_beats_a_low_mean);
  RUN_TEST(test_event_and_status_text);
  return UNITY_END();
}
