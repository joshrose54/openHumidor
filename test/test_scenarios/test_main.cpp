// The stage 2 scenarios (run 1, 2, 3, 13) played on the Mac against the real
// Controller and cabinet model, with a fake clock — exactly the code the board
// runs. Also proves a scenario really checks: a guard set wrong makes it FAIL.
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include "wineador/scenario.h"
#include "wineador/controller.h"
#include "wineador/cabinet_model.h"
#include "wineador/settings.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

namespace {

struct Sim {
  Settings s;              // what the controller runs with
  Controller ctl;
  CabinetModel model;
  ScenarioRunner sc;
  Readings bus;
  LoadOutputs live;
  uint32_t now = 0, lastModel = 0;
  bool fanPlugged = true;
  uint32_t fanReturnsAt = 0;
  uint32_t fanDelayMs = 3000;  // simulated: the tach reads again this long after J2 goes back in
  bool verbose = false;
  int kicks = 0;

  explicit Sim(Profile p) : s(settingsFor(p)) {
    ctl.begin(s, 0);
    model.begin(ModelRates{});
    sc.begin(s);
    for (int i = 0; i < kNumSensors; i++) bus.s[i].bus = BusResult::Ok;
  }
  bool fanOk() const { return fanPlugged && now >= fanReturnsAt; }

  void step(uint32_t dt) {
    now += dt;
    Readings r = bus;
    applyScripted(r, model);
    PassInputs in;
    in.nowMs = now;
    in.fanOk = fanOk();
    in.readingsFresh = true;
    in.cab = summarise(r);
    in.live = live;
    Demand d = sc.running() ? sc.demand() : Demand{};
    live = ctl.pass(in, d);
    if (now - lastModel >= 1000) { model.step(now - lastModel, live); lastModel = now; }
    ScenarioView v;
    v.nowMs = now;
    v.live = live;
    v.fanOk = fanOk();
    sc.update(v, ctl, model);
    if (sc.takeFanKick()) kicks++;
    for (const char* l; (l = sc.takeLine()) != nullptr;) {
      if (verbose) printf("%s\n", l);
      // Josh's hands, for run 2
      if (strstr(l, "UNPLUG THE PLENUM FAN")) fanPlugged = false;
      if (strstr(l, "PLUG J2 BACK IN")) { fanPlugged = true; fanReturnsAt = now + fanDelayMs; }
    }
  }
  ScenarioRunner::Result run(int n, uint32_t maxMs, uint32_t dt = 5) {
    TEST_ASSERT_TRUE(sc.start(n, now, model));
    const uint32_t t0 = now;
    while (sc.running() && now - t0 < maxMs) step(dt);
    return sc.result();
  }
  void settle(uint32_t ms) { const uint32_t t0 = now; while (now - t0 < ms) step(5); }
};

}  // namespace

void test_run1_bench_passes() {
  Sim m(Profile::Bench);
  TEST_ASSERT_TRUE(m.run(1, minutes(20)) == ScenarioRunner::Result::Pass);
}

void test_run1_real_profile_passes() {
  Sim m(Profile::Real);
  TEST_ASSERT_TRUE(m.run(1, hours(4), 20) == ScenarioRunner::Result::Pass);
}

void test_run1_fails_if_a_guard_is_wrong() {
  // The scenario expects the bench min on (10 s); the controller is given 5 s.
  Sim m(Profile::Bench);
  m.s.safety.compressorMinOnMs = seconds(5);
  m.ctl.begin(m.s, 0);
  TEST_ASSERT_TRUE(m.run(1, minutes(20)) == ScenarioRunner::Result::Fail);
}

void test_run1_fails_if_max_run_is_wrong() {
  Sim m(Profile::Bench);
  m.s.safety.heaterMaxRunMs = minutes(3);
  m.ctl.begin(m.s, 0);
  TEST_ASSERT_TRUE(m.run(1, minutes(20)) == ScenarioRunner::Result::Fail);
}

void test_run2_bench_passes() {
  Sim m(Profile::Bench);
  TEST_ASSERT_TRUE(m.run(2, minutes(10)) == ScenarioRunner::Result::Pass);
  TEST_ASSERT_TRUE(m.fanPlugged);
}

void test_run2_fan_slow_to_return_gets_kicks() {
  Sim m(Profile::Bench);
  m.fanDelayMs = 25000;   // J2 back in, but the tach only reads 25 s later
  TEST_ASSERT_TRUE(m.run(2, minutes(10)) == ScenarioRunner::Result::Pass);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(2, m.kicks);
}

void test_run3_bench_passes() {
  Sim m(Profile::Bench);
  TEST_ASSERT_TRUE(m.run(3, minutes(10)) == ScenarioRunner::Result::Pass);
}

void test_run13_passes_and_ends_cooling() {
  Sim m(Profile::Bench);
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(13, minutes(5), 1) == ScenarioRunner::Result::Pass);
  TEST_ASSERT_FALSE(m.live.relayHeating);
}

void test_run13_waits_for_a_running_load() {
  // A heater left running by run 3's hold: run 13 waits, then passes
  Sim m(Profile::Bench);
  m.sc.start(3, m.now, m.model);
  while (!m.live.heat) m.step(5);
  m.sc.stop(m.now, m.model);
  TEST_ASSERT_TRUE(m.run(13, minutes(5), 1) == ScenarioRunner::Result::Pass);
}

void test_stop_ends_with_no_demand() {
  Sim m(Profile::Bench);
  m.sc.start(1, m.now, m.model);
  m.settle(seconds(40));
  m.sc.stop(m.now, m.model);
  TEST_ASSERT_TRUE(m.sc.result() == ScenarioRunner::Result::Stopped);
  const Demand& d = m.sc.demand();
  TEST_ASSERT_FALSE(d.cool || d.heat || d.humid);
}

void test_unknown_scenario_not_started() {
  Sim m(Profile::Bench);
  TEST_ASSERT_FALSE(m.sc.start(7, 0, m.model));
  TEST_ASSERT_FALSE(m.sc.running());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_run1_bench_passes);
  RUN_TEST(test_run1_real_profile_passes);
  RUN_TEST(test_run1_fails_if_a_guard_is_wrong);
  RUN_TEST(test_run1_fails_if_max_run_is_wrong);
  RUN_TEST(test_run2_bench_passes);
  RUN_TEST(test_run2_fan_slow_to_return_gets_kicks);
  RUN_TEST(test_run3_bench_passes);
  RUN_TEST(test_run13_passes_and_ends_cooling);
  RUN_TEST(test_run13_waits_for_a_running_load);
  RUN_TEST(test_stop_ends_with_no_demand);
  RUN_TEST(test_unknown_scenario_not_started);
  return UNITY_END();
}
