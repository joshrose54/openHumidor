// Runtime guards (spec 4.4) against a fake clock — B3 tests 1 and 2 on the Mac,
// for the REAL profile's timers as well as the bench's.
#include <unity.h>
#include <initializer_list>
#include "wineador/guards.h"
#include "wineador/settings.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

namespace {

CabinetSummary healthy() {
  CabinetSummary c;
  c.sensorsOk = 6;
  c.cabinetOk = 4;
  c.meanC = c.minC = c.maxC = 20.0f;
  c.meanRhPct = c.maxRhPct = 62.0f;
  c.chamberOk = true;
  c.chamberC = 20.0f;
  c.ambientOk = true;
  return c;
}

// A small rig: the guards, a clock, and "live" following the decisions at once
// (the 20 ms relay settle is tested separately, in test_interlock).
struct Rig {
  Settings s;
  RuntimeGuards g;
  GuardInputs in;
  LoadOutputs live;
  uint32_t now;
  GuardDecisions d;

  // The guards "boot" 10 minutes before the rig's clock starts, so the boot-time
  // min off (4.4, worst case) is long past; the boot hold has its own tests.
  explicit Rig(Profile p, uint32_t start = minutes(10)) : s(settingsFor(p)), now(start) {
    g.begin(s, now - minutes(10));
    in.bootHoldActive = false;   // most tests start after the hold; boot hold has its own test
    in.fanOk = true;
    in.readingsFresh = true;
    in.cab = healthy();
  }
  const GuardDecisions& step(const Demand& dm, uint32_t dtMs = 10) {
    now += dtMs;
    in.nowMs = now;
    d = g.evaluate(in, dm, live);
    live.cool = d.cool.run;
    live.heat = d.heat.run;
    live.relayHeating = d.heat.run;
    live.humid = d.humid.run;
    return d;
  }
  // Steps until the load's "live" changes, or maxMs passes. Returns the time it took.
  uint32_t until(const Demand& dm, bool LoadOutputs::*load, bool want, uint32_t maxMs, uint32_t dtMs = 10) {
    const uint32_t t0 = now;
    while (live.*load != want && now - t0 < maxMs) step(dm, dtMs);
    return now - t0;
  }
};

Demand only(bool c, bool h, bool u) { Demand d; d.cool = c; d.heat = h; d.humid = u; return d; }
const Demand kCool = only(true, false, false);
const Demand kHeat = only(false, true, false);
const Demand kHumid = only(false, false, true);
const Demand kNone = only(false, false, false);

}  // namespace

// ---------------------------------------------------------------- boot
void test_boot_hold_holds_all_three() {
  for (Profile p : {Profile::Real, Profile::Bench}) {
    Rig r(p);
    r.in.bootHoldActive = true;
    r.step(only(true, true, true));
    TEST_ASSERT_FALSE(r.d.cool.run);
    TEST_ASSERT_FALSE(r.d.heat.run);
    TEST_ASSERT_FALSE(r.d.humid.run);
    TEST_ASSERT_TRUE(r.d.cool.block == GuardBlock::BootHold);
    TEST_ASSERT_TRUE(r.d.heat.block == GuardBlock::BootHold);
    TEST_ASSERT_TRUE(r.d.humid.block == GuardBlock::BootHold);
  }
}

// 4.4 "Timing state at boot": every load counts as having just stopped, so the
// boot hold, min off and crossover delay all run from boot. Each load is asked
// for from boot; the time it first runs must be exactly the longest of those.
//   real:  compressor 10 min (crossover), heater 10 min (crossover), humidifier 5 min
//   bench: all three 30 s (boot hold = crossover = compressor min off)
static uint32_t firstStartAfterBoot(Profile p, const Demand& dm, bool LoadOutputs::*load) {
  Rig r(p);
  r.g.begin(r.s, r.now);            // a real boot, now
  const uint32_t boot = r.now;
  while (!(r.live.*load) && r.now - boot < hours(1)) {
    r.in.bootHoldActive = (r.now + 10 - boot) < r.s.safety.bootHoldMs;   // what BootHold reports
    r.step(dm);
  }
  return r.now - boot;
}

void test_boot_counts_as_just_stopped_real_profile() {
  TEST_ASSERT_EQUAL_UINT32(minutes(10), firstStartAfterBoot(Profile::Real, kCool, &LoadOutputs::cool));
  TEST_ASSERT_EQUAL_UINT32(minutes(10), firstStartAfterBoot(Profile::Real, kHeat, &LoadOutputs::heat));
  TEST_ASSERT_EQUAL_UINT32(minutes(5),  firstStartAfterBoot(Profile::Real, kHumid, &LoadOutputs::humid));
}

void test_boot_counts_as_just_stopped_bench_profile() {
  TEST_ASSERT_EQUAL_UINT32(seconds(30), firstStartAfterBoot(Profile::Bench, kCool, &LoadOutputs::cool));
  TEST_ASSERT_EQUAL_UINT32(seconds(30), firstStartAfterBoot(Profile::Bench, kHeat, &LoadOutputs::heat));
  TEST_ASSERT_EQUAL_UINT32(seconds(30), firstStartAfterBoot(Profile::Bench, kHumid, &LoadOutputs::humid));
}

void test_boot_reason_shown_after_hold_is_crossover() {
  // Real profile, 6 min after boot: hold over, but the heater is held by the crossover delay, 4 min left
  Rig r(Profile::Real);
  r.g.begin(r.s, r.now);
  r.in.bootHoldActive = false;
  r.step(kHeat, minutes(6));
  TEST_ASSERT_FALSE(r.live.heat);
  TEST_ASSERT_TRUE(r.d.heat.block == GuardBlock::Crossover);
  TEST_ASSERT_EQUAL_UINT32(minutes(4), r.d.heat.waitMs);
}

void test_settings_problem_turns_everything_off() {
  Rig r(Profile::Bench);
  r.step(only(false, true, true));
  TEST_ASSERT_TRUE(r.live.heat && r.live.humid);
  r.in.settingsOk = false;
  r.step(only(false, true, true));
  TEST_ASSERT_FALSE(r.live.heat || r.live.humid);
  TEST_ASSERT_TRUE(r.d.heat.block == GuardBlock::SettingsProblem);
}

// ---------------------------------------------------------------- test 1: timing
// The pattern of B3 test 1 for one load: start; drop the demand at once (min on);
// ask again at once (min off); keep asking (max run).
static void timingPattern(Profile p, bool LoadOutputs::*load, const Demand& on,
                          uint32_t minOn, uint32_t minOff, uint32_t maxRun) {
  Rig r(p);
  r.step(on);
  TEST_ASSERT_TRUE(r.live.*load);

  // min on: demand gone at once, stays on for exactly min on (10 ms steps)
  uint32_t ran = r.until(kNone, load, false, maxRun * 2);
  TEST_ASSERT_FALSE(r.live.*load);
  if (minOn == 0) TEST_ASSERT_LESS_OR_EQUAL_UINT32(10, ran);
  else {
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(minOn, ran);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(minOn + 20, ran);
  }

  // min off: asked again at once, stays off for exactly min off
  uint32_t gap = r.until(on, load, true, maxRun * 2);
  TEST_ASSERT_TRUE(r.live.*load);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(minOff, gap);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(minOff + 20, gap);

  // max run: keeps asking, stops at exactly max run, reason max-run
  ran = r.until(on, load, false, maxRun * 2);
  TEST_ASSERT_FALSE(r.live.*load);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(maxRun, ran);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(maxRun + 20, ran);

  // then rests for at least min off, even though still asked
  gap = r.until(on, load, true, maxRun * 2);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(minOff, gap);
}

void test1_compressor_timing_real_and_bench() {
  for (Profile p : {Profile::Real, Profile::Bench}) {
    const SafetySettings f = settingsFor(p).safety;
    timingPattern(p, &LoadOutputs::cool, kCool, f.compressorMinOnMs, f.compressorMinOffMs, f.compressorMaxRunMs);
  }
}

void test1_heater_timing_real_and_bench() {
  for (Profile p : {Profile::Real, Profile::Bench}) {
    const SafetySettings f = settingsFor(p).safety;
    timingPattern(p, &LoadOutputs::heat, kHeat, f.heaterMinOnMs, f.heaterMinOffMs, f.heaterMaxRunMs);
  }
}

void test1_humidifier_timing_real_and_bench() {
  for (Profile p : {Profile::Real, Profile::Bench}) {
    const SafetySettings f = settingsFor(p).safety;
    timingPattern(p, &LoadOutputs::humid, kHumid, 0 /* no min on */, f.humidMinOffMs, f.humidMaxRunMs);
  }
}

void test1_rapid_demand_flapping_never_beats_the_timers() {
  // Demand toggles every 50 ms for 12 minutes of bench time. The compressor may
  // never run shorter than min on, nor restart sooner than min off.
  Rig r(Profile::Bench);
  const SafetySettings f = r.s.safety;
  bool was = false;
  uint32_t onAt = 0, offAt = 0;
  bool everOff = false;
  for (uint32_t i = 0; i < 72000; i++) {
    r.step(((i / 5) % 2) ? kCool : kNone);
    if (r.live.cool && !was) {
      if (everOff) TEST_ASSERT_GREATER_OR_EQUAL_UINT32(f.compressorMinOffMs, r.now - offAt);
      onAt = r.now;
    }
    if (!r.live.cool && was) {
      TEST_ASSERT_GREATER_OR_EQUAL_UINT32(f.compressorMinOnMs, r.now - onAt);
      offAt = r.now;
      everOff = true;
    }
    was = r.live.cool;
  }
  TEST_ASSERT_TRUE(everOff);
}

// ---------------------------------------------------------------- crossover
void test_crossover_both_ways_real_and_bench() {
  for (Profile p : {Profile::Real, Profile::Bench}) {
    Rig r(p);
    r.step(kNone, minutes(15));   // every load's boot-time min off long past
    const uint32_t xo = r.s.tuning.crossoverDelayMs;
    // compressor stops -> heater blocked for the crossover delay
    r.step(kCool);
    r.until(kNone, &LoadOutputs::cool, false, minutes(60));
    r.step(kHeat);
    TEST_ASSERT_FALSE(r.live.heat);
    TEST_ASSERT_TRUE(r.d.heat.block == GuardBlock::Crossover);
    uint32_t waited = 10 + r.until(kHeat, &LoadOutputs::heat, true, minutes(60));
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(xo, waited);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(xo + 30, waited);
    // heater stops -> compressor blocked for the crossover delay
    r.until(kNone, &LoadOutputs::heat, false, minutes(60));
    r.step(kCool);
    TEST_ASSERT_FALSE(r.live.cool);
    TEST_ASSERT_TRUE(r.d.cool.block == GuardBlock::Crossover);
    waited = 10 + r.until(kCool, &LoadOutputs::cool, true, minutes(60));
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(xo, waited);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(xo + 30, waited);
  }
}

void test_both_asked_cooling_first_heater_waits() {
  Rig r(Profile::Bench);
  r.step(only(true, true, false));
  TEST_ASSERT_TRUE(r.live.cool);
  TEST_ASSERT_FALSE(r.live.heat);
  TEST_ASSERT_TRUE(r.d.heat.block == GuardBlock::OtherLoad);
  for (int i = 0; i < 1000; i++) {   // never both
    r.step(only(true, true, false));
    TEST_ASSERT_FALSE(r.live.cool && r.live.heat);
  }
}

// ---------------------------------------------------------------- test 2: heater guards
// Each guard, tripped while the heater runs, turns it off on that same pass —
// including during its min on.
static void heaterTrip(void (*trip)(Rig&), GuardBlock expected) {
  for (Profile p : {Profile::Real, Profile::Bench}) {
    Rig r(p);
    r.step(kHeat);
    TEST_ASSERT_TRUE(r.live.heat);
    r.step(kHeat, 1000);            // well inside min on
    TEST_ASSERT_TRUE(r.live.heat);
    trip(r);
    r.step(kHeat);
    TEST_ASSERT_FALSE(r.live.heat);
    TEST_ASSERT_EQUAL_STRING(guardBlockName(expected), guardBlockName(r.d.heat.block));
  }
}

void test2_cabinet_ceiling() {
  heaterTrip([](Rig& r) { r.in.cab.maxC = r.s.user.targetTempC + 3.01f; }, GuardBlock::CabinetCeiling);
  // exactly setpoint + 3 is not above it
  Rig r(Profile::Bench);
  r.in.cab.maxC = r.s.user.targetTempC + 3.0f;
  r.step(kHeat);
  TEST_ASSERT_TRUE(r.live.heat);
}
void test2_chamber_ceiling() {
  heaterTrip([](Rig& r) { r.in.cab.chamberC = 30.01f; }, GuardBlock::ChamberCeiling);
}
void test2_fan() {
  heaterTrip([](Rig& r) { r.in.fanOk = false; }, GuardBlock::Fan);
}
void test2_chamber_sensor_unhealthy() {
  heaterTrip([](Rig& r) { r.in.cab.chamberOk = false; }, GuardBlock::Sensors);
}
void test2_too_few_cabinet_sensors() {
  heaterTrip([](Rig& r) { r.in.cab.cabinetOk = 2; }, GuardBlock::Sensors);
}
void test2_stale_readings() {
  heaterTrip([](Rig& r) { r.in.readingsFresh = false; }, GuardBlock::Sensors);
}
void test2_heater_disabled() {
  heaterTrip([](Rig& r) { r.s.user.heaterEnable = false; r.g.begin(r.s, r.now - minutes(10)); r.live.heat = false; },
             GuardBlock::Disabled);
}

void test_three_of_four_cabinet_is_enough() {
  Rig r(Profile::Bench);
  r.in.cab.cabinetOk = 3;
  r.step(kHeat);
  TEST_ASSERT_TRUE(r.live.heat);
}

// ---------------------------------------------------------------- compressor
void test_compressor_stops_for_sensors_and_fan() {
  Rig r(Profile::Bench);
  r.step(kCool);
  TEST_ASSERT_TRUE(r.live.cool);
  r.in.cab.chamberOk = false;
  r.step(kCool);
  TEST_ASSERT_FALSE(r.live.cool);
  TEST_ASSERT_TRUE(r.d.cool.block == GuardBlock::Sensors);

  Rig q(Profile::Bench);
  q.step(kCool);
  q.in.fanOk = false;
  q.step(kCool);
  TEST_ASSERT_FALSE(q.live.cool);
  TEST_ASSERT_TRUE(q.d.cool.block == GuardBlock::Fan);
}

// ---------------------------------------------------------------- humidifier
void test_humidifier_rh_ceiling_at_70() {
  Rig r(Profile::Bench);
  r.in.cab.maxRhPct = 69.9f;
  r.step(kHumid);
  TEST_ASSERT_TRUE(r.live.humid);
  r.in.cab.maxRhPct = 70.0f;      // at or above: off immediately
  r.step(kHumid);
  TEST_ASSERT_FALSE(r.live.humid);
  TEST_ASSERT_TRUE(r.d.humid.block == GuardBlock::RhCeiling);
}

void test_humidifier_keeps_running_with_chamber_failed() {
  Rig r(Profile::Bench);
  r.step(kHumid);
  r.in.cab.chamberOk = false;     // test 4's criterion: the humidifier doesn't need the chamber
  r.step(kHumid);
  TEST_ASSERT_TRUE(r.live.humid);
  r.in.cab.cabinetOk = 2;         // ...but stops below 3 of 4 cabinet
  r.step(kHumid);
  TEST_ASSERT_FALSE(r.live.humid);
  TEST_ASSERT_TRUE(r.d.humid.block == GuardBlock::Sensors);
}

void test_humidifier_needs_no_fan() {
  Rig r(Profile::Bench);
  r.in.fanOk = false;
  r.step(kHumid);
  TEST_ASSERT_TRUE(r.live.humid);
}

void test_humidifier_blocked_during_compressor_run_unless_enabled() {
  Rig r(Profile::Bench);
  r.step(only(true, false, true));
  TEST_ASSERT_TRUE(r.live.cool);
  TEST_ASSERT_FALSE(r.live.humid);
  TEST_ASSERT_TRUE(r.d.humid.block == GuardBlock::CompressorRun);

  Rig q(Profile::Bench);
  q.s.tuning.humidifyDuringCooling = true;
  q.g.begin(q.s, q.now - minutes(10));
  q.step(only(true, false, true));
  TEST_ASSERT_TRUE(q.live.cool && q.live.humid);
}

void test_humidifier_never_while_stripping() {
  Rig r(Profile::Bench);
  r.step(kHumid);
  r.in.strippingActive = true;
  r.step(kHumid);
  TEST_ASSERT_FALSE(r.live.humid);
  TEST_ASSERT_TRUE(r.d.humid.block == GuardBlock::Stripping);
}

// ---------------------------------------------------------------- relay exercise
void test_exercise_waiting_lets_a_running_load_finish_but_blocks_starts() {
  Rig r(Profile::Bench);
  r.step(kCool);
  TEST_ASSERT_TRUE(r.live.cool);
  r.in.exerciseWaiting = true;
  r.step(kCool);
  TEST_ASSERT_TRUE(r.live.cool);    // carries on
  r.step(kHeat);                    // can't start while the compressor runs, nor while waiting
  r.until(kNone, &LoadOutputs::cool, false, minutes(5));
  r.step(kCool, minutes(5));        // min off long past
  TEST_ASSERT_FALSE(r.live.cool);
  TEST_ASSERT_TRUE(r.d.cool.block == GuardBlock::RelayExercise);
}

void test_exercise_cycling_holds_both_off_humidifier_free() {
  Rig r(Profile::Bench);
  r.in.exerciseCycling = true;
  r.step(only(true, false, true));
  TEST_ASSERT_FALSE(r.live.cool);
  TEST_ASSERT_TRUE(r.d.cool.block == GuardBlock::RelayExercise);
  TEST_ASSERT_TRUE(r.live.humid);   // not behind K1
}

// ---------------------------------------------------------------- millis wrap
void test_timing_across_millis_wrap() {
  Rig r(Profile::Bench, 0xFFFFFFFFu - seconds(5));   // wraps 5 s in
  const SafetySettings f = r.s.safety;
  r.step(kCool);
  uint32_t ran = r.until(kNone, &LoadOutputs::cool, false, minutes(5));
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(f.compressorMinOnMs, ran);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(f.compressorMinOnMs + 20, ran);
  uint32_t gap = r.until(kCool, &LoadOutputs::cool, true, minutes(5));
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(f.compressorMinOffMs, gap);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(f.compressorMinOffMs + 20, gap);
}

void test_block_names() {
  TEST_ASSERT_EQUAL_STRING("boot-hold", guardBlockName(GuardBlock::BootHold));
  TEST_ASSERT_EQUAL_STRING("chamber-ceiling", guardBlockName(GuardBlock::ChamberCeiling));
  TEST_ASSERT_EQUAL_STRING("clear", guardBlockName(GuardBlock::None));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_boot_hold_holds_all_three);
  RUN_TEST(test_boot_counts_as_just_stopped_real_profile);
  RUN_TEST(test_boot_counts_as_just_stopped_bench_profile);
  RUN_TEST(test_boot_reason_shown_after_hold_is_crossover);
  RUN_TEST(test_settings_problem_turns_everything_off);
  RUN_TEST(test1_compressor_timing_real_and_bench);
  RUN_TEST(test1_heater_timing_real_and_bench);
  RUN_TEST(test1_humidifier_timing_real_and_bench);
  RUN_TEST(test1_rapid_demand_flapping_never_beats_the_timers);
  RUN_TEST(test_crossover_both_ways_real_and_bench);
  RUN_TEST(test_both_asked_cooling_first_heater_waits);
  RUN_TEST(test2_cabinet_ceiling);
  RUN_TEST(test2_chamber_ceiling);
  RUN_TEST(test2_fan);
  RUN_TEST(test2_chamber_sensor_unhealthy);
  RUN_TEST(test2_too_few_cabinet_sensors);
  RUN_TEST(test2_stale_readings);
  RUN_TEST(test2_heater_disabled);
  RUN_TEST(test_three_of_four_cabinet_is_enough);
  RUN_TEST(test_compressor_stops_for_sensors_and_fan);
  RUN_TEST(test_humidifier_rh_ceiling_at_70);
  RUN_TEST(test_humidifier_keeps_running_with_chamber_failed);
  RUN_TEST(test_humidifier_needs_no_fan);
  RUN_TEST(test_humidifier_blocked_during_compressor_run_unless_enabled);
  RUN_TEST(test_humidifier_never_while_stripping);
  RUN_TEST(test_exercise_waiting_lets_a_running_load_finish_but_blocks_starts);
  RUN_TEST(test_exercise_cycling_holds_both_off_humidifier_free);
  RUN_TEST(test_timing_across_millis_wrap);
  RUN_TEST(test_block_names);
  return UNITY_END();
}
