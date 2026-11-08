// TEST BUILDS ONLY — the stage 5 scenarios (B3 tests 19, 20, 21, 22): the humidifier.
//
// Like stage 4, they script the cabinet model and let the real humidifier control
// and guards decide. Run 21 also "demands humidify" directly (its demand() is
// added on top of the control's), because the control itself never asks for the
// humidifier during a strip.
#include "wineador/scenario.h"

#if defined(WINEADOR_TEST_BUILD)

#include <stdio.h>

namespace wineador {

namespace {

constexpr uint32_t kLateMs = 500;
constexpr uint32_t kEarlyMs = 5;

const char* secs5(char* out, size_t n, uint32_t ms) {
  if (ms < 60000) snprintf(out, n, "%lu.%lu s", (unsigned long)(ms / 1000), (unsigned long)((ms % 1000) / 100));
  else snprintf(out, n, "%lu:%02lu.%lu", (unsigned long)(ms / 60000), (unsigned long)((ms / 1000) % 60),
                (unsigned long)((ms % 1000) / 100));
  return out;
}
bool onTime5(uint32_t got, uint32_t want) { return got + kEarlyMs >= want && got <= want + kLateMs; }
const char* onWord5(bool b) { return b ? "ON" : "off"; }

}  // namespace

// ------------------------------------------------------------------ run 19
// B3 test 19, humidifier guards. RH stepped below the band: on after the full
// persistence; stops at its max run although still asked for; back on after
// exactly its min off with no fresh count; RH stepped to setpoint: off at once
// (no min on); a quick dip below the band and back is not enough to start it.
void ScenarioRunner::run19(const ScenarioView& v, Controller& ctl, CabinetModel& model, TemperatureControl& tc) {
  (void)tc;
  const uint32_t now = v.nowMs;
  const SafetySettings& f = s_.safety;
  const TuningSettings& t = s_.tuning;
  const HumidControl& hc = *v.hc;
  const GuardDecisions& d = ctl.decisions();
  const CabinetSummary& c = tc.lastCab();
  char a[16], b[16], m[200];

  switch (phase_) {
    case 0:
      say(now, "TEST 19 - humidifier guards. RH scripted to 56%% (call at <= %.0f%% for %s; off at %.0f%%). "
               "Max run %s, min off %s, no min on. Watch the humidifier fan on J11.", hc.triggerPct(),
          secs5(a, sizeof(a), t.humidifyPersistMs), s_.user.rhSetpointPct, secs5(b, sizeof(b), f.humidMaxRunMs),
          secs5(m, sizeof(m), f.humidMinOffMs));
      phase(1, now);
      break;

    case 1:
      if (hc.persisting()) {
        markMs_ = now - hc.persistMs(now);
        say(now, "mean RH %.1f%% seen: persistence counting", c.meanRhPct);
        phase(2, now);
      } else if (elapsedMs(now, phaseAtMs_) > minutes(1)) finish(now, model, Result::Fail, "low RH never seen");
      break;

    case 2:
      if (wentOn_[2]) {
        const uint32_t gap = now - markMs_;
        if (!onTime5(gap, t.humidifyPersistMs)) {
          snprintf(m, sizeof(m), "humidifier started %s after RH went low - persistence is %s", secs5(a, sizeof(a), gap),
                   secs5(b, sizeof(b), t.humidifyPersistMs));
          finish(now, model, Result::Fail, m);
          break;
        }
        count0_ = hc.calls();
        say(now, "humidifier ON after %s of sustained low RH. It must stop at its %s max run, still asked for",
            secs5(a, sizeof(a), gap), secs5(b, sizeof(b), f.humidMaxRunMs));
        phase(3, now);
      } else if (elapsedMs(now, markMs_) > t.humidifyPersistMs + seconds(5)) finish(now, model, Result::Fail, "humidifier did not start");
      break;

    case 3:
      if (v.sweepDone) say(now, "mean RH %.1f%% | humidifier %s", c.meanRhPct, onWord5(v.live.humid));
      if (wentOff_[2]) {
        const uint32_t ran = now - onAtMs_[2];
        if (d.humid.block != GuardBlock::MaxRun) {
          snprintf(m, sizeof(m), "humidifier stopped by '%s' - expected its max run", guardBlockName(d.humid.block));
          finish(now, model, Result::Fail, m);
          break;
        }
        if (!onTime5(ran, f.humidMaxRunMs)) {
          snprintf(m, sizeof(m), "max run stop after %s - the max run is %s", secs5(a, sizeof(a), ran), secs5(b, sizeof(b), f.humidMaxRunMs));
          finish(now, model, Result::Fail, m);
          break;
        }
        if (!hc.call()) { finish(now, model, Result::Fail, "the call ended at the max run - it must stay on"); break; }
        say(now, "humidifier OFF after %s: max run. Still asked for (RH %.1f%%): back on after exactly its %s min off",
            secs5(a, sizeof(a), ran), c.meanRhPct, secs5(b, sizeof(b), f.humidMinOffMs));
        phase(4, now);
      }
      break;

    case 4:
      if (wentOn_[2]) {
        const uint32_t gap = now - offAtMs_[2];
        if (!onTime5(gap, f.humidMinOffMs)) {
          snprintf(m, sizeof(m), "back on after %s - the min off is %s", secs5(a, sizeof(a), gap), secs5(b, sizeof(b), f.humidMinOffMs));
          finish(now, model, Result::Fail, m);
          break;
        }
        if (hc.calls() != count0_) { finish(now, model, Result::Fail, "a fresh call was counted - the call must carry on"); break; }
        say(now, "humidifier ON again after %s (its min off), same call - no fresh count", secs5(a, sizeof(a), gap));
        phase(5, now);
      } else if (elapsedMs(now, phaseAtMs_) > f.humidMinOffMs + seconds(5)) finish(now, model, Result::Fail, "humidifier did not come back");
      break;

    case 5:
      if (elapsedMs(now, phaseAtMs_) >= seconds(5)) {
        model.setCabinetRh(62.5f);
        say(now, "RH stepped to 62.5%% (above setpoint): the humidifier must stop at the next read - no min on");
        phase(6, now);
      }
      break;

    case 6:
      if (wentOff_[2]) {
        const uint32_t ran = now - onAtMs_[2];
        if (d.humid.block != GuardBlock::None || hc.call()) { finish(now, model, Result::Fail, "humidifier stopped, but not because the call ended"); break; }
        say(now, "humidifier OFF at mean RH %.1f%% after only %s on - released at setpoint, no min on", c.meanRhPct, secs5(a, sizeof(a), ran));
        model.setCabinetRh(59.0f);
        say(now, "quick re-ask: RH dips to 59%% for one read, then back above the band - not long enough to start it");
        phase(7, now);
      } else if (elapsedMs(now, phaseAtMs_) > seconds(20)) finish(now, model, Result::Fail, "humidifier did not stop at setpoint");
      break;

    case 7:
      if (wentOn_[2]) { finish(now, model, Result::Fail, "humidifier started without a full persistence"); break; }
      if (!flag_ && hc.persisting()) {
        flag_ = true;
        model.setCabinetRh(61.0f);
        say(now, "dip seen: persistence counting. RH back to 61%% (above the band)");
      }
      if (flag_ && !hc.persisting()) {
        say(now, "persistence broken after %s - humidifier stayed off", secs5(a, sizeof(a), elapsedMs(now, phaseAtMs_)));
        finish(now, model, Result::Pass, "on after full persistence; off at max run, back after exactly the min off with the "
                                         "same call; off at setpoint at once (no min on); a short dip did not start it");
      } else if (elapsedMs(now, phaseAtMs_) > minutes(1)) finish(now, model, Result::Fail, "the dip was not seen or never cleared");
      break;
  }
}

// ------------------------------------------------------------------ run 20
// B3 test 20, humidifier ceiling and sensors. RH 50 %, humidifier running.
// One cabinet sensor scripted to 70.5 % (mean still low): off at once; back
// after its min off when it drops, same call. Then cabinet sensors excluded
// one at a time (scripted implausible temperature): runs on with 3, off with 2;
// both back: on again only after the recovery hold.
void ScenarioRunner::run20(const ScenarioView& v, Controller& ctl, CabinetModel& model, TemperatureControl& tc) {
  const uint32_t now = v.nowMs;
  const SafetySettings& f = s_.safety;
  const HumidControl& hc = *v.hc;
  const GuardDecisions& d = ctl.decisions();
  const CabinetSummary& c = tc.lastCab();
  const SensorHealth& h = *v.health;
  char a[16], b[16], m[200];

  switch (phase_) {
    case 0:
      say(now, "TEST 20 - humidifier ceiling and sensors. RH 50%%: the humidifier will be asked for.");
      phase(1, now);
      break;

    case 1:
      if (wentOn_[2]) {
        count0_ = hc.calls();
        say(now, "humidifier ON (mean RH %.1f%%)", c.meanRhPct);
        phase(2, now);
      } else if (elapsedMs(now, phaseAtMs_) > minutes(2)) finish(now, model, Result::Fail, "humidifier never started");
      break;

    case 2:
      if (elapsedMs(now, phaseAtMs_) >= seconds(10)) {
        model.setOffset(1, 0, 70.5f - model.rhPct(1));
        say(now, "ch3 scripted to 70.5%% RH; the mean stays near 55%%. At the next read the humidifier must stop at once");
        phase(3, now);
      }
      break;

    case 3:
      if (wentOff_[2]) {
        if (d.humid.block != GuardBlock::RhCeiling) {
          snprintf(m, sizeof(m), "humidifier stopped by '%s' - expected the RH ceiling", guardBlockName(d.humid.block));
          finish(now, model, Result::Fail, m);
          break;
        }
        say(now, "humidifier OFF: RH CEILING - one sensor at %.1f%%, mean %.1f%%. Call still on (%s)", c.maxRhPct, c.meanRhPct,
            hc.call() ? "yes" : "NO");
        if (!hc.call()) { finish(now, model, Result::Fail, "the ceiling ended the call - it must only pause it"); break; }
        phase(4, now);
      } else if (elapsedMs(now, phaseAtMs_) > seconds(20)) finish(now, model, Result::Fail, "the ceiling did not stop the humidifier");
      break;

    case 4:
      if (elapsedMs(now, phaseAtMs_) >= seconds(5)) {
        model.setOffset(1, 0, 0);
        say(now, "ch3 back to the cabinet value: the humidifier resumes after its min off, same call");
        phase(5, now);
      }
      break;

    case 5:
      if (wentOn_[2]) {
        if (hc.calls() != count0_) { finish(now, model, Result::Fail, "a fresh call was counted after the ceiling"); break; }
        say(now, "humidifier ON again, same call - no fresh count");
        phase(6, now);
      } else if (elapsedMs(now, phaseAtMs_) > f.humidMinOffMs + seconds(20)) finish(now, model, Result::Fail, "humidifier did not resume");
      break;

    case 6:
      if (elapsedMs(now, phaseAtMs_) >= seconds(5)) {
        model.setOffset(0, -60.0f, 0);
        say(now, "ch2 scripted to an impossible temperature: it will be excluded, leaving 3 of 4. The humidifier must keep running");
        phase(7, now);
      }
      break;

    case 7:
      if (wentOff_[2]) { finish(now, model, Result::Fail, "humidifier stopped with 3 cabinet sensors still healthy"); break; }
      if (!flag_ && !h.healthy(0)) {
        flag_ = true;
        markMs_ = now;
        say(now, "ch2 EXCLUDED: %d of 4 cabinet sensors healthy - humidifier still %s", h.healthyCabinet(), onWord5(v.live.humid));
      }
      if (flag_ && elapsedMs(now, markMs_) >= seconds(15)) {
        model.setOffset(2, -60.0f, 0);
        say(now, "still running 15 s later - correct. Now ch4 too: 2 of 4 left, the humidifier must stop");
        flag_ = false;
        phase(8, now);
      } else if (!flag_ && elapsedMs(now, phaseAtMs_) > seconds(40)) finish(now, model, Result::Fail, "ch2 was not excluded");
      break;

    case 8:
      if (wentOff_[2]) {
        if (d.humid.block != GuardBlock::Sensors || h.healthyCabinet() >= f.minHealthyCabinet) {
          snprintf(m, sizeof(m), "humidifier stopped by '%s' with %d cabinet sensors", guardBlockName(d.humid.block), h.healthyCabinet());
          finish(now, model, Result::Fail, m);
          break;
        }
        model.setOffset(0, 0, 0);
        model.setOffset(2, 0, 0);
        say(now, "humidifier OFF: SENSORS - %d of 4 healthy. Both sensors back now: each needs 10 good reads (about 2.5 min), "
                 "then the %s recovery hold", h.healthyCabinet(), secs5(a, sizeof(a), f.sensorRecoveryHoldMs));
        expectMs_ = 0;
        phase(9, now);
      } else if (elapsedMs(now, phaseAtMs_) > seconds(40)) finish(now, model, Result::Fail, "humidifier did not stop at 2 of 4");
      break;

    case 9:
      if (expectMs_ == 0 && h.healthyCabinet() >= f.minHealthyCabinet) {
        expectMs_ = now;
        say(now, "%d of 4 cabinet sensors healthy again: recovery hold %s starts", h.healthyCabinet(), secs5(a, sizeof(a), f.sensorRecoveryHoldMs));
      }
      if (wentOn_[2]) {
        if (expectMs_ == 0) { finish(now, model, Result::Fail, "humidifier resumed before the sensors recovered"); break; }
        const uint32_t gap = now - expectMs_;
        if (!onTime5(gap, f.sensorRecoveryHoldMs)) {
          snprintf(m, sizeof(m), "resumed %s after the sensors recovered - the hold is %s", secs5(a, sizeof(a), gap),
                   secs5(b, sizeof(b), f.sensorRecoveryHoldMs));
          finish(now, model, Result::Fail, m);
          break;
        }
        snprintf(m, sizeof(m), "off at once at the ceiling (mean low); ran on with 3 of 4; off at 2 of 4; resumed %s after "
                               "recovery (the hold)", secs5(a, sizeof(a), gap));
        finish(now, model, Result::Pass, m);
      } else if (elapsedMs(now, phaseAtMs_) > minutes(6)) finish(now, model, Result::Fail, "humidifier never resumed");
      break;
  }
}

// ------------------------------------------------------------------ run 21
// B3 test 21, humidifier exclusivity. Humidify demanded during a strip (blocked
// by stripping); during a temperature cooling run with "humidify during cooling
// runs" off (blocked), then switched on (allowed).
void ScenarioRunner::run21(const ScenarioView& v, Controller& ctl, CabinetModel& model, TemperatureControl& tc) {
  const uint32_t now = v.nowMs;
  const GuardDecisions& d = ctl.decisions();
  char m[160];

  if (phase_ >= 1 && phase_ <= 5 && wentOn_[2]) {
    snprintf(m, sizeof(m), "humidifier STARTED while blocked (phase %d, %s)", phase_, guardBlockName(d.humid.block));
    finish(now, model, Result::Fail, m);
    return;
  }

  switch (phase_) {
    case 0:
      say(now, "TEST 21 - humidifier exclusivity. RH 68%%: a strip will start.");
      phase(1, now);
      break;

    case 1:
      if (tc.stripping() && v.live.cool) {
        demand_.humid = true;
        say(now, "strip running, compressor ON. Humidify DEMANDED now: it must stay off (stripping)");
        phase(2, now);
      } else if (elapsedMs(now, phaseAtMs_) > minutes(2)) finish(now, model, Result::Fail, "the strip never started");
      break;

    case 2:
      if (passesInPhase_ > 2 && d.humid.block != GuardBlock::Stripping) {
        snprintf(m, sizeof(m), "humidify held by '%s' during the strip - expected stripping", guardBlockName(d.humid.block));
        finish(now, model, Result::Fail, m);
        break;
      }
      if (elapsedMs(now, phaseAtMs_) >= seconds(20)) {
        demand_.humid = false;
        model.setCabinetRh(61.5f);   // vapour pressure clearly below the release target (62 % would sit exactly on it)
        say(now, "held 20 s by stripping - correct. Demand off; RH to 61.5%% so the strip releases");
        phase(3, now);
      }
      break;

    case 3:
      if (!tc.stripping() && !v.live.cool) {
        model.setCabinetC(21.0f);
        model.setCabinetRh(62.0f);
        say(now, "strip over. Cabinet 21.0 C: a temperature cooling run. 'Humidify during cooling runs' is %s",
            ctl.humidifyDuringCooling() ? "ON" : "OFF");
        phase(4, now);
      } else if (elapsedMs(now, phaseAtMs_) > minutes(1)) finish(now, model, Result::Fail, "the strip did not release");
      break;

    case 4:
      if (v.live.cool) {
        demand_.humid = true;
        say(now, "compressor ON (temperature). Humidify DEMANDED with the setting OFF: it must stay off (compressor-run)");
        phase(5, now);
      } else if (elapsedMs(now, phaseAtMs_) > minutes(2)) finish(now, model, Result::Fail, "the cooling run never started");
      break;

    case 5:
      if (passesInPhase_ > 2 && d.humid.block != GuardBlock::CompressorRun) {
        snprintf(m, sizeof(m), "humidify held by '%s' in the cooling run - expected compressor-run", guardBlockName(d.humid.block));
        finish(now, model, Result::Fail, m);
        break;
      }
      if (elapsedMs(now, phaseAtMs_) >= seconds(20)) {
        ctl.setHumidifyDuringCooling(true);
        say(now, "held 20 s - correct. 'Humidify during cooling runs' switched ON: the humidifier must start");
        phase(6, now);
      }
      break;

    case 6:
      if (wentOn_[2]) {
        if (!v.live.cool) { finish(now, model, Result::Fail, "the cooling run had ended - not a test of the setting"); break; }
        say(now, "humidifier ON during the cooling run - allowed with the setting on");
        phase(7, now);
      } else if (elapsedMs(now, phaseAtMs_) > seconds(5)) finish(now, model, Result::Fail, "humidifier did not start with the setting on");
      break;

    case 7:
      if (elapsedMs(now, phaseAtMs_) >= seconds(5)) {
        demand_.humid = false;
        ctl.setHumidifyDuringCooling(s_.tuning.humidifyDuringCooling);
        model.setCabinetC(20.0f);
        say(now, "demand off, setting back to %s, cabinet 20.0 C", s_.tuning.humidifyDuringCooling ? "on" : "off");
        phase(8, now);
      }
      break;

    case 8:
      if (!v.live.humid && !v.live.cool)
        finish(now, model, Result::Pass, "blocked during the strip; blocked in a temperature run with the setting off; allowed with it on");
      else if (elapsedMs(now, phaseAtMs_) > minutes(1)) finish(now, model, Result::Fail, "loads did not stop at the end");
      break;
  }
}

// ------------------------------------------------------------------ run 22
// B3 test 22, humidifier ineffective. The model adds no water (vapour pressure
// flat): off at the end of the window, flagged, held for the retry time, then
// tried again; the model responds by then, so the flag clears.
void ScenarioRunner::run22(const ScenarioView& v, Controller& ctl, CabinetModel& model, TemperatureControl& tc) {
  const uint32_t now = v.nowMs;
  const TuningSettings& t = s_.tuning;
  const HumidControl& hc = *v.hc;
  const GuardDecisions& d = ctl.decisions();
  const CabinetSummary& c = tc.lastCab();
  char a[16], b[16], m[200];

  switch (phase_) {
    case 0:
      say(now, "TEST 22 - humidifier ineffective. RH 56%%; the model adds NO water (like an empty tank). Window %s, needs "
               "+%.4f kPa; retry after %s.", secs5(a, sizeof(a), t.humidIneffectiveWindowMs), hc.riseNeededKpa(),
          secs5(b, sizeof(b), t.humidIneffectiveRetryMs));
      phase(1, now);
      break;

    case 1:
      if (wentOn_[2]) { markMs_ = now; say(now, "humidifier ON, vapour pressure %.4f kPa", c.meanVpKpa); phase(2, now); }
      else if (elapsedMs(now, phaseAtMs_) > minutes(2)) finish(now, model, Result::Fail, "humidifier never started");
      break;

    case 2:
      if (wentOff_[2]) {
        const uint32_t gap = now - markMs_;
        if (d.humid.block != GuardBlock::HumidIneffective || !hc.ineffective()) {
          snprintf(m, sizeof(m), "humidifier went off for '%s', not humid-ineffective", guardBlockName(d.humid.block));
          finish(now, model, Result::Fail, m);
          break;
        }
        if (!onTime5(gap, t.humidIneffectiveWindowMs)) {
          snprintf(m, sizeof(m), "flagged after %s - the window is %s", secs5(a, sizeof(a), gap), secs5(b, sizeof(b), t.humidIneffectiveWindowMs));
          finish(now, model, Result::Fail, m);
          break;
        }
        markMs_ = now;
        flag_ = false;
        say(now, "humidifier OFF after %s: HUMIDIFIER INEFFECTIVE, flagged, held off for %s (call still on: %s)",
            secs5(a, sizeof(a), gap), secs5(b, sizeof(b), t.humidIneffectiveRetryMs), hc.call() ? "yes" : "NO");
        phase(3, now);
      } else if (elapsedMs(now, markMs_) > t.humidIneffectiveWindowMs + kLateMs) {
        finish(now, model, Result::Fail, "humidifier still on after its ineffective window");
      }
      break;

    case 3:
      if (!flag_ && elapsedMs(now, markMs_) >= seconds(10)) {
        flag_ = true;
        model.rates().humidResponds = true;
        say(now, "tank 'refilled': the model adds water again, so the retry should pass. Humidifier %s (%s)",
            onWord5(v.live.humid), guardBlockName(d.humid.block));
      }
      if (wentOn_[2]) {
        const uint32_t gap = now - markMs_;
        if (!onTime5(gap, t.humidIneffectiveRetryMs)) {
          snprintf(m, sizeof(m), "retried after %s - the retry time is %s", secs5(a, sizeof(a), gap), secs5(b, sizeof(b), t.humidIneffectiveRetryMs));
          finish(now, model, Result::Fail, m);
          break;
        }
        markMs_ = now;
        say(now, "humidifier ON again after %s - retried", secs5(a, sizeof(a), gap));
        phase(4, now);
      } else if (elapsedMs(now, markMs_) > t.humidIneffectiveRetryMs + seconds(5)) finish(now, model, Result::Fail, "humidifier was not retried");
      break;

    case 4:
      if (!hc.ineffective()) {
        say(now, "vapour pressure %.4f kPa: HUMIDIFIER EFFECTIVE AGAIN - flag cleared. RH to 63%% to end the call", c.meanVpKpa);
        model.setCabinetRh(63.0f);
        phase(5, now);
      } else if (elapsedMs(now, markMs_) > t.humidIneffectiveWindowMs + kLateMs) finish(now, model, Result::Fail, "the retry did not clear the flag");
      break;

    case 5:
      if (!v.live.humid) finish(now, model, Result::Pass, "off at the end of its window, flagged, held for the retry time, retried, flag cleared");
      else if (elapsedMs(now, phaseAtMs_) > seconds(30)) finish(now, model, Result::Fail, "humidifier did not stop at the end");
      break;
  }
}

}  // namespace wineador

#endif  // WINEADOR_TEST_BUILD
