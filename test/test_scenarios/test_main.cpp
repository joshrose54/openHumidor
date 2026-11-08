// The scenarios (run 1, 2, 3, 4, 5, 6, 13, 15) played on the Mac against the real
// Controller, sensor health, requirement gate, fan monitor and cabinet model, with
// a fake clock and a simulated bus — exactly the logic the board runs. The
// simulation does Josh's part when a scenario prompts (unplugging probes, J2, the
// SDA short). Also proves a scenario really checks: a setting made wrong makes it FAIL.
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <initializer_list>
#include "wineador/scenario.h"
#include "wineador/controller.h"
#include "wineador/cabinet_model.h"
#include "wineador/sensor_health.h"
#include "wineador/sensor_table.h"
#include "wineador/fan_monitor.h"
#include "wineador/settings.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

namespace {

struct Sim {
  Settings s;              // what the board runs with
  Controller ctl;
  CabinetModel model;
  ScenarioRunner sc;
  SensorHealth health;
  RequirementGate gate;
  FanMonitor fanMon;
  LoadOutputs live;
  uint32_t now = 0, lastModel = 0, lastSweep = 0, lastWindow = 0;
  // The simulated hardware
  bool plugged[kNumSensors] = {true, true, true, true, true, true};
  int shorted = -1;            // sensor whose SDA is shorted to GND
  bool fanPlugged = true;
  bool fanSpinning = true;     // an unplugged fan only restarts when kicked
  uint32_t busStuck = 0;
  int kicks = 0;
  bool verbose = false;

  explicit Sim(Profile p) : s(settingsFor(p)) { begin(); }
  void begin() {
    ctl.begin(s, 0);
    model.begin(ModelRates{});
    sc.begin(settingsFor(s.profile));   // the scenario expects the spec's values
    health.begin(s);
    gate.begin(s.safety.sensorRecoveryHoldMs);
    fanMon.begin(s.safety.fanStallRetryMs);
    for (int i = 0; i < kNumSensors; i++) health.serialResult(i, BusResult::Ok, kSensors[i].serial);
    sweep();
  }

  bool fanOk() const { return fanPlugged && fanSpinning; }

  // One sensor sweep: serials asked for, then every sensor, as the driver does.
  void sweep() {
    for (int i = 0; i < kNumSensors; i++)
      if (health.wantsSerial(i))
        health.serialResult(i, plugged[i] && shorted != i ? BusResult::Ok : BusResult::NoAck, kSensors[i].serial);
    Readings r;
    for (int i = 0; i < kNumSensors; i++) {
      if (shorted == i) { r.s[i].bus = BusResult::BusStuck; busStuck++; continue; }
      r.s[i].bus = plugged[i] ? BusResult::Ok : BusResult::NoAck;
    }
    applyScripted(r, model);
    health.update(r, now);
    lastSweep = now;
  }

  void step(uint32_t dt) {
    now += dt;
    const int cabOk = health.healthyCabinet();
    gate.update(health.chamberHealthy() && cabOk >= 3, cabOk >= 3, now);
    PassInputs in;
    in.nowMs = now;
    in.fanOk = fanOk();
    in.readingsFresh = true;
    in.cab = health.summary();
    in.sensorHoldTemp = gate.tempHolding();
    in.sensorHoldHumid = gate.humidHolding();
    in.live = live;
    Demand d = sc.running() ? sc.demand() : Demand{};
    live = ctl.pass(in, d);
    if (now - lastModel >= 1000) { model.step(now - lastModel, live); lastModel = now; }
    if (now - lastWindow >= 5000) {
      lastWindow = now;
      if (fanMon.window(now, true, fanOk() ? 420 : 0)) { kicks++; if (fanPlugged) fanSpinning = true; }
    }
    bool swept = false;
    if (now - lastSweep >= 15000) { sweep(); swept = true; }

    ScenarioView v;
    v.nowMs = now;
    v.live = live;
    v.fanOk = fanOk();
    v.sweepDone = swept;
    v.health = &health;
    v.fan = &fanMon;
    v.busStuck = busStuck;
    sc.update(v, ctl, model);
    for (const char* l; (l = sc.takeLine()) != nullptr;) {
      if (verbose) printf("%s\n", l);
      // Josh's hands
      if (strstr(l, "UNPLUG THE PLENUM FAN")) { fanPlugged = false; fanSpinning = false; }
      if (strstr(l, "PLUG J2 BACK IN")) fanPlugged = true;
      if (strstr(l, "UNPLUG THE CHAMBER SENSOR")) plugged[kChamberIndex] = false;
      if (strstr(l, "UNPLUG ch2")) plugged[0] = false;
      if (strstr(l, "UNPLUG ch3")) plugged[1] = false;
      if (strstr(l, "BACK IN NOW") && strstr(l, "ch6")) for (bool& p : plugged) p = true;
      if (strstr(l, "SHORT ONE SENSOR'S SDA")) shorted = 2;
      if (strstr(l, "REMOVE THE SHORT")) shorted = -1;
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
  Sim m(Profile::Bench);
  m.s.safety.compressorMinOnMs = seconds(5);   // the board's setting differs from the spec's
  m.begin();
  TEST_ASSERT_TRUE(m.run(1, minutes(20)) == ScenarioRunner::Result::Fail);
}

void test_run2_bench_passes() {
  Sim m(Profile::Bench);
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(2, minutes(20)) == ScenarioRunner::Result::Pass);
  TEST_ASSERT_TRUE(m.fanPlugged);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(1, m.kicks);    // the fan came back on a kickstart
}

void test_run2_fails_if_the_recovery_hold_is_short() {
  Sim m(Profile::Bench);
  m.s.safety.sensorRecoveryHoldMs = seconds(5);   // the board holds 5 s; the spec says 30 s
  m.begin();
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(2, minutes(20)) == ScenarioRunner::Result::Fail);
}

void test_run3_bench_passes() {
  Sim m(Profile::Bench);
  TEST_ASSERT_TRUE(m.run(3, minutes(10)) == ScenarioRunner::Result::Pass);
}

void test_run4_bench_passes() {
  Sim m(Profile::Bench);
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(4, minutes(20)) == ScenarioRunner::Result::Pass);
}

void test_run5_divergence_passes_both_profiles() {
  for (Profile p : {Profile::Bench, Profile::Real}) {
    Sim m(p);
    TEST_ASSERT_TRUE(m.run(5, minutes(20), 20) == ScenarioRunner::Result::Pass);
  }
}

void test_run6_bench_passes() {
  Sim m(Profile::Bench);
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(6, minutes(10)) == ScenarioRunner::Result::Pass);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(2, m.kicks);
}

void test_run13_passes_and_ends_cooling() {
  Sim m(Profile::Bench);
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(13, minutes(5), 1) == ScenarioRunner::Result::Pass);
  TEST_ASSERT_FALSE(m.live.relayHeating);
}

void test_run13_waits_for_a_running_load() {
  Sim m(Profile::Bench);
  m.sc.start(3, m.now, m.model);
  while (!m.live.heat) m.step(5);
  m.sc.stop(m.now, m.model);
  TEST_ASSERT_TRUE(m.run(13, minutes(5), 1) == ScenarioRunner::Result::Pass);
}

void test_run15_bus_fault_passes() {
  Sim m(Profile::Bench);
  m.settle(seconds(20));
  TEST_ASSERT_TRUE(m.run(15, minutes(15)) == ScenarioRunner::Result::Pass);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2, m.busStuck);
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
  RUN_TEST(test_run2_bench_passes);
  RUN_TEST(test_run2_fails_if_the_recovery_hold_is_short);
  RUN_TEST(test_run3_bench_passes);
  RUN_TEST(test_run4_bench_passes);
  RUN_TEST(test_run5_divergence_passes_both_profiles);
  RUN_TEST(test_run6_bench_passes);
  RUN_TEST(test_run13_passes_and_ends_cooling);
  RUN_TEST(test_run13_waits_for_a_running_load);
  RUN_TEST(test_run15_bus_fault_passes);
  RUN_TEST(test_stop_ends_with_no_demand);
  RUN_TEST(test_unknown_scenario_not_started);
  return UNITY_END();
}
