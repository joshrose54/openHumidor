// TEST BUILDS ONLY — the stage 4 scenarios (B3 tests 7, 8, 9, 10, 17, 23).
//
// Unlike the stage 2-3 scenarios these never ask for a load. They script the
// cabinet model (temperature, RH, how the loads affect it) and the REAL
// temperature control decides, through the real guards and interlock. Each
// check uses the summary the control acted on that pass (tc.lastCab()), so a
// sensor read landing mid-pass can't make a correct decision look wrong.
#include "wineador/scenario.h"

#if defined(WINEADOR_TEST_BUILD)

#include <stdio.h>
#include <math.h>

namespace wineador {

namespace {

constexpr uint32_t kLateMs = 500;   // a timed change may come this much after its expected time
constexpr uint32_t kEarlyMs = 5;    // ...and never more than this before it

const char* secs4(char* out, size_t n, uint32_t ms) {
  if (ms < 60000) snprintf(out, n, "%lu.%lu s", (unsigned long)(ms / 1000), (unsigned long)((ms % 1000) / 100));
  else snprintf(out, n, "%lu:%02lu.%lu", (unsigned long)(ms / 60000), (unsigned long)((ms / 1000) % 60),
                (unsigned long)((ms % 1000) / 100));
  return out;
}

const char* onWord(bool b) { return b ? "ON" : "off"; }

// true if `got` is within [want - early, want + late]
bool onTime(uint32_t got, uint32_t want) { return got + kEarlyMs >= want && got <= want + kLateMs; }

}  // namespace

// ------------------------------------------------------------------ run 7
// B3 test 7, deadband. The cabinet is swept 17 -> 23 -> 17 C at 1.5 C/min (the
// loads don't move it). Checked on every pass:
//   - a load STARTS only outside the dead zone: compressor above 20.5, heater below 18.5
//   - a run ENDS (demand gone) at its threshold: cooling at 20.0, heating at 19.0
//   - so nothing ever runs between 19.0 and 20.0
void ScenarioRunner::run7(const ScenarioView& v, Controller& ctl, CabinetModel& model, TemperatureControl& tc) {
  const uint32_t now = v.nowMs;
  const TuningSettings& t = s_.tuning;
  const float sp = s_.user.targetTempC;
  const float coolStart = sp + t.coolingHysteresisC, coolEnd = sp;
  const float heatStart = sp - t.heatingDeadbandC, heatEnd = sp - t.heatingStopBelowSetpointC;
  const CabinetSummary& c = tc.lastCab();
  const float x = c.controlC;
  const GuardDecisions& d = ctl.decisions();
  char m[160];

  if (phase_ >= 1) {
    if (wentOn_[0]) {
      coolStarts_++;
      if (!(x > coolStart)) {
        snprintf(m, sizeof(m), "compressor STARTED at %.2f C - inside the dead zone (must be above %.1f)", x, coolStart);
        finish(now, model, Result::Fail, m);
        return;
      }
      say(now, "compressor ON at %.2f C (starts only above %.1f) - correct", x, coolStart);
    }
    if (wentOn_[1]) {
      heatStarts_++;
      if (!(x < heatStart)) {
        snprintf(m, sizeof(m), "heater STARTED at %.2f C - inside the dead zone (must be below %.1f)", x, heatStart);
        finish(now, model, Result::Fail, m);
        return;
      }
      say(now, "heater ON at %.2f C (starts only below %.1f) - correct", x, heatStart);
    }
    if (wentOff_[0]) {
      if (d.cool.block == GuardBlock::None) {
        if (!(x <= coolEnd)) {
          snprintf(m, sizeof(m), "cooling demand ended at %.2f C - its run end is %.1f", x, coolEnd);
          finish(now, model, Result::Fail, m);
          return;
        }
        coolEnds_++;
        say(now, "compressor OFF at %.2f C: run end at setpoint (%.1f) - correct", x, coolEnd);
      } else {
        say(now, "compressor OFF at %.2f C (%s guard - still asked for; restarts after its rest)", x,
            guardBlockName(d.cool.block));
      }
    }
    if (wentOff_[1]) {
      if (d.heat.block == GuardBlock::None) {
        if (!(x >= heatEnd)) {
          snprintf(m, sizeof(m), "heating demand ended at %.2f C - its run end is %.1f", x, heatEnd);
          finish(now, model, Result::Fail, m);
          return;
        }
        heatEnds_++;
        say(now, "heater OFF at %.2f C: run end at setpoint - %.1f (%.1f) - correct", x,
            t.heatingStopBelowSetpointC, heatEnd);
      } else {
        say(now, "heater OFF at %.2f C (%s guard - still asked for)", x, guardBlockName(d.heat.block));
      }
    }
    if (v.live.cool && x <= coolEnd && !d.cool.heldOnByMinOn) {
      snprintf(m, sizeof(m), "compressor running at %.2f C - at or below its run end (%.1f)", x, coolEnd);
      finish(now, model, Result::Fail, m);
      return;
    }
    if (v.live.heat && x >= heatEnd && !d.heat.heldOnByMinOn) {
      snprintf(m, sizeof(m), "heater running at %.2f C - at or above its run end (%.1f)", x, heatEnd);
      finish(now, model, Result::Fail, m);
      return;
    }
    if (v.sweepDone)
      say(now, "cabinet %.2f C (control %.2f, %s) | asked: %s%s | live: compressor %s, heater %s", model.cabinetC(), x,
          dir_ > 0 ? "rising" : "falling", tc.coolCall() ? "cool" : tc.heatCall() ? "heat" : "nothing",
          "", onWord(v.live.cool), onWord(v.live.heat));
  }

  switch (phase_) {
    case 0:
      say(now, "TEST 7 - deadband. Cabinet swept 17 -> 23 -> 17 C at 1.5 C/min. Dead zone %.1f-%.1f C: nothing may START there. "
               "Runs end at %.1f (cooling) and %.1f (heating). Watch the bulb (heater) and J10 pins 1-2 (cooling).",
          heatStart, coolStart, coolEnd, heatEnd);
      lastTickMs_ = now;
      phase(1, now);
      break;

    case 1: {
      const float dtMin = elapsedMs(now, lastTickMs_) / 60000.0f;
      lastTickMs_ = now;
      sweepC_ += dir_ * 1.5f * dtMin;
      if (dir_ > 0 && sweepC_ >= 23.0f) { sweepC_ = 23.0f; dir_ = -1; say(now, "top of the sweep, 23.0 C: sweeping back down"); }
      if (dir_ < 0 && sweepC_ <= 17.0f) { sweepC_ = 17.0f; say(now, "bottom of the sweep, 17.0 C: holding 20 s"); phase(2, now); }
      model.setCabinetC(sweepC_);
      break;
    }

    case 2:
      if (elapsedMs(now, phaseAtMs_) < seconds(20)) break;
      if (heatStarts_ < 1 || coolStarts_ < 1 || heatEnds_ < 1 || coolEnds_ < 1) {
        snprintf(m, sizeof(m), "the sweep did not exercise every edge (heater starts %d, ends %d; compressor starts %d, ends %d)",
                 heatStarts_, heatEnds_, coolStarts_, coolEnds_);
        finish(now, model, Result::Fail, m);
        break;
      }
      snprintf(m, sizeof(m), "nothing started inside %.1f-%.1f C (heater starts %d, compressor starts %d); runs ended at %.1f and %.1f; "
                             "nothing ran between %.1f and %.1f",
               heatStart, coolStart, heatStarts_, coolStarts_, coolEnd, heatEnd, heatEnd, coolEnd);
      finish(now, model, Result::Pass, m);
      break;
  }
}

// ------------------------------------------------------------------ run 8
// B3 test 8, crossover delay. Cooling runs to its end; the cabinet is then
// scripted cold at once, so heating is asked for at the next sensor read: the
// heater must stay off until the full delay after cooling stopped. Then the same
// the other way. Both crossovers must be counted.
void ScenarioRunner::run8(const ScenarioView& v, Controller& ctl, CabinetModel& model, TemperatureControl& tc) {
  const uint32_t now = v.nowMs;
  const uint32_t delay = s_.tuning.crossoverDelayMs;
  const GuardDecisions& d = ctl.decisions();
  const float x = tc.lastCab().controlC;
  char a[16], b[16], m[160];
  const uint32_t waitLimit = ctl.hold().remainingMs() + delay + s_.safety.compressorMinOffMs + minutes(2);

  switch (phase_) {
    case 0:
      say(now, "TEST 8 - crossover delay (%s). Cabinet scripted to 21.0 C: cooling will be asked for.",
          secs4(a, sizeof(a), delay));
      count0_ = tc.crossovers();
      phase(1, now);
      break;

    case 1:   // wait for cooling
      if (wentOn_[0]) { say(now, "compressor ON (control %.2f C). Cabinet cools 1 C/min to its run end at 20.0", x); phase(2, now); }
      else if (elapsedMs(now, phaseAtMs_) > waitLimit) finish(now, model, Result::Fail, "compressor never started");
      break;

    case 2:   // cooling ends: script the cabinet cold at once
      if (wentOff_[0]) {
        if (d.cool.block != GuardBlock::None) {
          snprintf(m, sizeof(m), "compressor stopped by %s, not its run end", guardBlockName(d.cool.block));
          finish(now, model, Result::Fail, m);
          break;
        }
        markMs_ = now;
        flag_ = false;
        model.setCabinetC(18.0f);
        say(now, "compressor OFF at %.2f C. Cabinet now scripted to 18.0 C: heating asked for at the next read. "
                 "The heater must stay off until %s after the compressor stopped", x, secs4(a, sizeof(a), delay));
        phase(3, now);
      } else if (elapsedMs(now, phaseAtMs_) > minutes(3)) finish(now, model, Result::Fail, "cooling never reached its run end");
      break;

    case 3:   // heater held by crossover, then starts at the full delay
      if (d.heat.demanded && !v.live.heat && d.heat.block == GuardBlock::Crossover && !flag_) {
        flag_ = true;
        say(now, "heating asked for, HELD by crossover: %s left", secs4(a, sizeof(a), d.heat.waitMs));
      }
      if (wentOn_[1]) {
        const uint32_t gap = now - markMs_;
        if (!flag_) { finish(now, model, Result::Fail, "heater started without ever being held by crossover"); break; }
        if (!onTime(gap, delay)) {
          snprintf(m, sizeof(m), "heater started %s after cooling stopped - the delay is %s", secs4(a, sizeof(a), gap),
                   secs4(b, sizeof(b), delay));
          finish(now, model, Result::Fail, m);
          break;
        }
        say(now, "heater ON %s after the compressor stopped - blocked for the full delay", secs4(a, sizeof(a), gap));
        phase(4, now);
      } else if (elapsedMs(now, markMs_) > delay + minutes(1)) finish(now, model, Result::Fail, "heater never started");
      break;

    case 4:   // heating ends: script the cabinet warm at once
      if (passesInPhase_ == 1) {   // the control counts the start on the pass after it goes live
        if (tc.crossovers() != count0_ + 1) { finish(now, model, Result::Fail, "crossover not counted"); break; }
        say(now, "crossover counted (%lu since boot)", (unsigned long)tc.crossovers());
      }
      if (wentOff_[1]) {
        if (d.heat.block != GuardBlock::None) {
          snprintf(m, sizeof(m), "heater stopped by %s, not its run end", guardBlockName(d.heat.block));
          finish(now, model, Result::Fail, m);
          break;
        }
        markMs_ = now;
        flag_ = false;
        model.setCabinetC(21.0f);
        say(now, "heater OFF at %.2f C. Cabinet now scripted to 21.0 C: cooling asked for at the next read. "
                 "The compressor must stay off until %s after the heater stopped", x, secs4(a, sizeof(a), delay));
        phase(5, now);
      } else if (elapsedMs(now, phaseAtMs_) > minutes(3)) finish(now, model, Result::Fail, "heating never reached its run end");
      break;

    case 5:
      if (d.cool.demanded && !v.live.cool && d.cool.block == GuardBlock::Crossover && !flag_) {
        flag_ = true;
        say(now, "cooling asked for, HELD by crossover: %s left", secs4(a, sizeof(a), d.cool.waitMs));
      }
      if (wentOn_[0]) {
        const uint32_t gap = now - markMs_;
        if (!flag_) { finish(now, model, Result::Fail, "compressor started without ever being held by crossover"); break; }
        if (!onTime(gap, delay)) {
          snprintf(m, sizeof(m), "compressor started %s after the heater stopped - the delay is %s", secs4(a, sizeof(a), gap),
                   secs4(b, sizeof(b), delay));
          finish(now, model, Result::Fail, m);
          break;
        }
        say(now, "compressor ON %s after the heater stopped - blocked for the full delay", secs4(a, sizeof(a), gap));
        model.setCabinetC(20.0f);
        phase(6, now);
      } else if (elapsedMs(now, markMs_) > delay + minutes(1)) finish(now, model, Result::Fail, "compressor never started");
      break;

    case 6:
      if (passesInPhase_ == 1) {
        if (tc.crossovers() != count0_ + 2) { finish(now, model, Result::Fail, "second crossover not counted"); break; }
        say(now, "crossover counted (%lu since boot)", (unsigned long)tc.crossovers());
      }
      if (!v.live.cool) {
        snprintf(m, sizeof(m), "both directions held for the full %s; 2 crossovers counted", secs4(a, sizeof(a), delay));
        finish(now, model, Result::Pass, m);
      } else if (elapsedMs(now, phaseAtMs_) > minutes(1)) finish(now, model, Result::Fail, "compressor did not stop at the end");
      break;
  }
}

// ------------------------------------------------------------------ run 9
// B3 test 9, RH stripping. RH scripted high: persistence must run in full (a
// one-read dip restarts it); then stripping calls cooling. The model cools and
// takes water out, so RH stays above the setpoint while vapour pressure falls:
// the strip must end when vapour pressure reaches target, not on RH.
void ScenarioRunner::run9(const ScenarioView& v, Controller& ctl, CabinetModel& model, TemperatureControl& tc) {
  (void)ctl;
  const uint32_t now = v.nowMs;
  const TuningSettings& t = s_.tuning;
  const uint32_t per = t.rhStripPersistMs;
  const float trig = s_.user.rhSetpointPct + t.rhStripOffsetPct;
  const float target = tc.stripTargetVpKpa();
  const CabinetSummary& c = tc.lastCab();
  char a[16], b[16], m[200];

  if (phase_ >= 1 && phase_ <= 4 && tc.stripping()) { finish(now, model, Result::Fail, "stripping started before its persistence ran in full"); return; }

  switch (phase_) {
    case 0:
      say(now, "TEST 9 - RH stripping. Cabinet 20.3 C. RH now scripted to 68%% (trigger %.0f%%, persistence %s). "
               "Release when vapour pressure <= %.3f kPa.", trig, secs4(a, sizeof(a), per), target);
      model.setCabinetRh(68.0f);
      phase(1, now);
      break;

    case 1:
      if (tc.stripPersisting()) {
        say(now, "mean RH %.1f%% seen: persistence counting", c.meanRhPct);
        phase(2, now);
      } else if (elapsedMs(now, phaseAtMs_) > minutes(1)) finish(now, model, Result::Fail, "high RH never seen");
      break;

    case 2:
      if (tc.stripPersistMs(now) >= per / 2) {
        model.setCabinetRh(65.0f);
        say(now, "half way (%s): RH dips to 65%% for a read - persistence must start again from zero",
            secs4(a, sizeof(a), tc.stripPersistMs(now)));
        phase(3, now);
      }
      break;

    case 3:
      if (!tc.stripPersisting()) {
        model.setCabinetRh(68.0f);
        say(now, "dip seen (mean RH %.1f%%): persistence reset. RH back to 68%%", c.meanRhPct);
        phase(4, now);
      } else if (elapsedMs(now, phaseAtMs_) > minutes(1)) finish(now, model, Result::Fail, "the dip did not reset persistence");
      break;

    case 4:
      if (tc.stripPersisting()) {
        markMs_ = now - tc.stripPersistMs(now);
        say(now, "mean RH %.1f%% again: persistence counting from zero", c.meanRhPct);
        phase(5, now);
      } else if (elapsedMs(now, phaseAtMs_) > minutes(1)) finish(now, model, Result::Fail, "high RH not seen again");
      break;

    case 5:
      if (tc.stripping()) {
        const uint32_t gap = now - markMs_;
        if (!onTime(gap, per)) {
          snprintf(m, sizeof(m), "stripping started %s after RH went high - persistence is %s", secs4(a, sizeof(a), gap),
                   secs4(b, sizeof(b), per));
          finish(now, model, Result::Fail, m);
          break;
        }
        say(now, "STRIP STARTED after %s of sustained RH (mean %.1f%% >= %.0f%%): cooling asked for although the cabinet "
                 "is in the dead zone", secs4(a, sizeof(a), gap), c.meanRhPct, trig);
        phase(6, now);
      } else if (elapsedMs(now, markMs_) > per + seconds(5)) finish(now, model, Result::Fail, "stripping did not start");
      break;

    case 6:
      if (v.live.cool) { say(now, "compressor ON for stripping. The model now cools 0.3 C/min and takes water out"); phase(7, now); }
      else if (elapsedMs(now, phaseAtMs_) > minutes(2)) finish(now, model, Result::Fail, "compressor never started for stripping");
      break;

    case 7:
      if (v.sweepDone)
        say(now, "cabinet %.2f C, mean RH %.1f%%, vapour pressure %.3f kPa (release at %.3f) | compressor %s", c.meanC,
            c.meanRhPct, c.meanVpKpa, target, onWord(v.live.cool));
      if (!tc.stripping()) {
        if (tc.lastStripStop() != StripStop::VapourPressure) {
          snprintf(m, sizeof(m), "stripping stopped for '%s', not vapour pressure", stripStopName(tc.lastStripStop()));
          finish(now, model, Result::Fail, m);
          break;
        }
        if (c.meanVpKpa > target) { finish(now, model, Result::Fail, "released above the vapour pressure target"); break; }
        if (!(c.meanRhPct > s_.user.rhSetpointPct)) {
          finish(now, model, Result::Fail, "RH had already fallen to its setpoint - can't tell a VP release from an RH one");
          break;
        }
        say(now, "STRIP STOPPED: vapour pressure %.3f <= %.3f kPa, with RH still %.1f%% - above the %.0f%% setpoint. "
                 "Release followed vapour pressure, not RH", c.meanVpKpa, target, c.meanRhPct, s_.user.rhSetpointPct);
        phase(8, now);
      } else if (elapsedMs(now, phaseAtMs_) > minutes(10)) finish(now, model, Result::Fail, "stripping never released");
      break;

    case 8:
      if (!v.live.cool) {
        snprintf(m, sizeof(m), "persistence ran in full (and restarted after a dip); cooling called; released on vapour pressure "
                               "%.3f kPa with RH %.1f%%", c.meanVpKpa, c.meanRhPct);
        finish(now, model, Result::Pass, m);
      } else if (elapsedMs(now, phaseAtMs_) > s_.safety.compressorMinOnMs + seconds(2))
        finish(now, model, Result::Fail, "compressor still on after the strip ended");
      break;
  }
}

// ------------------------------------------------------------------ run 10
// B3 test 10, RH temperature floor. RH high, one cabinet sensor scripted 1 C
// below the rest; the model cools without removing water, so vapour pressure
// never reaches target: the floor must stop the strip. While below the floor
// persistence must not count; once every sensor is above it, the full
// persistence runs again from zero.
void ScenarioRunner::run10(const ScenarioView& v, Controller& ctl, CabinetModel& model, TemperatureControl& tc) {
  (void)ctl;
  const uint32_t now = v.nowMs;
  const TuningSettings& t = s_.tuning;
  const uint32_t per = t.rhStripPersistMs;
  const float floorC = s_.user.targetTempC - t.rhStripFloorBelowSetpointC;
  const CabinetSummary& c = tc.lastCab();
  char a[16], b[16], m[200];

  switch (phase_) {
    case 0:
      say(now, "TEST 10 - RH temperature floor (%.1f C). RH 69%%; ch2 scripted 1.0 C below the other cabinet sensors. "
               "The model cools 0.5 C/min and removes NO water, so only the floor can stop the strip.", floorC);
      phase(1, now);
      break;

    case 1:
      if (tc.stripping()) { say(now, "STRIP STARTED: mean RH %.1f%%", c.meanRhPct); phase(2, now); }
      else if (elapsedMs(now, phaseAtMs_) > per + minutes(1)) finish(now, model, Result::Fail, "stripping never started");
      break;

    case 2:
      if (v.live.cool) { say(now, "compressor ON for stripping"); phase(3, now); }
      else if (elapsedMs(now, phaseAtMs_) > minutes(2)) finish(now, model, Result::Fail, "compressor never started");
      break;

    case 3:
      if (v.sweepDone)
        say(now, "coldest cabinet sensor %.2f C (floor %.1f), mean %.2f C, RH %.1f%% | compressor %s", c.minC, floorC, c.meanC,
            c.meanRhPct, onWord(v.live.cool));
      if (!tc.stripping()) {
        if (tc.lastStripStop() != StripStop::Floor) {
          snprintf(m, sizeof(m), "stripping stopped for '%s', not the floor", stripStopName(tc.lastStripStop()));
          finish(now, model, Result::Fail, m);
          break;
        }
        if (!(c.minC < floorC)) { finish(now, model, Result::Fail, "floor stop with every sensor above the floor"); break; }
        model.rates().coolCPerMin = 0.0f;       // hold the cabinet where it is
        markMs_ = now;
        say(now, "STRIP STOPPED: temperature floor - coldest sensor %.2f C < %.1f (mean %.2f, no heating call). RH still %.1f%%",
            c.minC, floorC, c.meanC, c.meanRhPct);
        phase(4, now);
      } else if (elapsedMs(now, phaseAtMs_) > minutes(6)) finish(now, model, Result::Fail, "the floor never stopped the strip");
      break;

    case 4:   // below the floor: persistence must not count
      if (tc.stripping() || tc.stripPersisting()) {
        finish(now, model, Result::Fail, "persistence counted while a sensor was below the floor");
        break;
      }
      if (elapsedMs(now, markMs_) >= 2 * per) {
        model.setCabinetC(20.0f);
        say(now, "%s below the floor with RH high: persistence did not count - correct. Cabinet back to 20.0 C "
                 "(every sensor above the floor)", secs4(a, sizeof(a), 2 * per));
        phase(5, now);
      }
      break;

    case 5:
      if (tc.stripPersisting()) {
        markMs_ = now - tc.stripPersistMs(now);
        say(now, "every sensor above the floor (coldest %.2f C): persistence counting from zero", c.minC);
        phase(6, now);
      } else if (elapsedMs(now, phaseAtMs_) > minutes(1)) finish(now, model, Result::Fail, "persistence never restarted");
      break;

    case 6:
      if (tc.stripping()) {
        const uint32_t gap = now - markMs_;
        if (!onTime(gap, per)) {
          snprintf(m, sizeof(m), "restarted %s after the floor cleared - persistence is %s", secs4(a, sizeof(a), gap),
                   secs4(b, sizeof(b), per));
          finish(now, model, Result::Fail, m);
          break;
        }
        snprintf(m, sizeof(m), "the floor stopped the strip; no persistence below it; the full %s again once above it",
                 secs4(a, sizeof(a), per));
        finish(now, model, Result::Pass, m);
      } else if (elapsedMs(now, markMs_) > per + seconds(5)) finish(now, model, Result::Fail, "stripping did not restart");
      break;
  }
}

// ------------------------------------------------------------------ run 17
// B3 test 17, load ineffective. Heater asked for with the model not responding
// (as a tripped cut-out): off at the end of its window, flagged, held for the
// retry time, then tried again (the model responds again, so the flag clears).
// Then cooling with the model not responding: flagged, and cooling carries on.
void ScenarioRunner::run17(const ScenarioView& v, Controller& ctl, CabinetModel& model, TemperatureControl& tc) {
  const uint32_t now = v.nowMs;
  const TuningSettings& t = s_.tuning;
  const GuardDecisions& d = ctl.decisions();
  const CabinetSummary& c = tc.lastCab();
  char a[16], b[16], m[200];
  const uint32_t waitLimit = ctl.hold().remainingMs() + t.crossoverDelayMs + s_.safety.compressorMinOffMs + minutes(2);

  switch (phase_) {
    case 0:
      say(now, "TEST 17 - load ineffective. Cabinet 18.0 C: heating asked for. The model will NOT respond (like a tripped "
               "KSD9700). Heater window %s, needs +%.1f C; retry after %s.", secs4(a, sizeof(a), t.heaterIneffectiveWindowMs),
          t.heaterIneffectiveRiseC, secs4(b, sizeof(b), t.heaterIneffectiveRetryMs));
      phase(1, now);
      break;

    case 1:
      if (wentOn_[1]) { markMs_ = now; say(now, "heater ON, chamber %.2f C", c.chamberC); phase(2, now); }
      else if (elapsedMs(now, phaseAtMs_) > waitLimit) finish(now, model, Result::Fail, "heater never started");
      break;

    case 2:
      if (wentOff_[1]) {
        const uint32_t gap = now - markMs_;
        if (d.heat.block != GuardBlock::Ineffective || !tc.heaterIneffective()) {
          snprintf(m, sizeof(m), "heater went off for '%s', not heater-ineffective", guardBlockName(d.heat.block));
          finish(now, model, Result::Fail, m);
          break;
        }
        if (!onTime(gap, t.heaterIneffectiveWindowMs)) {
          snprintf(m, sizeof(m), "heater flagged after %s - the window is %s", secs4(a, sizeof(a), gap),
                   secs4(b, sizeof(b), t.heaterIneffectiveWindowMs));
          finish(now, model, Result::Fail, m);
          break;
        }
        markMs_ = now;
        flag_ = false;
        say(now, "heater OFF after %s: HEATER INEFFECTIVE (chamber %.2f C), flagged, held off for %s", secs4(a, sizeof(a), gap),
            c.chamberC, secs4(b, sizeof(b), t.heaterIneffectiveRetryMs));
        phase(3, now);
      } else if (elapsedMs(now, markMs_) > t.heaterIneffectiveWindowMs + kLateMs) {
        finish(now, model, Result::Fail, "heater still on after its ineffective window");
      }
      break;

    case 3:   // held for the retry; the model starts responding meanwhile
      if (!flag_ && elapsedMs(now, markMs_) >= seconds(10)) {
        flag_ = true;
        model.rates().heatResponds = true;
        model.rates().heatCPerMin = 0.1f;
        say(now, "the cut-out 'resets': the model responds again, so the retry should pass. Heater %s while held (%s)",
            onWord(v.live.heat), guardBlockName(d.heat.block));
      }
      if (wentOn_[1]) {
        const uint32_t gap = now - markMs_;
        if (!onTime(gap, t.heaterIneffectiveRetryMs)) {
          snprintf(m, sizeof(m), "heater retried after %s - the retry time is %s", secs4(a, sizeof(a), gap),
                   secs4(b, sizeof(b), t.heaterIneffectiveRetryMs));
          finish(now, model, Result::Fail, m);
          break;
        }
        markMs_ = now;
        say(now, "heater ON again after %s - retried. Chamber %.2f C", secs4(a, sizeof(a), gap), c.chamberC);
        phase(4, now);
      } else if (elapsedMs(now, markMs_) > t.heaterIneffectiveRetryMs + seconds(5)) {
        finish(now, model, Result::Fail, "heater was not retried");
      }
      break;

    case 4:
      if (!tc.heaterIneffective()) {
        say(now, "chamber %.2f C: HEATER EFFECTIVE AGAIN - flag cleared. Cabinet to 19.5 C (heating no longer asked for)", c.chamberC);
        model.setCabinetC(19.5f);
        phase(5, now);
      } else if (elapsedMs(now, markMs_) > t.heaterIneffectiveWindowMs + kLateMs) {
        finish(now, model, Result::Fail, "the retried heater did not clear the flag");
      }
      break;

    case 5:
      if (!v.live.heat) {
        model.rates().coolResponds = false;
        model.setCabinetC(21.0f);
        say(now, "heater off. Now cooling: cabinet 21.0 C, the model will NOT respond (like a failed SSR or fridge). "
                 "Window %s, needs -%.1f C", secs4(a, sizeof(a), t.coolingIneffectiveWindowMs), t.coolingIneffectiveFallC);
        phase(6, now);
      } else if (elapsedMs(now, phaseAtMs_) > minutes(1)) finish(now, model, Result::Fail, "heater did not stop");
      break;

    case 6:
      if (wentOn_[0]) { markMs_ = now; say(now, "compressor ON, chamber %.2f C", c.chamberC); phase(7, now); }
      else if (elapsedMs(now, phaseAtMs_) > waitLimit) finish(now, model, Result::Fail, "compressor never started");
      break;

    case 7:
      if (wentOff_[0]) { finish(now, model, Result::Fail, "compressor stopped - cooling must carry on when ineffective"); break; }
      if (tc.coolingIneffective()) {
        const uint32_t gap = now - markMs_;
        if (!onTime(gap, t.coolingIneffectiveWindowMs)) {
          snprintf(m, sizeof(m), "cooling flagged after %s - the window is %s", secs4(a, sizeof(a), gap),
                   secs4(b, sizeof(b), t.coolingIneffectiveWindowMs));
          finish(now, model, Result::Fail, m);
          break;
        }
        markMs_ = now;
        say(now, "COOLING INEFFECTIVE after %s (chamber %.2f C), flagged. Compressor %s - still called", secs4(a, sizeof(a), gap),
            c.chamberC, onWord(v.live.cool));
        phase(8, now);
      } else if (elapsedMs(now, markMs_) > t.coolingIneffectiveWindowMs + kLateMs) {
        finish(now, model, Result::Fail, "cooling not flagged at the end of its window");
      }
      break;

    case 8:
      if (wentOff_[0]) { finish(now, model, Result::Fail, "compressor stopped after the flag - it must carry on"); break; }
      if (elapsedMs(now, markMs_) >= seconds(10)) {
        say(now, "compressor still ON 10 s after the flag - correct. Cabinet to 19.9 C to end the run");
        model.setCabinetC(19.9f);
        phase(9, now);
      }
      break;

    case 9:
      if (!v.live.cool)
        finish(now, model, Result::Pass, "heater: off at its window, flagged, held, retried, flag cleared. Cooling: flagged, "
                                          "kept running (the cooling flag stays until a later run passes)");
      else if (elapsedMs(now, phaseAtMs_) > minutes(1)) finish(now, model, Result::Fail, "compressor did not stop at the end");
      break;
  }
}

// ------------------------------------------------------------------ run 23
// B3 test 23, plenum fan rules. Idle; idle duty set to 0 (fan stopped, as B6 may
// choose) so every start is from 0 %; a compressor run and its harvest; a heater
// run and its harvest; idle duty back; then 10 % commanded and refused.
// Checked on every pass: the fan runs whenever the compressor or heater runs;
// every start from 0 % is a 1 s kickstart at 100 %.
void ScenarioRunner::run23(const ScenarioView& v, Controller& ctl, CabinetModel& model, TemperatureControl& tc) {
  const uint32_t now = v.nowMs;
  const TuningSettings& t = s_.tuning;
  const uint32_t kick = s_.safety.fanKickstartMs;
  char a[16], b[16], m[200];
  const uint32_t waitLimit = ctl.hold().remainingMs() + t.crossoverDelayMs + s_.safety.compressorMinOffMs + minutes(2);

  if ((v.live.cool || v.live.heat) && v.fanOut < s_.safety.fanMinDutyPct) {
    finish(now, model, Result::Fail, "the fan was not running while the compressor or heater ran");
    return;
  }
  if (v.fanOut == 100 && v.fanTarget != 100 && !kicking_) { kicking_ = true; kickAtMs_ = now; }
  if (kicking_ && v.fanOut != 100) {
    kicking_ = false;
    kicks_++;
    const uint32_t dur = now - kickAtMs_;
    if (!onTime(dur, kick)) {
      snprintf(m, sizeof(m), "kickstart lasted %s - it must be %s", secs4(a, sizeof(a), dur), secs4(b, sizeof(b), kick));
      finish(now, model, Result::Fail, m);
      return;
    }
    say(now, "kickstart %d: 100%% for %s, then %u%%", kicks_, secs4(a, sizeof(a), dur), (unsigned)v.fanOut);
  }

  switch (phase_) {
    case 0:
      say(now, "TEST 23 - plenum fan rules. Idle first. Listen to the plenum fan.");
      phase(1, now);
      break;

    case 1:
      if (elapsedMs(now, phaseAtMs_) < seconds(5)) break;
      if (v.fanOut != t.fanIdlePct) {
        snprintf(m, sizeof(m), "idle fan %u%% - should be %u%%", (unsigned)v.fanOut, (unsigned)t.fanIdlePct);
        finish(now, model, Result::Fail, m);
        break;
      }
      tc.setIdleFanOverride(0);
      say(now, "idle: fan %u%% - correct. Idle duty now 0 (fan stopped at idle), so each start below is from 0%%", (unsigned)v.fanOut);
      phase(2, now);
      break;

    case 2:
      if (elapsedMs(now, phaseAtMs_) < seconds(8)) break;
      if (v.fanOut != 0) { finish(now, model, Result::Fail, "fan did not stop at idle duty 0"); break; }
      count0_ = (uint32_t)kicks_;
      model.setCabinetC(21.0f);
      say(now, "fan stopped. Cabinet 21.0 C: cooling asked for. The fan must kickstart, then run at %u%% with the compressor",
          (unsigned)t.fanCompressorPct);
      phase(3, now);
      break;

    case 3:
      if (wentOn_[0]) {
        if ((uint32_t)kicks_ + (kicking_ ? 1u : 0u) < count0_ + 1) { finish(now, model, Result::Fail, "compressor started without a fan kickstart"); break; }
        say(now, "compressor ON, fan %u%%", (unsigned)v.fanOut);
        phase(4, now);
      } else if (elapsedMs(now, phaseAtMs_) > waitLimit) finish(now, model, Result::Fail, "compressor never started");
      break;

    case 4:
      if (v.live.cool && !kicking_ && v.fanOut != t.fanCompressorPct) {
        snprintf(m, sizeof(m), "fan %u%% during the compressor run - should be %u%%", (unsigned)v.fanOut, (unsigned)t.fanCompressorPct);
        finish(now, model, Result::Fail, m);
        break;
      }
      if (wentOff_[0]) {
        say(now, "compressor OFF at its run end: cooling harvest starts");
        fanMax_ = 0;
        fanMin_ = 255;
        fanPrev_ = 255;
        phase(5, now);
      } else if (elapsedMs(now, phaseAtMs_) > minutes(3)) finish(now, model, Result::Fail, "the compressor run did not end");
      break;

    case 5:
      if (tc.harvest() == HarvestKind::Cooling) {
        if (passesInPhase_ == 1 && v.fanOut != t.fanCoolHarvestStartPct) {
          snprintf(m, sizeof(m), "cooling harvest began at fan %u%% - should be %u%%", (unsigned)v.fanOut, (unsigned)t.fanCoolHarvestStartPct);
          finish(now, model, Result::Fail, m);
          break;
        }
        if (v.fanOut > fanPrev_) { finish(now, model, Result::Fail, "cooling harvest fan duty rose - it must only fall"); break; }
        if (v.fanOut < t.fanCoolHarvestEndPct) { finish(now, model, Result::Fail, "cooling harvest fan duty fell below its end duty"); break; }
        if (v.fanOut != fanPrev_ && fanPrev_ != 255)
          say(now, "harvest fan %u%% (chamber %.2f C from the cabinet)", (unsigned)v.fanOut, fabsf(tc.lastCab().chamberC - tc.lastCab().meanC));
        fanPrev_ = v.fanOut;
        if (v.fanOut > fanMax_) fanMax_ = v.fanOut;
        if (v.fanOut < fanMin_) fanMin_ = v.fanOut;
        break;
      }
      if (passesInPhase_ == 1) { finish(now, model, Result::Fail, "no cooling harvest after the compressor stopped"); break; }
      if (tc.lastHarvestEnd() != HarvestEnd::Convergence) {
        snprintf(m, sizeof(m), "cooling harvest ended by '%s' - should be chamber convergence", harvestEndName(tc.lastHarvestEnd()));
        finish(now, model, Result::Fail, m);
        break;
      }
      if (fanMin_ > t.fanCoolHarvestEndPct + 10) {
        snprintf(m, sizeof(m), "cooling harvest fan only fell to %u%% - should approach %u%% at convergence", (unsigned)fanMin_,
                 (unsigned)t.fanCoolHarvestEndPct);
        finish(now, model, Result::Fail, m);
        break;
      }
      say(now, "cooling harvest ENDED on chamber convergence after %s (backstop %s); fan %u%% falling to %u%%",
          secs4(a, sizeof(a), tc.lastHarvestMs()), secs4(b, sizeof(b), t.harvestBackstopMs), (unsigned)fanMax_, (unsigned)fanMin_);
      count0_ = (uint32_t)kicks_;
      model.setCabinetC(18.0f);
      say(now, "cabinet 18.0 C: heating asked for. Fan must kickstart, then %u%% with the heater", (unsigned)t.fanHeatingPct);
      phase(6, now);
      break;

    case 6:
      if (wentOn_[1]) {
        if ((uint32_t)kicks_ + (kicking_ ? 1u : 0u) < count0_ + 1) { finish(now, model, Result::Fail, "heater started without a fan kickstart"); break; }
        say(now, "heater ON, fan %u%%", (unsigned)v.fanOut);
        phase(7, now);
      } else if (elapsedMs(now, phaseAtMs_) > waitLimit) finish(now, model, Result::Fail, "heater never started");
      break;

    case 7:
      if (v.live.heat && !kicking_ && v.fanOut != t.fanHeatingPct) {
        snprintf(m, sizeof(m), "fan %u%% during the heater run - should be %u%%", (unsigned)v.fanOut, (unsigned)t.fanHeatingPct);
        finish(now, model, Result::Fail, m);
        break;
      }
      if (wentOff_[1]) { say(now, "heater OFF at its run end: heating harvest starts"); phase(8, now); }
      else if (elapsedMs(now, phaseAtMs_) > minutes(3)) finish(now, model, Result::Fail, "the heater run did not end");
      break;

    case 8:
      if (tc.harvest() == HarvestKind::Heating) {
        if (v.fanOut != t.fanHeatingPct) {
          snprintf(m, sizeof(m), "fan %u%% during the heating harvest - should be %u%%", (unsigned)v.fanOut, (unsigned)t.fanHeatingPct);
          finish(now, model, Result::Fail, m);
        }
        break;
      }
      if (passesInPhase_ == 1) { finish(now, model, Result::Fail, "no heating harvest after the heater stopped"); break; }
      if (tc.lastHarvestEnd() != HarvestEnd::Convergence) {
        snprintf(m, sizeof(m), "heating harvest ended by '%s' - should be chamber convergence", harvestEndName(tc.lastHarvestEnd()));
        finish(now, model, Result::Fail, m);
        break;
      }
      say(now, "heating harvest ENDED on chamber convergence after %s (backstop %s), fan %u%% throughout",
          secs4(a, sizeof(a), tc.lastHarvestMs()), secs4(b, sizeof(b), t.harvestBackstopMs), (unsigned)t.fanHeatingPct);
      tc.setIdleFanOverride(-1);
      say(now, "idle duty back to %u%%", (unsigned)t.fanIdlePct);
      phase(9, now);
      break;

    case 9:
      if (elapsedMs(now, phaseAtMs_) < seconds(4)) break;
      if (v.fanOut != t.fanIdlePct) { finish(now, model, Result::Fail, "fan did not return to its idle duty"); break; }
      if (tc.setIdleFanOverride(10)) { finish(now, model, Result::Fail, "10% duty was ACCEPTED - 1-19% must be refused"); break; }
      say(now, "10%% commanded: REFUSED (1-19%% is never commanded); fan stays %u%%", (unsigned)v.fanOut);
      phase(10, now);
      break;

    case 10:
      if (passesInPhase_ < 3) break;
      if (v.fanOut != t.fanIdlePct || tc.idleFanOverride() != -1) { finish(now, model, Result::Fail, "the refused command changed the fan"); break; }
      snprintf(m, sizeof(m), "fan ran with every load; %d kickstarts, each 1 s; both harvests ended on convergence; 10%% refused",
               kicks_);
      finish(now, model, Result::Pass, m);
      break;
  }
}

}  // namespace wineador

#endif  // WINEADOR_TEST_BUILD
