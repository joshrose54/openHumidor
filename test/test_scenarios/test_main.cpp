// The scenarios (run 1, 2, 3, 4, 5, 6, 13, 15) played on the Mac against the real
// Controller, sensor health, requirement gate, fan monitor and cabinet model, with
// a fake clock and a simulated bus — exactly the logic the board runs. The
// simulation does Josh's part when a scenario prompts (unplugging probes, J2, the
// SDA short). Also proves a scenario really checks: a setting made wrong makes it FAIL.
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <initializer_list>
#include "wineador/scenario.h"
#include "wineador/controller.h"
#include "wineador/cabinet_model.h"
#include "wineador/sensor_health.h"
#include "wineador/sensor_table.h"
#include "wineador/fan_monitor.h"
#include "wineador/settings.h"
#include "wineador/temp_control.h"
#include "wineador/humid_control.h"
#include "wineador/fan_controller.h"

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
  TemperatureControl tc;
  HumidControl hc;
  FanController fan{20, 1000};
  uint8_t fanOut = 0;
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

  explicit Sim(Profile p) : s(settingsFor(p)) { verbose = getenv("SIMV") != nullptr; begin(); }
  void begin() {
    ctl.begin(s, 0);
    model.begin(ModelRates{});
    sc.begin(settingsFor(s.profile));   // the scenario expects the spec's values
    health.begin(s);
    gate.begin(s.safety.sensorRecoveryHoldMs);
    fanMon.begin(s.safety.fanStallRetryMs);
    tc.begin(s, 0);
    hc.begin(s, 0);
    fan = FanController(s.safety.fanMinDutyPct, s.safety.fanKickstartMs);
    fan.command(s.tuning.fanIdlePct, 0);
    fanOut = fan.output(0);
    fanSpinning = true;
    for (int i = 0; i < kNumSensors; i++) health.serialResult(i, BusResult::Ok, kSensors[i].serial);
    sweep();
  }

  // The fan runs only when driven; a stopped or unplugged fan restarts only on a kickstart.
  bool fanOk() const { return fanPlugged && fanSpinning && fanOut >= 20; }

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
    ControlInputs ci;
    ci.nowMs = now;
    ci.cab = in.cab;
    ci.readingsFresh = true;
    ci.live = live;
    ci.scriptedDemand = sc.running() && !sc.usesControl();
    const ControlOutput co = tc.update(ci);
    HumidInputs hi;
    hi.nowMs = now;
    hi.cab = in.cab;
    hi.readingsFresh = true;
    hi.live = live;
    hi.stripping = co.strippingActive;
    hi.scriptedDemand = ci.scriptedDemand;
    const HumidOutput ho = hc.update(hi);
    in.strippingActive = co.strippingActive;
    in.heaterIneffectiveHold = co.heaterIneffectiveHold;
    in.humidIneffectiveHold = ho.ineffectiveHold;
    Demand d = ci.scriptedDemand ? sc.demand() : co.demand;
    if (!ci.scriptedDemand) {
      d.humid = ho.demand;
      if (sc.running()) d.humid = d.humid || sc.demand().humid;   // run 21 "demands humidify"
    }
    if (co.fanPct != fan.target()) fan.command(co.fanPct, now);
    fanOut = fan.output(now);
    if (fanOut == 0) fanSpinning = false;
    else if (fanOut == 100 && fanPlugged) fanSpinning = true;
    live = ctl.pass(in, d);
    if (now - lastModel >= 1000) { model.step(now - lastModel, live); lastModel = now; }
    if (now - lastWindow >= 5000) {
      lastWindow = now;
      if (fanMon.window(now, fan.target() >= 20, fanOk() ? 420 : 0)) {
        kicks++;
        const uint8_t target = fan.target();     // as main: a stall kickstart
        fan.command(0, now);
        fan.output(now);
        fan.command(target, now);
        fanOut = fan.output(now);
        if (fanPlugged) fanSpinning = true;
      }
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
    v.fanOut = fanOut;
    v.fanTarget = fan.target();
    v.hc = &hc;
    sc.update(v, ctl, model, tc);
    for (const char* l; (l = sc.takeLine()) != nullptr;) {
      if (verbose) printf("%s\n", l);
      // Josh's hands
      if (strstr(l, "UNPLUG THE PLENUM FAN")) { fanPlugged = false; fanSpinning = false; }
      if (strstr(l, "PLUG J2 BACK IN")) fanPlugged = true;
      if (strstr(l, "UNPLUG THE CHAMBER SENSOR")) plugged[kChamberIndex] = false;
      if (strstr(l, "UNPLUG ch2")) plugged[0] = false;
      if (strstr(l, "UNPLUG ch3")) plugged[1] = false;
      if (strstr(l, "BACK IN NOW") && !strstr(l, "J2")) for (bool& p : plugged) p = true;
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

void test_run41_heater_two_cabinet_sensors() {
  Sim m(Profile::Bench);
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(41, minutes(15)) == ScenarioRunner::Result::Pass);
}

void test_run42_cooling_chamber_out() {
  Sim m(Profile::Bench);
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(42, minutes(15)) == ScenarioRunner::Result::Pass);
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

// ---------------------------------------------------------------- stage 4
void test_run7_deadband_passes() {
  Sim m(Profile::Bench);
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(7, minutes(15)) == ScenarioRunner::Result::Pass);
}

void test_run7_fails_if_deadband_wrong() {
  Sim m(Profile::Bench);
  m.s.tuning.coolingHysteresisC = 0.2f;     // the board starts cooling at 20.2: inside the dead zone
  m.begin();
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(7, minutes(15)) == ScenarioRunner::Result::Fail);
}

void test_run8_crossover_passes() {
  Sim m(Profile::Bench);
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(8, minutes(10)) == ScenarioRunner::Result::Pass);
  TEST_ASSERT_EQUAL_UINT32(2, m.tc.crossovers());
}

void test_run8_fails_if_crossover_short() {
  Sim m(Profile::Bench);
  m.s.tuning.crossoverDelayMs = seconds(20);
  m.begin();
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(8, minutes(10)) == ScenarioRunner::Result::Fail);
}

void test_run9_stripping_passes() {
  Sim m(Profile::Bench);
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(9, minutes(15)) == ScenarioRunner::Result::Pass);
}

void test_run9_fails_if_persistence_short() {
  Sim m(Profile::Bench);
  m.s.tuning.rhStripPersistMs = seconds(10);
  m.begin();
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(9, minutes(15)) == ScenarioRunner::Result::Fail);
}

void test_run10_floor_passes() {
  Sim m(Profile::Bench);
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(10, minutes(15)) == ScenarioRunner::Result::Pass);
}

void test_run17_ineffective_passes() {
  Sim m(Profile::Bench);
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(17, minutes(15)) == ScenarioRunner::Result::Pass);
  TEST_ASSERT_FALSE(m.tc.heaterIneffective());
  TEST_ASSERT_TRUE(m.tc.coolingIneffective());
}

void test_run17_fails_if_retry_wrong() {
  Sim m(Profile::Bench);
  m.s.tuning.heaterIneffectiveRetryMs = minutes(1);
  m.begin();
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(17, minutes(15)) == ScenarioRunner::Result::Fail);
}

void test_run23_fan_rules_passes() {
  Sim m(Profile::Bench);
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(23, minutes(15)) == ScenarioRunner::Result::Pass);
  TEST_ASSERT_EQUAL_INT(-1, m.tc.idleFanOverride());
}

void test_run23_fails_if_harvest_profile_wrong() {
  Sim m(Profile::Bench);
  m.s.tuning.fanCoolHarvestStartPct = 60;   // the board starts the harvest slow
  m.begin();
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(23, minutes(15)) == ScenarioRunner::Result::Fail);
}

void test_run8_after_heating_ran_last() {
  // On the board run 8 followed run 7, which ended with heating: its first cooling
  // start is itself a crossover. The scenario must still count only its own two.
  Sim m(Profile::Bench);
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(7, minutes(15)) == ScenarioRunner::Result::Pass);
  m.settle(minutes(2));
  TEST_ASSERT_TRUE(m.run(8, minutes(10)) == ScenarioRunner::Result::Pass);
}

void test_stage4_runs_back_to_back() {
  // Every stage 4 scenario, one after another, as on the board.
  Sim m(Profile::Bench);
  m.settle(seconds(31));
  for (int n : {7, 8, 9, 10, 17, 23, 8, 7}) {
    TEST_ASSERT_TRUE_MESSAGE(m.run(n, minutes(15)) == ScenarioRunner::Result::Pass, "a scenario failed in sequence");
    m.settle(minutes(2));
  }
}

// ---------------------------------------------------------------- stage 5
void test_run19_humidifier_guards_passes() {
  Sim m(Profile::Bench);
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(19, minutes(15)) == ScenarioRunner::Result::Pass);
}

void test_run19_fails_if_min_off_wrong() {
  Sim m(Profile::Bench);
  m.s.safety.humidMinOffMs = seconds(10);
  m.begin();
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(19, minutes(15)) == ScenarioRunner::Result::Fail);
}

void test_run20_ceiling_and_sensors_passes() {
  Sim m(Profile::Bench);
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(20, minutes(15)) == ScenarioRunner::Result::Pass);
}

void test_run20_fails_if_ceiling_wrong() {
  Sim m(Profile::Bench);
  m.s.safety.rhHardCeilingPct = 75.0f;
  m.begin();
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(20, minutes(15)) == ScenarioRunner::Result::Fail);
}

void test_run21_exclusivity_passes() {
  Sim m(Profile::Bench);
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(21, minutes(15)) == ScenarioRunner::Result::Pass);
  TEST_ASSERT_FALSE(m.ctl.humidifyDuringCooling());     // put back afterwards
}

void test_run21_fails_if_setting_already_on() {
  Sim m(Profile::Bench);
  m.s.tuning.humidifyDuringCooling = true;              // the board allows it in a temperature run
  m.begin();
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(21, minutes(15)) == ScenarioRunner::Result::Fail);
}

void test_run22_humidifier_ineffective_passes() {
  Sim m(Profile::Bench);
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(22, minutes(15)) == ScenarioRunner::Result::Pass);
  TEST_ASSERT_FALSE(m.hc.ineffective());
}

void test_run22_fails_if_window_wrong() {
  Sim m(Profile::Bench);
  m.s.tuning.humidIneffectiveWindowMs = seconds(30);
  m.begin();
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(22, minutes(15)) == ScenarioRunner::Result::Fail);
}

void test_stage5_runs_back_to_back_after_stage4() {
  Sim m(Profile::Bench);
  m.settle(seconds(31));
  for (int n : {23, 19, 20, 21, 22, 9, 19, 22}) {
    TEST_ASSERT_TRUE_MESSAGE(m.run(n, minutes(15)) == ScenarioRunner::Result::Pass, "a scenario failed in sequence");
    m.settle(minutes(2));
  }
  m.settle(minutes(3));
  TEST_ASSERT_FALSE(m.live.humid || m.live.cool || m.live.heat);   // settled cabinet: everything idle
}

void test_idle_control_asks_nothing_when_settled() {
  // After a stage 4 scenario the model is back at 20 C / 62 %: the control idles.
  Sim m(Profile::Bench);
  m.settle(seconds(31));
  TEST_ASSERT_TRUE(m.run(8, minutes(10)) == ScenarioRunner::Result::Pass);
  m.settle(minutes(5));
  TEST_ASSERT_FALSE(m.live.cool || m.live.heat);
  TEST_ASSERT_FALSE(m.tc.coolCall() || m.tc.heatCall() || m.tc.stripping());
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
  TEST_ASSERT_FALSE(m.sc.start(77, 0, m.model));
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
  RUN_TEST(test_run41_heater_two_cabinet_sensors);
  RUN_TEST(test_run42_cooling_chamber_out);
  RUN_TEST(test_run5_divergence_passes_both_profiles);
  RUN_TEST(test_run6_bench_passes);
  RUN_TEST(test_run13_passes_and_ends_cooling);
  RUN_TEST(test_run13_waits_for_a_running_load);
  RUN_TEST(test_run15_bus_fault_passes);
  RUN_TEST(test_run7_deadband_passes);
  RUN_TEST(test_run7_fails_if_deadband_wrong);
  RUN_TEST(test_run8_crossover_passes);
  RUN_TEST(test_run8_fails_if_crossover_short);
  RUN_TEST(test_run9_stripping_passes);
  RUN_TEST(test_run9_fails_if_persistence_short);
  RUN_TEST(test_run10_floor_passes);
  RUN_TEST(test_run17_ineffective_passes);
  RUN_TEST(test_run17_fails_if_retry_wrong);
  RUN_TEST(test_run23_fan_rules_passes);
  RUN_TEST(test_run23_fails_if_harvest_profile_wrong);
  RUN_TEST(test_run8_after_heating_ran_last);
  RUN_TEST(test_stage4_runs_back_to_back);
  RUN_TEST(test_run19_humidifier_guards_passes);
  RUN_TEST(test_run19_fails_if_min_off_wrong);
  RUN_TEST(test_run20_ceiling_and_sensors_passes);
  RUN_TEST(test_run20_fails_if_ceiling_wrong);
  RUN_TEST(test_run21_exclusivity_passes);
  RUN_TEST(test_run21_fails_if_setting_already_on);
  RUN_TEST(test_run22_humidifier_ineffective_passes);
  RUN_TEST(test_run22_fails_if_window_wrong);
  RUN_TEST(test_stage5_runs_back_to_back_after_stage4);
  RUN_TEST(test_idle_control_asks_nothing_when_settled);
  RUN_TEST(test_stop_ends_with_no_demand);
  RUN_TEST(test_unknown_scenario_not_started);
  return UNITY_END();
}
