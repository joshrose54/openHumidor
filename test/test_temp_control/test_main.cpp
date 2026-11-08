// Temperature control (stage 4): deadband and run ends (4.5), crossover count,
// harvest and the plenum fan (4.6), RH stripping (4.7), ineffective checks (5.6),
// the heater PWM cap. Each rule on both profiles where the timing differs.
#include <unity.h>
#include <string.h>
#include <math.h>
#include <initializer_list>
#include "wineador/temp_control.h"
#include "wineador/psychro.h"
#include "wineador/heater_pwm.h"
#include "wineador/guards.h"
#include "wineador/settings.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

namespace {

// A summary with all four cabinet sensors at one temperature and RH, and the chamber.
CabinetSummary cabAt(float c, float rh = 62.0f, float chamber = NAN) {
  CabinetSummary s;
  s.sensorsOk = 6;
  s.cabinetOk = 4;
  s.meanC = s.minC = s.maxC = s.controlC = c;
  s.meanRhPct = s.maxRhPct = rh;
  s.meanVpKpa = vapourPressureKpa(c, rh);
  s.chamberOk = true;
  s.chamberC = isnan(chamber) ? c : chamber;
  s.ambientOk = true;
  s.ambientC = 20.0f;
  return s;
}

struct Rig {
  Settings s;
  TemperatureControl tc;
  ControlInputs in;
  ControlOutput out;
  uint32_t now = 1000;
  explicit Rig(Profile p = Profile::Bench) : s(settingsFor(p)) {
    tc.begin(s, now);
    in.readingsFresh = true;
    in.cab = cabAt(20.0f);
  }
  ControlOutput step(uint32_t dt = 0) {
    now += dt;
    in.nowMs = now;
    out = tc.update(in);
    return out;
  }
  void cool(bool on) { in.live.cool = on; in.live.relayHeating = false; if (on) in.live.heat = false; }
  void heat(bool on) { in.live.heat = on; in.live.relayHeating = on; if (on) in.live.cool = false; }
  int count(ControlEvent::Type t) {
    int n = 0;
    for (ControlEvent e; tc.takeEvent(e);) if (e.type == t) n++;
    return n;
  }
  ControlEvent last(ControlEvent::Type t) {
    ControlEvent found;
    for (ControlEvent e; tc.takeEvent(e);) if (e.type == t) found = e;
    return found;
  }
};

}  // namespace

// ------------------------------------------------------------------ vapour pressure
void test_vapour_pressure_matches_spec() {
  TEST_ASSERT_FLOAT_WITHIN(0.005f, 1.45f, vapourPressureKpa(20.0f, 62.0f));     // 4.7
  // 62 % at 20 C reads ~85 % at 15 C with nothing removed (4.7)
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 85.0f, rhFromVp(15.0f, vapourPressureKpa(20.0f, 62.0f)));
}

// ------------------------------------------------------------------ deadband
// Sweep 17 -> 23 -> 17 C in 0.05 C steps (B3 test 7). Cooling asked for only
// above 20.5, ending at 20.0; heating only below 18.5, ending at 19.0.
void test_deadband_sweep_both_profiles() {
  for (Profile p : {Profile::Real, Profile::Bench}) {
    Rig r(p);
    bool cool = false, heat = false;
    int coolOn = 0, coolOff = 0, heatOn = 0, heatOff = 0;
    auto at = [&](float c) {
      r.in.cab = cabAt(c);
      ControlOutput o = r.step(1000);
      if (o.demand.cool != cool) {
        if (o.demand.cool) { TEST_ASSERT_TRUE(c > 20.5f); coolOn++; }
        else               { TEST_ASSERT_TRUE(c <= 20.0f); TEST_ASSERT_TRUE(c > 19.9f); coolOff++; }
        cool = o.demand.cool;
      }
      if (o.demand.heat != heat) {
        if (o.demand.heat) { TEST_ASSERT_TRUE(c < 18.5f); heatOn++; }
        else               { TEST_ASSERT_TRUE(c >= 19.0f); TEST_ASSERT_TRUE(c < 19.1f); heatOff++; }
        heat = o.demand.heat;
      }
      TEST_ASSERT_FALSE(o.demand.cool && o.demand.heat);
    };
    for (int i = 0; i <= 120; i++) at(17.0f + 0.05f * i);
    for (int i = 120; i >= 0; i--) at(17.0f + 0.05f * i);
    TEST_ASSERT_EQUAL_INT(1, coolOn);
    TEST_ASSERT_EQUAL_INT(1, coolOff);
    TEST_ASSERT_EQUAL_INT(2, heatOn);    // at the start (17 C) and on the way back down
    TEST_ASSERT_EQUAL_INT(1, heatOff);
  }
}

void test_nothing_asked_entering_dead_zone_from_idle() {
  Rig r;
  for (float c = 18.55f; c < 20.5f; c += 0.05f) {
    r.in.cab = cabAt(c);
    ControlOutput o = r.step(1000);
    TEST_ASSERT_FALSE(o.demand.cool);
    TEST_ASSERT_FALSE(o.demand.heat);
  }
}

void test_deadband_follows_setpoint() {
  Rig r;
  r.s.user.targetTempC = 17.0f;
  r.tc.begin(r.s, r.now);
  r.in.cab = cabAt(17.6f);
  TEST_ASSERT_TRUE(r.step(1000).demand.cool);
  r.in.cab = cabAt(17.0f);
  TEST_ASSERT_FALSE(r.step(1000).demand.cool);
  r.in.cab = cabAt(15.4f);
  TEST_ASSERT_TRUE(r.step(1000).demand.heat);
}

void test_control_source_is_controlC() {
  // Spread over 2 C: the summary's control value is the warmest; that is what decides (4.2).
  Rig r;
  CabinetSummary c = cabAt(19.6f);
  c.minC = 18.6f; c.maxC = 20.8f; c.spreadC = 2.2f; c.controlC = 20.8f; c.controlWarmest = true;
  r.in.cab = c;
  TEST_ASSERT_TRUE(r.step(1000).demand.cool);
}

void test_no_demand_without_readings_or_when_scripted() {
  Rig r;
  r.in.cab = cabAt(23.0f);
  r.in.readingsFresh = false;
  TEST_ASSERT_FALSE(r.step(1000).demand.cool);
  r.in.readingsFresh = true;
  r.in.cab.cabinetOk = 0;
  TEST_ASSERT_FALSE(r.step(1000).demand.cool);
  r.in.cab = cabAt(23.0f);
  r.in.scriptedDemand = true;
  TEST_ASSERT_FALSE(r.step(1000).demand.cool);
  r.in.scriptedDemand = false;
  TEST_ASSERT_TRUE(r.step(1000).demand.cool);
}

// ------------------------------------------------------------------ crossover
void test_crossover_counted_each_change_of_load() {
  Rig r;
  r.cool(true);  r.step(1000);
  r.cool(false); r.step(1000);
  r.cool(true);  r.step(1000);     // cooling again: not a crossover
  r.cool(false); r.step(1000);
  TEST_ASSERT_EQUAL_UINT32(0, r.tc.crossovers());
  r.heat(true);  r.step(1000);
  TEST_ASSERT_EQUAL_UINT32(1, r.tc.crossovers());
  ControlEvent e = r.last(ControlEvent::Type::Crossover);
  TEST_ASSERT_TRUE(e.type == ControlEvent::Type::Crossover);
  TEST_ASSERT_EQUAL_UINT32(1, e.count);
  r.heat(false); r.step(1000);
  r.cool(true);  r.step(1000);
  TEST_ASSERT_EQUAL_UINT32(2, r.tc.crossovers());
}

// ------------------------------------------------------------------ fan and harvest
void test_fan_duties() {
  Rig r;
  TEST_ASSERT_EQUAL_UINT8(20, r.step(1000).fanPct);          // idle
  r.cool(true);
  TEST_ASSERT_EQUAL_UINT8(40, r.step(1000).fanPct);          // compressor
  r.cool(false);
  r.in.cab = cabAt(20.0f, 62.0f, 20.5f);                     // already converged: harvest ends at once
  r.step(1000);
  TEST_ASSERT_EQUAL_UINT8(20, r.step(1000).fanPct);
  r.heat(true);
  TEST_ASSERT_EQUAL_UINT8(60, r.step(1000).fanPct);          // heater
  r.heat(false);
  r.in.cab = cabAt(20.0f, 62.0f, 25.0f);
  TEST_ASSERT_EQUAL_UINT8(60, r.step(1000).fanPct);          // heating harvest
  TEST_ASSERT_TRUE(r.tc.harvest() == HarvestKind::Heating);
}

void test_cooling_harvest_profile_follows_gap() {
  for (Profile p : {Profile::Real, Profile::Bench}) {
    Rig r(p);
    r.cool(true);
    r.in.cab = cabAt(20.0f, 62.0f, 14.0f);                   // 6 C gap when it stops
    r.step(1000);
    r.cool(false);
    TEST_ASSERT_EQUAL_UINT8(100, r.step(1000).fanPct);       // fast at first
    TEST_ASSERT_TRUE(r.tc.harvest() == HarvestKind::Cooling);
    uint8_t prev = 100;
    for (float gap = 6.0f; gap > 1.0f; gap -= 0.25f) {
      r.in.cab = cabAt(20.0f, 62.0f, 20.0f - gap);
      const uint8_t d = r.step(1000).fanPct;
      TEST_ASSERT_TRUE(d <= prev);                           // never rises
      TEST_ASSERT_TRUE(d >= 40);
      TEST_ASSERT_EQUAL_INT(0, d % 5);                       // 5 % steps
      prev = d;
    }
    r.in.cab = cabAt(20.0f, 62.0f, 18.5f);                   // gap 1.5: nearly there
    TEST_ASSERT_TRUE(r.step(1000).fanPct <= 50);
    r.in.cab = cabAt(20.0f, 62.0f, 13.0f);                   // gap grows again: duty does not rise
    TEST_ASSERT_TRUE(r.step(1000).fanPct <= 50);
    r.in.cab = cabAt(20.0f, 62.0f, 19.0f);                   // gap 1.0: converged
    r.step(1000);
    TEST_ASSERT_TRUE(r.tc.harvest() == HarvestKind::None);
    TEST_ASSERT_TRUE(r.tc.lastHarvestEnd() == HarvestEnd::Convergence);
    TEST_ASSERT_EQUAL_UINT8(20, r.tc.fanPct());
  }
}

void test_halfway_gap_gives_halfway_duty() {
  Rig r;
  r.cool(true);
  r.in.cab = cabAt(20.0f, 62.0f, 15.0f);    // gap 5: span 4 above the 1.0 margin
  r.step(1000);
  r.cool(false);
  r.step(1000);
  r.in.cab = cabAt(20.0f, 62.0f, 17.0f);    // gap 3: halfway -> 40 + 30 = 70 %
  TEST_ASSERT_EQUAL_UINT8(70, r.step(1000).fanPct);
}

void test_harvest_backstop_both_profiles() {
  for (Profile p : {Profile::Real, Profile::Bench}) {
    Rig r(p);
    const uint32_t back = r.s.tuning.harvestBackstopMs;
    r.heat(true);
    r.in.cab = cabAt(20.0f, 62.0f, 26.0f);
    r.step(1000);
    r.heat(false);
    r.step(1000);
    const uint32_t t0 = r.now;
    while (r.tc.harvest() != HarvestKind::None && r.now - t0 < back + 10000) r.step(500);
    TEST_ASSERT_TRUE(r.tc.lastHarvestEnd() == HarvestEnd::Backstop);
    TEST_ASSERT_UINT32_WITHIN(500, back, r.now - t0);
  }
}

void test_harvest_with_chamber_unhealthy_runs_to_backstop() {
  Rig r;
  r.cool(true);
  r.in.cab = cabAt(20.0f, 62.0f, 14.0f);
  r.step(1000);
  r.in.cab.chamberOk = false;
  r.cool(false);
  r.step(1000);
  TEST_ASSERT_TRUE(r.tc.harvest() == HarvestKind::Cooling);
  TEST_ASSERT_EQUAL_UINT8(100, r.tc.fanPct());
  r.step(r.s.tuning.harvestBackstopMs - 1500);
  TEST_ASSERT_TRUE(r.tc.harvest() == HarvestKind::Cooling);
  TEST_ASSERT_EQUAL_UINT8(100, r.tc.fanPct());        // gap unknown: duty holds
  r.step(1500);                                       // exactly the backstop
  TEST_ASSERT_TRUE(r.tc.lastHarvestEnd() == HarvestEnd::Backstop);
}

void test_harvest_ends_when_a_load_starts() {
  Rig r;
  r.cool(true);
  r.in.cab = cabAt(20.6f, 62.0f, 14.0f);
  r.step(1000);
  r.cool(false);
  r.step(1000);
  r.cool(true);
  r.step(1000);
  TEST_ASSERT_TRUE(r.tc.harvest() == HarvestKind::None);
  TEST_ASSERT_TRUE(r.tc.lastHarvestEnd() == HarvestEnd::LoadStarted);
  TEST_ASSERT_EQUAL_UINT8(40, r.tc.fanPct());
}

void test_idle_fan_override_and_refusal() {
  Rig r;
  TEST_ASSERT_FALSE(r.tc.setIdleFanOverride(10));     // 1-19 %: refused (4.6)
  TEST_ASSERT_FALSE(r.tc.setIdleFanOverride(1));
  TEST_ASSERT_FALSE(r.tc.setIdleFanOverride(19));
  TEST_ASSERT_FALSE(r.tc.setIdleFanOverride(101));
  TEST_ASSERT_EQUAL_INT(-1, r.tc.idleFanOverride());
  TEST_ASSERT_EQUAL_UINT8(20, r.step(1000).fanPct);
  TEST_ASSERT_TRUE(r.tc.setIdleFanOverride(0));
  TEST_ASSERT_EQUAL_UINT8(0, r.step(1000).fanPct);
  r.cool(true);
  TEST_ASSERT_EQUAL_UINT8(40, r.step(1000).fanPct);   // a load runs: the rules win
  r.cool(false);
  r.in.cab = cabAt(20.0f);                            // converged at once
  r.step(1000);
  TEST_ASSERT_EQUAL_UINT8(0, r.step(1000).fanPct);
  r.in.cab = cabAt(20.6f);                            // cooling asked for, not yet live:
  TEST_ASSERT_EQUAL_UINT8(40, r.step(1000).fanPct);   // the fan starts first (fan guard)
  r.in.cab = cabAt(18.4f);
  r.step(1000);
  TEST_ASSERT_EQUAL_UINT8(60, r.step(1000).fanPct);
  r.in.cab = cabAt(19.5f);
  TEST_ASSERT_EQUAL_UINT8(0, r.step(1000).fanPct);
  TEST_ASSERT_TRUE(r.tc.setIdleFanOverride(-1));
  TEST_ASSERT_EQUAL_UINT8(20, r.step(1000).fanPct);
  r.in.cab = cabAt(20.6f);                            // with idle at 20 % an asked-for load
  TEST_ASSERT_EQUAL_UINT8(20, r.step(1000).fanPct);   // changes nothing until it runs
}

// ------------------------------------------------------------------ stripping
void test_strip_triggers_after_full_persistence_both_profiles() {
  for (Profile p : {Profile::Real, Profile::Bench}) {
    Rig r(p);
    const uint32_t per = r.s.tuning.rhStripPersistMs;
    r.in.cab = cabAt(20.0f, 67.0f);                   // = setpoint + 5: triggers (>=)
    r.step(1000);
    const uint32_t t0 = r.now;
    r.step(per - 1000);
    TEST_ASSERT_FALSE(r.tc.stripping());
    TEST_ASSERT_FALSE(r.out.demand.cool);
    r.step(1000);
    TEST_ASSERT_TRUE(r.tc.stripping());
    TEST_ASSERT_EQUAL_UINT32(per, r.now - t0);
    TEST_ASSERT_TRUE(r.out.demand.cool);              // cooling called although 20.0 C is in the dead zone
    TEST_ASSERT_TRUE(r.out.strippingActive);
    TEST_ASSERT_EQUAL_UINT32(1, r.tc.stripCount());
    TEST_ASSERT_EQUAL_INT(1, r.count(ControlEvent::Type::StripStart));
  }
}

void test_strip_persistence_restarts_after_a_dip() {
  Rig r;
  const uint32_t per = r.s.tuning.rhStripPersistMs;
  r.in.cab = cabAt(20.0f, 68.0f);
  r.step(1000);
  r.step(per - 5000);
  r.in.cab = cabAt(20.0f, 66.9f);                     // one dip below the trigger
  r.step(1000);
  r.in.cab = cabAt(20.0f, 68.0f);
  r.step(1000);
  const uint32_t t0 = r.now;
  while (!r.tc.stripping() && r.now - t0 < 3 * per) r.step(500);
  TEST_ASSERT_UINT32_WITHIN(500, per, r.now - t0);    // the full time again, from zero
}

void test_strip_releases_on_vapour_pressure_not_rh() {
  Rig r;
  r.in.cab = cabAt(20.0f, 68.0f);
  r.step(1000);
  r.step(r.s.tuning.rhStripPersistMs);
  TEST_ASSERT_TRUE(r.tc.stripping());
  const float target = r.tc.stripTargetVpKpa();
  // The cabinet cools while water comes out. RH stays high (above the trigger,
  // even) while vapour pressure falls: it must keep stripping until VP <= target.
  CabinetSummary c = cabAt(18.9f, 70.0f);   // VP 1.53 > 1.447
  TEST_ASSERT_TRUE(c.meanVpKpa > target);
  r.in.cab = c;
  r.step(1000);
  TEST_ASSERT_TRUE(r.tc.stripping());
  c = cabAt(18.9f, rhFromVp(18.9f, target) + 0.1f);   // RH ~67 %, VP just above target
  r.in.cab = c;
  r.step(1000);
  TEST_ASSERT_TRUE(r.tc.stripping());
  c = cabAt(18.9f, rhFromVp(18.9f, target) - 0.05f);  // VP at/below target, RH still ~67 %
  TEST_ASSERT_TRUE(c.meanRhPct > 66.0f);
  r.in.cab = c;
  r.step(1000);
  TEST_ASSERT_FALSE(r.tc.stripping());
  TEST_ASSERT_TRUE(r.tc.lastStripStop() == StripStop::VapourPressure);
  ControlEvent e = r.last(ControlEvent::Type::StripStop);
  TEST_ASSERT_TRUE(e.strip == StripStop::VapourPressure);
}

void test_strip_floor_stops_and_persistence_waits_for_floor_clear() {
  Rig r;
  const uint32_t per = r.s.tuning.rhStripPersistMs;
  r.in.cab = cabAt(20.0f, 69.0f);
  r.step(1000);
  r.step(per);
  TEST_ASSERT_TRUE(r.tc.stripping());
  // One sensor below 18.0 (floor = setpoint - 2), the mean above 18.5 (no heat call)
  CabinetSummary c = cabAt(18.8f, 72.0f);
  c.minC = 17.9f;
  r.in.cab = c;
  r.step(1000);
  TEST_ASSERT_FALSE(r.tc.stripping());
  TEST_ASSERT_TRUE(r.tc.lastStripStop() == StripStop::Floor);
  TEST_ASSERT_FALSE(r.out.demand.heat);
  // Still below the floor: persistence does not count, however long
  r.step(3 * per);
  TEST_ASSERT_FALSE(r.tc.stripping());
  TEST_ASSERT_FALSE(r.tc.stripPersisting());
  // Back above the floor (exactly at it counts as above): the full persistence from zero
  c.minC = 18.0f;
  r.in.cab = c;
  r.step(1000);
  const uint32_t t0 = r.now;
  while (!r.tc.stripping() && r.now - t0 < 3 * per) r.step(500);
  TEST_ASSERT_TRUE(r.tc.stripping());
  TEST_ASSERT_UINT32_WITHIN(500, per, r.now - t0);
}

void test_heating_wins_over_stripping() {
  Rig r;
  const uint32_t per = r.s.tuning.rhStripPersistMs;
  r.in.cab = cabAt(20.0f, 69.0f);
  r.step(1000);
  r.step(per);
  TEST_ASSERT_TRUE(r.tc.stripping());
  r.in.cab = cabAt(18.4f, 74.0f);       // below setpoint - 1.5: heating called; above the floor
  r.step(1000);
  TEST_ASSERT_FALSE(r.tc.stripping());
  TEST_ASSERT_TRUE(r.tc.lastStripStop() == StripStop::Heating);
  TEST_ASSERT_TRUE(r.out.demand.heat);
  TEST_ASSERT_FALSE(r.out.demand.cool);
  // ...and it never starts while heating is asked for, however long the RH stays high
  r.step(3 * per);
  TEST_ASSERT_FALSE(r.tc.stripping());
  // or while the heater is still running (min on) after the call ends
  r.in.cab = cabAt(19.2f, 72.0f);
  r.heat(true);
  r.step(1000);
  r.step(3 * per);
  TEST_ASSERT_FALSE(r.tc.stripping());
}

void test_strip_disabled_and_scripted() {
  Rig r;
  r.s.tuning.rhStripEnable = false;
  r.tc.begin(r.s, r.now);
  r.in.cab = cabAt(20.0f, 69.0f);
  r.step(1000);
  r.step(10 * r.s.tuning.rhStripPersistMs);
  TEST_ASSERT_FALSE(r.tc.stripping());
  Rig q;
  q.in.cab = cabAt(20.0f, 69.0f);
  q.in.scriptedDemand = true;
  q.step(1000);
  q.step(10 * q.s.tuning.rhStripPersistMs);
  TEST_ASSERT_FALSE(q.tc.stripping());
}

void test_strip_stops_without_enough_sensors() {
  Rig r;
  r.in.cab = cabAt(20.0f, 69.0f);
  r.step(1000);
  r.step(r.s.tuning.rhStripPersistMs);
  TEST_ASSERT_TRUE(r.tc.stripping());
  r.in.cab.cabinetOk = 2;
  r.step(1000);
  TEST_ASSERT_FALSE(r.tc.stripping());
  TEST_ASSERT_TRUE(r.tc.lastStripStop() == StripStop::Sensors);
}

// ------------------------------------------------------------------ ineffective checks
void test_heater_ineffective_off_flag_retry_clear_both_profiles() {
  for (Profile p : {Profile::Real, Profile::Bench}) {
    Rig r(p);
    const TuningSettings& t = r.s.tuning;
    r.in.cab = cabAt(18.0f, 62.0f, 18.0f);
    r.heat(true);
    r.step(1000);
    r.in.cab = cabAt(18.0f, 62.0f, 18.4f);              // +0.4: not enough
    r.step(t.heaterIneffectiveWindowMs - 1000);
    TEST_ASSERT_FALSE(r.tc.heaterIneffective());
    r.step(1000);
    TEST_ASSERT_TRUE(r.tc.heaterIneffective());
    TEST_ASSERT_TRUE(r.out.heaterIneffectiveHold);       // -> guards: heater off
    TEST_ASSERT_EQUAL_INT(1, r.count(ControlEvent::Type::HeaterIneffective));
    r.heat(false);                                       // the guard turns it off
    r.step(t.heaterIneffectiveRetryMs - 1000);
    TEST_ASSERT_TRUE(r.out.heaterIneffectiveHold);
    r.step(1000);
    TEST_ASSERT_FALSE(r.out.heaterIneffectiveHold);      // retry: may start again
    TEST_ASSERT_TRUE(r.tc.heaterIneffective());          // still flagged until a window passes
    r.in.cab = cabAt(18.0f, 62.0f, 18.0f);
    r.heat(true);
    r.step(1000);
    r.in.cab = cabAt(18.0f, 62.0f, 18.5f);               // +0.5 within the window: passes
    r.step(1000);
    TEST_ASSERT_FALSE(r.tc.heaterIneffective());
    TEST_ASSERT_EQUAL_INT(1, r.count(ControlEvent::Type::HeaterEffective));
  }
}

void test_heater_run_ending_inside_window_gives_no_result() {
  Rig r;
  r.in.cab = cabAt(18.0f, 62.0f, 18.0f);
  r.heat(true);
  r.step(1000);
  r.step(r.s.tuning.heaterIneffectiveWindowMs / 2);
  r.heat(false);
  r.step(1000);
  r.step(r.s.tuning.heaterIneffectiveWindowMs);
  TEST_ASSERT_FALSE(r.tc.heaterIneffective());
  TEST_ASSERT_EQUAL_INT(0, r.count(ControlEvent::Type::HeaterIneffective));
}

void test_heater_check_once_per_run() {
  // Passed once in a run: a later stall in the same run is not checked again.
  Rig r;
  r.in.cab = cabAt(18.0f, 62.0f, 18.0f);
  r.heat(true);
  r.step(1000);
  r.in.cab = cabAt(18.0f, 62.0f, 19.0f);
  r.step(1000);
  r.in.cab = cabAt(18.0f, 62.0f, 18.0f);
  r.step(3 * r.s.tuning.heaterIneffectiveWindowMs);
  TEST_ASSERT_FALSE(r.tc.heaterIneffective());
}

void test_cooling_ineffective_flag_only_both_profiles() {
  for (Profile p : {Profile::Real, Profile::Bench}) {
    Rig r(p);
    const TuningSettings& t = r.s.tuning;
    r.in.cab = cabAt(21.0f, 62.0f, 21.0f);
    r.cool(true);
    r.step(1000);
    r.in.cab = cabAt(21.0f, 62.0f, 20.1f);               // -0.9: not enough
    r.step(t.coolingIneffectiveWindowMs);
    TEST_ASSERT_TRUE(r.tc.coolingIneffective());
    TEST_ASSERT_TRUE(r.out.demand.cool);                 // cooling still called
    TEST_ASSERT_FALSE(r.out.heaterIneffectiveHold);
    TEST_ASSERT_EQUAL_INT(1, r.count(ControlEvent::Type::CoolingIneffective));
    r.cool(false);
    r.step(1000);
    r.in.cab = cabAt(21.0f, 62.0f, 21.0f);
    r.cool(true);
    r.step(1000);
    r.in.cab = cabAt(21.0f, 62.0f, 20.0f);               // -1.0: passes, flag clears
    r.step(1000);
    TEST_ASSERT_FALSE(r.tc.coolingIneffective());
    TEST_ASSERT_EQUAL_INT(1, r.count(ControlEvent::Type::CoolingEffective));
  }
}

void test_no_check_when_chamber_unhealthy_at_start() {
  Rig r;
  r.in.cab = cabAt(18.0f);
  r.in.cab.chamberOk = false;
  r.heat(true);
  r.step(1000);
  r.step(2 * r.s.tuning.heaterIneffectiveWindowMs);
  TEST_ASSERT_FALSE(r.tc.heaterIneffective());
}

void test_guard_holds_heater_when_ineffective() {
  Settings s = settingsFor(Profile::Bench);
  RuntimeGuards g;
  g.begin(s, 0);
  GuardInputs in;
  in.nowMs = minutes(10);
  in.bootHoldActive = false;
  in.fanOk = true;
  in.readingsFresh = true;
  in.cab = cabAt(18.0f);
  in.heaterIneffectiveHold = true;
  Demand d; d.heat = true;
  LoadOutputs live; live.heat = true; live.relayHeating = true;
  GuardDecisions out = g.evaluate(in, d, live);
  TEST_ASSERT_FALSE(out.heat.run);                       // a stop guard: off at once, min on or not
  TEST_ASSERT_TRUE(out.heat.block == GuardBlock::Ineffective);
  TEST_ASSERT_EQUAL_STRING("heater-ineffective", guardBlockName(GuardBlock::Ineffective));
}

void test_stripping_blocks_humidifier_in_guards() {
  Settings s = settingsFor(Profile::Bench);
  RuntimeGuards g;
  g.begin(s, 0);
  GuardInputs in;
  in.nowMs = minutes(10);
  in.bootHoldActive = false;
  in.fanOk = true;
  in.readingsFresh = true;
  in.cab = cabAt(20.0f, 60.0f);
  in.strippingActive = true;
  Demand d; d.humid = true;
  GuardDecisions out = g.evaluate(in, d, LoadOutputs{});
  TEST_ASSERT_FALSE(out.humid.run);
  TEST_ASSERT_TRUE(out.humid.block == GuardBlock::Stripping);
}

// ------------------------------------------------------------------ heater PWM cap
void test_heater_pwm_cap() {
  TEST_ASSERT_TRUE(heaterPinOn(12345, 0, 100, 1000));      // starting value: full duty
  TEST_ASSERT_FALSE(heaterPinOn(12345, 0, 0, 1000));
  int on = 0;
  for (uint32_t t = 0; t < 10000; t += 10) on += heaterPinOn(5000 + t, 5000, 30, 1000) ? 1 : 0;
  TEST_ASSERT_EQUAL_INT(300, on);                          // 30 % of 1 s periods
  TEST_ASSERT_TRUE(heaterPinOn(5000, 5000, 30, 1000));     // starts on
  TEST_ASSERT_FALSE(heaterPinOn(5300, 5000, 30, 1000));
  TEST_ASSERT_TRUE(heaterPinOn(0xFFFFFF00u + 1000, 0xFFFFFF00u, 30, 1000));   // across the millis wrap
}

// ------------------------------------------------------------------ text
void test_event_and_status_text() {
  Settings s = settingsFor(Profile::Bench);
  char buf[300];
  ControlEvent e;
  e.type = ControlEvent::Type::StripStop;
  e.strip = StripStop::VapourPressure;
  e.a = 65.2f; e.b = 1.44f;
  formatControlEvent(buf, sizeof(buf), e, s);
  TEST_ASSERT_NOT_NULL(strstr(buf, "RH STRIP STOP: vapour pressure reached target"));
  e = ControlEvent{};
  e.type = ControlEvent::Type::HeaterIneffective;
  e.a = 0.1f;
  formatControlEvent(buf, sizeof(buf), e, s);
  TEST_ASSERT_NOT_NULL(strstr(buf, "HEATER INEFFECTIVE"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "120 s"));                // bench retry
  TEST_ASSERT_NOT_NULL(strstr(buf, "changed +0.10 C"));
  e.a = -1.16f;                                              // chamber fell: reads as a change, not a "rise"
  formatControlEvent(buf, sizeof(buf), e, s);
  TEST_ASSERT_NOT_NULL(strstr(buf, "changed -1.16 C"));
  e.type = ControlEvent::Type::CoolingIneffective;
  e.a = 0.44f;                                               // a fall of 0.44
  formatControlEvent(buf, sizeof(buf), e, s);
  TEST_ASSERT_NOT_NULL(strstr(buf, "changed -0.44 C in 60 s (needs -1.0)"));
  e = ControlEvent{};
  e.type = ControlEvent::Type::HarvestEnd;
  e.kind = HarvestKind::Cooling;
  e.harvestEnd = HarvestEnd::Convergence;
  e.ms = 48300;
  formatControlEvent(buf, sizeof(buf), e, s);
  TEST_ASSERT_EQUAL_STRING("HARVEST END (cooling): chamber converged after 48.3 s", buf);

  Rig r;
  r.in.cab = cabAt(20.12f, 62.0f);
  r.step(1000);
  formatControlText(buf, sizeof(buf), r.tc, r.in.cab, r.now);
  TEST_ASSERT_NOT_NULL(strstr(buf, "ctl 20.12C mean idle"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "xover 0"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_vapour_pressure_matches_spec);
  RUN_TEST(test_deadband_sweep_both_profiles);
  RUN_TEST(test_nothing_asked_entering_dead_zone_from_idle);
  RUN_TEST(test_deadband_follows_setpoint);
  RUN_TEST(test_control_source_is_controlC);
  RUN_TEST(test_no_demand_without_readings_or_when_scripted);
  RUN_TEST(test_crossover_counted_each_change_of_load);
  RUN_TEST(test_fan_duties);
  RUN_TEST(test_cooling_harvest_profile_follows_gap);
  RUN_TEST(test_halfway_gap_gives_halfway_duty);
  RUN_TEST(test_harvest_backstop_both_profiles);
  RUN_TEST(test_harvest_with_chamber_unhealthy_runs_to_backstop);
  RUN_TEST(test_harvest_ends_when_a_load_starts);
  RUN_TEST(test_idle_fan_override_and_refusal);
  RUN_TEST(test_strip_triggers_after_full_persistence_both_profiles);
  RUN_TEST(test_strip_persistence_restarts_after_a_dip);
  RUN_TEST(test_strip_releases_on_vapour_pressure_not_rh);
  RUN_TEST(test_strip_floor_stops_and_persistence_waits_for_floor_clear);
  RUN_TEST(test_heating_wins_over_stripping);
  RUN_TEST(test_strip_disabled_and_scripted);
  RUN_TEST(test_strip_stops_without_enough_sensors);
  RUN_TEST(test_heater_ineffective_off_flag_retry_clear_both_profiles);
  RUN_TEST(test_heater_run_ending_inside_window_gives_no_result);
  RUN_TEST(test_heater_check_once_per_run);
  RUN_TEST(test_cooling_ineffective_flag_only_both_profiles);
  RUN_TEST(test_no_check_when_chamber_unhealthy_at_start);
  RUN_TEST(test_guard_holds_heater_when_ineffective);
  RUN_TEST(test_stripping_blocks_humidifier_in_guards);
  RUN_TEST(test_heater_pwm_cap);
  RUN_TEST(test_event_and_status_text);
  return UNITY_END();
}
