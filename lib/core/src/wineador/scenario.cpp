#include "wineador/scenario.h"

#if defined(WINEADOR_TEST_BUILD)

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace wineador {

namespace {

constexpr uint32_t kLateMs = 500;   // a timed change may come this much after its expected time
constexpr uint32_t kEarlyMs = 5;    // ...and never more than this before it

bool liveOf(const LoadOutputs& o, int L) { return L == 0 ? o.cool : L == 1 ? o.heat : o.humid; }
const char* loadName(int L) { return L == 0 ? "compressor" : L == 1 ? "heater" : "humidifier"; }
const LoadDecision& decisionOf(const GuardDecisions& d, int L) { return L == 0 ? d.cool : L == 1 ? d.heat : d.humid; }
void setDemand(Demand& d, int L, bool on) { (L == 0 ? d.cool : L == 1 ? d.heat : d.humid) = on; }

// "12.3 s" or "2:05.0"
const char* secs(char* out, size_t n, uint32_t ms) {
  if (ms < 60000) snprintf(out, n, "%lu.%lu s", (unsigned long)(ms / 1000), (unsigned long)((ms % 1000) / 100));
  else snprintf(out, n, "%lu:%02lu.%lu", (unsigned long)(ms / 60000), (unsigned long)((ms / 1000) % 60),
                (unsigned long)((ms % 1000) / 100));
  return out;
}

}  // namespace

// ------------------------------------------------------------------ plumbing
bool ScenarioRunner::start(int n, uint32_t nowMs, CabinetModel& model) {
  if (!exists(n)) return false;
  n_ = n;
  result_ = Result::Running;
  demand_ = Demand{};
  startMs_ = nowMs;
  item_ = 0;
  stuckAtStart_ = 0;
  kicksSeen_ = 0;
  target_ = -1;
  expectMs_ = 0;
  humidMetMs_ = tempMetMs_ = 0;
  humidBack_ = heatBack_ = false;
  for (int i = 0; i < kNumSensors; i++) model.setOffset(i, 0, 0);
  savedRates_ = model.rates();
  if (n == 1) {
    // Test 1 is about timing only. With the real profile the heater's 60 min
    // max run would warm the model past the cabinet ceiling, so the loads'
    // effect on the model is switched off while it runs.
    model.rates().coolResponds = model.rates().heatResponds = model.rates().humidResponds = false;
  }
  // Stage 4: the scenario scripts the cabinet model; the real control decides.
  heatStarts_ = coolStarts_ = heatEnds_ = coolEnds_ = 0;
  kicks_ = 0;
  kicking_ = false;
  flag_ = false;
  dir_ = 1;
  lastTickMs_ = nowMs;
  if (usesControl(n)) {
    ModelRates& r = model.rates();
    r.driftCPerMin = 0.0f;              // nothing moves unless a load or the scenario moves it
    r.coolVpKpaPerMin = 0.0f;
    model.setCabinetRh(62.0f);
    switch (n) {
      case 7:  r.coolCPerMin = 0.0f; r.heatCPerMin = 0.0f; sweepC_ = 17.0f; model.setCabinetC(17.0f); break;
      case 8:  r.coolCPerMin = -1.0f; r.heatCPerMin = 1.0f; model.setCabinetC(21.0f); break;
      case 9:  r.coolCPerMin = -0.3f; r.coolVpKpaPerMin = 0.05f; model.setCabinetC(20.3f); model.setCabinetRh(62.0f); break;
      case 10: r.coolCPerMin = -0.5f; model.setCabinetC(20.0f); model.setCabinetRh(69.0f); model.setOffset(0, -1.0f, 0); break;
      case 17: r.heatResponds = false; r.heatCPerMin = 0.0f; r.coolCPerMin = 0.0f; model.setCabinetC(18.0f); break;
      case 23: r.coolCPerMin = -1.0f; r.heatCPerMin = 1.0f; model.setCabinetC(20.0f); break;
      // Stage 5: the cabinet temperature stays put unless the scenario moves it; the
      // humidifier adds water at 2 RH points a minute, so it passes its check (+0.5 points in
      // the 1 min bench window) with margin even though readings trail the model by up to 15 s.
      case 24: r.coolCPerMin = -1.0f; r.heatCPerMin = 1.0f; r.humidPctPerMin = 2.0f; r.coolVpKpaPerMin = 0.05f;
        model.setCabinetC(20.0f); break;
      case kSoakScenario:   // A2 cooling and heating rates (the defaults); room and drift set per phase
        r = ModelRates{};
        r.driftCPerMin = 0.006f;
        soakPhase_ = -1;
        soakCycle_ = 0;
        model.setCabinetC(20.0f);
        break;
      case 19: case 20: case 21: case 22:
        r.coolCPerMin = 0.0f; r.heatCPerMin = 0.0f; r.humidPctPerMin = 2.0f;
        model.setCabinetC(20.0f);
        if (n == 19 || n == 22) model.setCabinetRh(56.0f);
        if (n == 20) model.setCabinetRh(50.0f);
        if (n == 21) model.setCabinetRh(68.0f);
        if (n == 22) r.humidResponds = false;
        break;
      default: break;
    }
  }
  audit_.begin(s_, nowMs);
  lastReportMs_ = nowMs;
  for (int L = 0; L < 3; L++) { ranSinceStart_[L] = false; wentOn_[L] = wentOff_[L] = false; }
  relayRose_ = relayFell_ = false;
  phase(0, nowMs);
  passesInPhase_ = 0;
  return true;
}

void ScenarioRunner::stop(uint32_t nowMs, CabinetModel& model) {
  if (!running()) return;
  finish(nowMs, model, Result::Stopped, "stopped by 'stop'. Demand back to none");
}

void ScenarioRunner::finish(uint32_t nowMs, CabinetModel& model, Result r, const char* why) {
  result_ = r;
  demand_ = Demand{};
  for (int i = 0; i < kNumSensors; i++) model.setOffset(i, 0, 0);
  model.rates() = savedRates_;
  if (usesControl(n_)) {
    // Back to a settled cabinet, so the control goes idle afterwards.
    model.setCabinetC(20.0f);
    model.setCabinetRh(62.0f);
    if (tc_) tc_->setIdleFanOverride(-1);
    if (ctl_) ctl_->setHumidifyDuringCooling(s_.tuning.humidifyDuringCooling);
  }
  const char* word = r == Result::Pass ? "PASS" : r == Result::Fail ? "FAIL" : "STOPPED";
  say(nowMs, "======== run %d %s: %s", n_, word, why);
}

void ScenarioRunner::say(uint32_t nowMs, const char* fmt, ...) {
  char* line = lines_[(lineHead_ + lineCount_) % kLines];
  if (lineCount_ == kLines) { lineHead_ = (lineHead_ + 1) % kLines; lineCount_--; line = lines_[(lineHead_ + lineCount_) % kLines]; }
  char t[16];
  int k = snprintf(line, kLineLen, "  [run %d  t+%s] ", n_, secs(t, sizeof(t), elapsedMs(nowMs, startMs_)));
  if (k < 0) k = 0;
  if (k < kLineLen) {
    va_list a;
    va_start(a, fmt);
    vsnprintf(line + k, kLineLen - k, fmt, a);
    va_end(a);
  }
  lineCount_++;
}

const char* ScenarioRunner::takeLine() {
  if (lineCount_ == 0) return nullptr;
  memcpy(taken_, lines_[lineHead_], kLineLen);
  lineHead_ = (lineHead_ + 1) % kLines;
  lineCount_--;
  return taken_;
}

void ScenarioRunner::edges(const ScenarioView& v) {
  for (int L = 0; L < 3; L++) {
    const bool was = liveOf(prev_, L), now = liveOf(v.live, L);
    wentOn_[L] = !was && now;
    wentOff_[L] = was && !now;
    if (wentOn_[L]) { onAtMs_[L] = v.nowMs; ranSinceStart_[L] = true; }
    if (wentOff_[L]) offAtMs_[L] = v.nowMs;
  }
  relayRose_ = !prev_.relayHeating && v.live.relayHeating;
  relayFell_ = prev_.relayHeating && !v.live.relayHeating;
  if (relayRose_) relayRoseAtMs_ = v.nowMs;
  if (relayFell_) relayFellAtMs_ = v.nowMs;
  prev_ = v.live;
}

void ScenarioRunner::update(const ScenarioView& v, Controller& ctl, CabinetModel& model, TemperatureControl& tc) {
  tc_ = &tc;
  ctl_ = &ctl;
  if (!running()) { prev_ = v.live; return; }
  edges(v);
  passesInPhase_++;

  // Checked on every pass of every scenario: the interlock must never be broken.
  const LoadOutputs& o = v.live;
  if ((o.cool && o.heat) || (o.cool && o.relayHeating) || (o.heat && !o.relayHeating)) {
    finish(v.nowMs, model, Result::Fail, "INTERLOCK BROKEN - a load was live on the wrong relay side");
    return;
  }

  switch (n_) {
    case 1:  run1(v, ctl, model); break;
    case 2:  run2(v, ctl, model); break;
    case 3:  run3(v, ctl, model); break;
    case 4:  run4(v, ctl, model); break;
    case 41: run41(v, ctl, model); break;
    case 42: run42(v, ctl, model); break;
    case 5:  run5(v, ctl, model); break;
    case 6:  run6(v, ctl, model); break;
    case 13: run13(v, ctl, model); break;
    case 15: run15(v, ctl, model); break;
    case 7:  run7(v, ctl, model, tc); break;
    case 8:  run8(v, ctl, model, tc); break;
    case 9:  run9(v, ctl, model, tc); break;
    case 10: run10(v, ctl, model, tc); break;
    case 17: run17(v, ctl, model, tc); break;
    case 23: run23(v, ctl, model, tc); break;
    case 19: run19(v, ctl, model, tc); break;
    case 20: run20(v, ctl, model, tc); break;
    case 21: run21(v, ctl, model, tc); break;
    case 22: run22(v, ctl, model, tc); break;
    case 24: run24(v, ctl, model, tc); break;
    case kSoakScenario: runSoak(v, ctl, model, tc); break;
    default: break;
  }
}

// ------------------------------------------------------------------ run 1
// B3 test 1, guard timing. For each load in turn:
//   start it; drop the demand at once      -> stays on for exactly its min on
//   ask again at once                       -> stays off for exactly its min off
//   keep asking                             -> stops at exactly its max run
void ScenarioRunner::run1(const ScenarioView& v, Controller& ctl, CabinetModel& model) {
  const uint32_t now = v.nowMs;
  const int L = item_;
  const SafetySettings& f = s_.safety;
  const uint32_t minOn  = L == 0 ? f.compressorMinOnMs  : L == 1 ? f.heaterMinOnMs  : 0;
  const uint32_t minOff = L == 0 ? f.compressorMinOffMs : L == 1 ? f.heaterMinOffMs : f.humidMinOffMs;
  const uint32_t maxRun = L == 0 ? f.compressorMaxRunMs : L == 1 ? f.heaterMaxRunMs : f.humidMaxRunMs;
  const char* name = loadName(L);
  const bool live = liveOf(v.live, L);
  char a[16], b[16], c[64];

  switch (phase_) {
    case 0:   // ask for this load only
      if (passesInPhase_ == 1 && L == 0)
        say(now, "TEST 1 - guard timing. Compressor, heater, humidifier in turn: min on, min off, max run. Watch the bulb (heater) and the humidifier fan.");
      demand_ = Demand{};
      setDemand(demand_, L, true);
      say(now, "1.%d %s: asking it to start", L + 1, name);
      deadlineMs_ = ctl.hold().remainingMs() + minOff + s_.tuning.crossoverDelayMs + 10000;
      phase(1, now);
      break;

    case 1:   // waiting for it to start
      if (wentOn_[L]) {
        setDemand(demand_, L, false);
        if (minOn) say(now, "%s ON. Demand OFF at once: it must stay on for its min on (%s)", name, secs(a, sizeof(a), minOn));
        else       say(now, "%s ON. Demand OFF at once: no min on, so it must stop straight away", name);
        phase(2, now);
      } else if (elapsedMs(now, phaseAtMs_) > deadlineMs_) {
        snprintf(c, sizeof(c), "%s did not start - held by %s", name, guardBlockName(decisionOf(ctl.decisions(), L).block));
        finish(now, model, Result::Fail, c);
      }
      break;

    case 2: {   // min on
      if (wentOff_[L]) {
        const uint32_t ran = offAtMs_[L] - onAtMs_[L];
        if (ran + kEarlyMs < minOn) {
          snprintf(c, sizeof(c), "%s stopped after %s - before its min on of %s", name, secs(a, sizeof(a), ran), secs(b, sizeof(b), minOn));
          finish(now, model, Result::Fail, c);
          break;
        }
        say(now, "%s OFF after %s (min on %s) - correct. Demand ON at once: it must stay off for its min off (%s)",
            name, secs(a, sizeof(a), ran), secs(b, sizeof(b), minOn), secs(c, sizeof(c), minOff));
        setDemand(demand_, L, true);
        phase(3, now);
      } else if (live && elapsedMs(now, onAtMs_[L]) > minOn + kLateMs) {
        snprintf(c, sizeof(c), "%s still on %s after its min on of %s", name,
                 secs(a, sizeof(a), elapsedMs(now, onAtMs_[L])), secs(b, sizeof(b), minOn));
        finish(now, model, Result::Fail, c);
      }
      break;
    }

    case 3: {   // min off
      if (wentOn_[L]) {
        const uint32_t gap = onAtMs_[L] - offAtMs_[L];
        if (gap + kEarlyMs < minOff) {
          snprintf(c, sizeof(c), "%s restarted after %s - before its min off of %s", name, secs(a, sizeof(a), gap), secs(b, sizeof(b), minOff));
          finish(now, model, Result::Fail, c);
          break;
        }
        say(now, "%s ON again %s after stopping (min off %s) - correct. Demand stays ON: it must stop at its max run (%s)",
            name, secs(a, sizeof(a), gap), secs(b, sizeof(b), minOff), secs(c, sizeof(c), maxRun));
        phase(4, now);
      } else if (elapsedMs(now, offAtMs_[L]) > minOff + kLateMs) {
        snprintf(c, sizeof(c), "%s did not restart after its min off - held by %s", name,
                 guardBlockName(decisionOf(ctl.decisions(), L).block));
        finish(now, model, Result::Fail, c);
      }
      break;
    }

    case 4: {   // max run
      if (wentOff_[L]) {
        const uint32_t ran = offAtMs_[L] - onAtMs_[L];
        if (ran + kEarlyMs < maxRun) {
          snprintf(c, sizeof(c), "%s stopped after %s - before its max run (held by %s)", name, secs(a, sizeof(a), ran),
                   guardBlockName(decisionOf(ctl.decisions(), L).block));
          finish(now, model, Result::Fail, c);
          break;
        }
        say(now, "%s OFF after %s (max run %s) - correct", name, secs(a, sizeof(a), ran), secs(b, sizeof(b), maxRun));
        demand_ = Demand{};
        if (++item_ == 3) {
          finish(now, model, Result::Pass, "min on, min off and max run enforced for the compressor, heater and humidifier");
        } else {
          phase(0, now);
        }
      } else if (live && elapsedMs(now, onAtMs_[L]) > maxRun + kLateMs) {
        snprintf(c, sizeof(c), "%s still on after its max run of %s", name, secs(a, sizeof(a), maxRun));
        finish(now, model, Result::Fail, c);
      }
      break;
    }
  }
}

// ------------------------------------------------------------------ run 2
// B3 test 2, heater guards. The heater runs (lit for 5 s so it is seen), then one
// guard is tripped; the heater must be off on the pass after the guards first see it.
//   item 0: cabinet ceiling (cabinet sensor ch2 scripted above setpoint + 3 C)
//   item 1: chamber ceiling (chamber scripted above 30 C)
//   item 2: sensors (chamber scripted to -20 C: implausible, excluded; then it must
//           recover — 10 reads + serial — and the 4.3 recovery hold must pass)
//   item 3: fan (Josh unplugs J2; the tach reads zero)
// Scripted values reach the guards with the next sensor sweep (every 15 s).
void ScenarioRunner::run2(const ScenarioView& v, Controller& ctl, CabinetModel& model) {
  const uint32_t now = v.nowMs;
  const SafetySettings& f = s_.safety;
  const float target = s_.user.targetTempC;
  constexpr uint32_t kShowMs = 5000;
  char a[16], b[16], c[120];

  switch (phase_) {
    case 0:
      say(now, "TEST 2 - heater guards. The heater runs; each guard is tripped in turn; it must go off on the next pass. Watch the bulb.");
      demand_ = Demand{};
      demand_.heat = true;
      say(now, "asking the heater to start");
      deadlineMs_ = ctl.hold().remainingMs() + f.heaterMinOffMs + s_.tuning.crossoverDelayMs + 10000;
      phase(1, now);
      break;

    case 1:   // waiting for the heater
      if (v.live.heat) {
        say(now, "heater ON - lit for 5 s before the first trip");
        phase(2, now);
      } else if (elapsedMs(now, phaseAtMs_) > deadlineMs_) {
        snprintf(c, sizeof(c), "heater did not start - held by %s", guardBlockName(ctl.decisions().heat.block));
        finish(now, model, Result::Fail, c);
      }
      break;

    case 2:   // heater lit for 5 s, then the next trip
      if (!v.live.heat) { finish(now, model, Result::Fail, "the heater stopped before the trip"); break; }
      if (elapsedMs(now, phaseAtMs_) < kShowMs) break;
      if (item_ == 0) {
        const float want = target + f.cabinetCeilingAboveSetpointC + 0.5f;
        model.setOffset(0, want - model.tempC(0), 0);
        say(now, "2.1 TRIP cabinet ceiling: cabinet sensor ch2 scripted to %.1f C (ceiling %.1f C); seen at the next sweep",
            (double)want, (double)(target + f.cabinetCeilingAboveSetpointC));
        phase(3, now);
      } else if (item_ == 1) {
        const float want = f.chamberCeilingC + 0.5f;
        model.setOffset(kChamberIndex, want - model.tempC(kChamberIndex), 0);
        say(now, "2.2 TRIP chamber ceiling: chamber sensor ch6 scripted to %.1f C (ceiling %.1f C)", (double)want, (double)f.chamberCeilingC);
        phase(3, now);
      } else if (item_ == 2) {
        model.setOffset(kChamberIndex, -20.0f - model.tempC(kChamberIndex), 0);
        say(now, "2.3 TRIP sensors: chamber sensor ch6 scripted to -20.0 C - implausible (below -10 C), so it is excluded");
        phase(3, now);
      } else {
        const uint32_t ranFor = elapsedMs(now, onAtMs_[1]);
        const uint32_t leftMs = f.heaterMaxRunMs > ranFor ? f.heaterMaxRunMs - ranFor : 0;
        say(now, "2.4 FAN: >>> UNPLUG THE PLENUM FAN PLUG (J2) NOW <<< (within %s, before the heater reaches its max run). Waiting for the tach to read zero",
            secs(a, sizeof(a), leftMs > 5000 ? leftMs - 5000 : 0));
        phase(7, now);
      }
      break;

    case 3:   // wait for the sweep that brings the scripted value to the guards
      if (v.sweepDone) { phase(4, now); break; }
      if (!v.live.heat) { finish(now, model, Result::Fail, "the heater stopped before the trip reached it"); break; }
      if (elapsedMs(now, phaseAtMs_) > 30000) finish(now, model, Result::Fail, "no sensor sweep in 30 s");
      break;

    case 4: {   // the very next pass: the heater must be off, for the right reason
      const GuardBlock want = item_ == 0 ? GuardBlock::CabinetCeiling : item_ == 1 ? GuardBlock::ChamberCeiling : GuardBlock::Sensors;
      const GuardBlock got = ctl.decisions().heat.block;
      if (v.live.heat) { finish(now, model, Result::Fail, "heater still on one pass after the guards saw the trip"); break; }
      if (got != want) {
        snprintf(c, sizeof(c), "heater off, but for the wrong reason: %s (expected %s)", guardBlockName(got), guardBlockName(want));
        finish(now, model, Result::Fail, c);
        break;
      }
      if (item_ == 2 && v.health && v.health->reason(kChamberIndex) != Exclusion::Implausible) {
        snprintf(c, sizeof(c), "chamber excluded for %s, expected implausible", exclusionName(v.health->reason(kChamberIndex)));
        finish(now, model, Result::Fail, c);
        break;
      }
      model.setOffset(item_ == 0 ? 0 : kChamberIndex, 0, 0);
      if (item_ < 2) {
        say(now, "heater OFF on the next pass (%s) - correct. Trip cleared; the heater restarts after its min off (%s) once the next sweep shows it",
            guardBlockName(got), secs(a, sizeof(a), f.heaterMinOffMs));
      } else {
        say(now, "heater OFF on the next pass (sensors: chamber implausible) - correct. Chamber back to normal: it needs 10 good reads "
                 "and its serial re-checked (~2.5 min), then the recovery hold (%s), before the heater may restart",
            secs(a, sizeof(a), f.sensorRecoveryHoldMs));
      }
      expectMs_ = 0;   // when the chamber recovered (item 2)
      phase(5, now);
      break;
    }

    case 5:   // waiting for the restart
      if (item_ == 2 && expectMs_ == 0 && v.health && v.health->healthy(kChamberIndex)) {
        expectMs_ = now ? now : 1;
        say(now, "chamber sensor RECOVERED (10 good reads, serial re-checked); recovery hold %s starts", secs(a, sizeof(a), f.sensorRecoveryHoldMs));
      }
      if (v.live.heat) {
        if (item_ == 2) {
          if (expectMs_ == 0) { finish(now, model, Result::Fail, "the heater restarted before the chamber sensor recovered"); break; }
          const uint32_t held = elapsedMs(now, expectMs_);
          if (held + 50 < f.sensorRecoveryHoldMs) {
            snprintf(c, sizeof(c), "the heater restarted %s after the recovery - before the %s hold", secs(a, sizeof(a), held),
                     secs(b, sizeof(b), f.sensorRecoveryHoldMs));
            finish(now, model, Result::Fail, c);
            break;
          }
          say(now, "heater ON again %s after the chamber recovered (hold %s) - correct; lit for 5 s", secs(a, sizeof(a), held),
              secs(b, sizeof(b), f.sensorRecoveryHoldMs));
        } else {
          say(now, "heater ON again - lit for 5 s");
        }
        item_++;
        phase(2, now);
      } else if (elapsedMs(now, phaseAtMs_) > (item_ == 2 ? 300000u : f.heaterMinOffMs + 40000)) {
        snprintf(c, sizeof(c), "heater did not restart after the trip was cleared - held by %s", guardBlockName(ctl.decisions().heat.block));
        finish(now, model, Result::Fail, c);
      }
      break;

    case 7:   // waiting for the tach to read zero
      if (!v.fanOk) {
        if (v.live.heat) {               // the guards see it on the next pass
          say(now, "tach reads zero");
          phase(8, now);
        } else if (wentOff_[1] && ctl.decisions().heat.block == GuardBlock::Fan) {
          say(now, "tach reads zero; heater OFF on that same pass (fan) - correct");
          say(now, ">>> PLUG J2 BACK IN NOW <<< The board kickstarts the fan every %s until the tach reads",
              secs(a, sizeof(a), f.fanStallRetryMs));
          demand_ = Demand{};
          phase(9, now);
        } else {
          finish(now, model, Result::Fail,
                 "the heater was not running when the tach read zero. Plug J2 back in, wait for the fan, then type run 2 again");
        }
      } else if (wentOff_[1]) {
        finish(now, model, Result::Fail, "the heater reached its max run before J2 was unplugged. Type run 2 again and unplug J2 sooner");
      } else if (elapsedMs(now, phaseAtMs_) > 120000) {
        finish(now, model, Result::Fail, "the tach never read zero in 2 min. Is J2 unplugged?");
      }
      break;

    case 8: {   // the very next pass
      const GuardBlock got = ctl.decisions().heat.block;
      if (v.live.heat) { finish(now, model, Result::Fail, "heater still on one pass after the tach read zero"); break; }
      if (got != GuardBlock::Fan) {
        snprintf(c, sizeof(c), "heater off, but for the wrong reason: %s (expected fan)", guardBlockName(got));
        finish(now, model, Result::Fail, c);
        break;
      }
      say(now, "heater OFF on the next pass (fan) - correct");
      say(now, ">>> PLUG J2 BACK IN NOW <<< The board kickstarts the fan every %s until the tach reads",
          secs(a, sizeof(a), f.fanStallRetryMs));
      demand_ = Demand{};
      phase(9, now);
      break;
    }

    case 9:   // waiting for the fan
      if (v.fanOk) {
        say(now, "fan running again");
        finish(now, model, Result::Pass, "heater off on the next pass for the cabinet ceiling, the chamber ceiling, the sensors and the fan; recovery hold kept");
      } else if (elapsedMs(now, phaseAtMs_) > 180000) {
        finish(now, model, Result::Fail, "the fan did not come back in 3 min. Check J2 is plugged in");
      }
      break;
  }
}

// ------------------------------------------------------------------ run 3
// B3 test 3, interlock impossibility. Both loads asked for at once, through the
// guards. Cooling goes first; Josh meters J10. Then the cooling demand drops and
// the heater takes over after the crossover delay; Josh meters again. The
// interlock check in update() runs on every pass throughout.
void ScenarioRunner::run3(const ScenarioView& v, Controller& ctl, CabinetModel& model) {
  const uint32_t now = v.nowMs;
  const SafetySettings& f = s_.safety;
  const uint32_t holdMs = 60000;
  char a[16], c[96];

  switch (phase_) {
    case 0:
      say(now, "TEST 3 - interlock. Compressor AND heater asked for at once. Only one may ever have a 12 V feed.");
      demand_ = Demand{};
      demand_.cool = demand_.heat = true;
      deadlineMs_ = ctl.hold().remainingMs() + f.compressorMinOffMs + s_.tuning.crossoverDelayMs + 10000;
      phase(1, now);
      break;

    case 1:
      if (v.live.heat) { finish(now, model, Result::Fail, "the heater started first - cooling should go first"); break; }
      if (v.live.cool) {
        say(now, "COOLING ON, heater held (%s), K1 on the cooling side.", guardBlockName(ctl.decisions().heat.block));
        say(now, ">>> METER NOW, black on GND: J10 pin 1 = about 12 V. J10 pin 3 = about 0 V (up to ~1 V stray is normal). Bulb OFF. Holding %s",
            secs(a, sizeof(a), holdMs));
        phase(2, now);
      } else if (elapsedMs(now, phaseAtMs_) > deadlineMs_) {
        snprintf(c, sizeof(c), "cooling did not start - held by %s", guardBlockName(ctl.decisions().cool.block));
        finish(now, model, Result::Fail, c);
      }
      break;

    case 2:
      if (!v.live.cool) { finish(now, model, Result::Fail, "cooling stopped during the hold"); break; }
      if (elapsedMs(now, phaseAtMs_) >= holdMs) {
        demand_.cool = false;
        say(now, "Cooling demand dropped; heat still asked for. Expect cooling OFF, then the heater after the crossover delay (%s)",
            secs(a, sizeof(a), s_.tuning.crossoverDelayMs));
        deadlineMs_ = f.compressorMinOnMs + s_.tuning.crossoverDelayMs + f.relaySettleMs + 10000;
        phase(3, now);
      }
      break;

    case 3:
      if (v.live.heat) {
        say(now, "HEATER ON, cooling off, K1 on the heating side.");
        say(now, ">>> METER NOW, black on GND: J10 pin 3 = about 12 V (bulb ON). J10 pin 1 = about 0 V (up to ~1 V stray is normal). Holding %s",
            secs(a, sizeof(a), holdMs));
        phase(4, now);
      } else if (elapsedMs(now, phaseAtMs_) > deadlineMs_) {
        snprintf(c, sizeof(c), "the heater did not take over - held by %s", guardBlockName(ctl.decisions().heat.block));
        finish(now, model, Result::Fail, c);
      }
      break;

    case 4:
      if (!v.live.heat) { finish(now, model, Result::Fail, "the heater stopped during the hold"); break; }
      if (elapsedMs(now, phaseAtMs_) >= holdMs) {
        demand_ = Demand{};
        say(now, "Heat demand dropped; waiting for the heater to stop");
        phase(5, now);
      }
      break;

    case 5:
      if (!v.live.heat && !v.live.cool) {
        finish(now, model, Result::Pass, "only one load ever live, and K1 always on that load's side (checked every pass)");
      } else if (elapsedMs(now, phaseAtMs_) > f.heaterMinOnMs + 10000) {
        finish(now, model, Result::Fail, "the heater did not stop");
      }
      break;
  }
}

// ------------------------------------------------------------------ run 13
// B3 test 13, relay exercise. Three cycles, each 1 s energised and 1 s released,
// compressor and heater off throughout, ending on the cooling side.
void ScenarioRunner::run13(const ScenarioView& v, Controller& ctl, CabinetModel& model) {
  const uint32_t now = v.nowMs;
  const SafetySettings& f = s_.safety;
  constexpr uint32_t kTolMs = 100;
  char a[16], b[16], c[96];

  if (phase_ == 2 && (v.live.cool || v.live.heat)) {
    finish(now, model, Result::Fail, "a temperature load was live during the relay exercise");
    return;
  }

  switch (phase_) {
    case 0:
      say(now, "TEST 13 - relay exercise. %u cycles, %s energised / %s released. Listen for K1 clicking; both loads must stay off.",
          (unsigned)f.relayExerciseCycles, secs(a, sizeof(a), f.relayExerciseOnMs), secs(b, sizeof(b), f.relayExerciseOffMs));
      demand_ = Demand{};
      if (!ctl.requestExercise(now)) { finish(now, model, Result::Fail, "a relay exercise was already under way"); break; }
      item_ = 0;
      deadlineMs_ = f.compressorMaxRunMs + f.relayExerciseCycles * (f.relayExerciseOnMs + f.relayExerciseOffMs) + 10000;
      phase(1, now);
      break;

    case 1:   // waiting for compressor and heater to be off (one may be finishing its run)
      if (ctl.exercise().cycling()) {
        if (v.live.relayHeating && !relayRose_) { relayRose_ = true; relayRoseAtMs_ = now; }   // K1 was already there
        phase(2, now);
        // fall through into the cycling checks for this pass
      } else {
        if (passesInPhase_ == 2 && (v.live.cool || v.live.heat))
          say(now, "waiting for the %s to finish before cycling", v.live.cool ? "compressor" : "heater");
        if (elapsedMs(now, phaseAtMs_) > deadlineMs_) finish(now, model, Result::Fail, "the exercise never started");
        break;
      }
      [[fallthrough]];

    case 2:
      if (relayRose_) {
        item_++;
        if (item_ > 1) {
          const uint32_t off = relayRoseAtMs_ - relayFellAtMs_;
          if (off + kTolMs < f.relayExerciseOffMs || off > f.relayExerciseOffMs + kTolMs) {
            snprintf(c, sizeof(c), "released for %s, expected %s", secs(a, sizeof(a), off), secs(b, sizeof(b), f.relayExerciseOffMs));
            finish(now, model, Result::Fail, c);
            break;
          }
        }
      }
      if (relayFell_) {
        const uint32_t on = relayFellAtMs_ - relayRoseAtMs_;
        say(now, "cycle %d: K1 energised for %s", item_, secs(a, sizeof(a), on));
        if (on + kTolMs < f.relayExerciseOnMs || on > f.relayExerciseOnMs + kTolMs) {
          snprintf(c, sizeof(c), "energised for %s, expected %s", secs(a, sizeof(a), on), secs(b, sizeof(b), f.relayExerciseOnMs));
          finish(now, model, Result::Fail, c);
          break;
        }
      }
      if (!ctl.exercise().active()) {
        if (item_ != f.relayExerciseCycles) {
          snprintf(c, sizeof(c), "%d cycles seen, expected %u", item_, (unsigned)f.relayExerciseCycles);
          finish(now, model, Result::Fail, c);
        } else if (v.live.relayHeating) {
          finish(now, model, Result::Fail, "the exercise ended with K1 on the heating side");
        } else {
          finish(now, model, Result::Pass, "three cycles, both loads off throughout, ended on the cooling side");
        }
      } else if (elapsedMs(now, phaseAtMs_) > f.relayExerciseCycles * (f.relayExerciseOnMs + f.relayExerciseOffMs) + 5000) {
        finish(now, model, Result::Fail, "the exercise did not finish");
      }
      break;
  }
}

// ------------------------------------------------------------------ run 4
// B3 test 4, sensor failure (real bus faults: Josh unplugs probes when told).
// Heat and humidify asked for. Chamber out: heater off, humidifier keeps running.
// Then cabinet sensors out one at a time: humidifier still on at 3 of 4, off at 2.
// All back in: each recovers (10 reads + serial), then the recovery hold, then the
// loads resume. (The compressor's thresholds are the heater's, in the native tests.)
void ScenarioRunner::run4(const ScenarioView& v, Controller& ctl, CabinetModel& model) {
  const uint32_t now = v.nowMs;
  const SafetySettings& f = s_.safety;
  const SensorHealth* h = v.health;
  char a[16], c[120];
  if (!h) { finish(now, model, Result::Fail, "no health information"); return; }

  // Throughout: the humidifier must never be held for "sensors" while 3 of 4 cabinet are healthy.
  if (phase_ >= 1 && expectMs_ == 0 && h->healthyCabinet() >= f.minHealthyCabinet &&
      ctl.decisions().humid.block == GuardBlock::Sensors) {
    finish(now, model, Result::Fail, "the humidifier was held for sensors with 3 of 4 cabinet sensors healthy");
    return;
  }

  switch (phase_) {
    case 0:
      say(now, "TEST 4 - sensor failure. Heat and humidify asked for. You will unplug sensors one at a time when told.");
      demand_ = Demand{};
      demand_.heat = demand_.humid = true;
      deadlineMs_ = ctl.hold().remainingMs() + f.heaterMinOffMs + s_.tuning.crossoverDelayMs + 20000;
      phase(1, now);
      break;

    case 1:
      if (v.live.heat && v.live.humid) {
        say(now, "heater and humidifier ON");
        say(now, "4.1 >>> UNPLUG THE CHAMBER SENSOR (ch6) NOW <<<");
        target_ = kChamberIndex;
        phase(2, now);
      } else if (elapsedMs(now, phaseAtMs_) > deadlineMs_) {
        finish(now, model, Result::Fail, "heater and humidifier did not both start");
      }
      break;

    case 2: case 4: case 6:   // waiting for the unplugged sensor to be excluded
      if (!h->healthy(target_)) {
        say(now, "ch%d excluded: %s", channelOf(target_), exclusionName(h->reason(target_)));
        phase(phase_ + 1, now);
      } else if (elapsedMs(now, phaseAtMs_) > 90000) {
        snprintf(c, sizeof(c), "ch%d was never excluded - is it unplugged?", channelOf(target_));
        finish(now, model, Result::Fail, c);
      }
      break;

    case 3:   // chamber out: heater off now; humidifier keeps running
      if (v.live.heat || ctl.decisions().heat.block != GuardBlock::Sensors) {
        finish(now, model, Result::Fail, "heater not off for sensors with the chamber sensor out");
        break;
      }
      say(now, "heater OFF (sensors: chamber) - correct. Humidifier %s (needs only 3 of 4 cabinet) - correct",
          v.live.humid ? "still ON" : "not blocked for sensors");
      say(now, "4.2 >>> UNPLUG ch2 (top front) NOW <<<");
      target_ = 0;
      phase(4, now);
      break;

    case 5:   // 3 of 4 cabinet: humidifier still allowed
      if (ctl.decisions().humid.block == GuardBlock::Sensors) {
        finish(now, model, Result::Fail, "humidifier held for sensors at 3 of 4 cabinet");
        break;
      }
      say(now, "3 of 4 cabinet healthy: humidifier still allowed - correct; heater still off (%s)",
          guardBlockName(ctl.decisions().heat.block));
      say(now, "4.3 >>> UNPLUG ch3 (bottom front) NOW <<<");
      target_ = 1;
      phase(6, now);
      break;

    case 7:   // 2 of 4 cabinet: humidifier off now
      if (v.live.humid || ctl.decisions().humid.block != GuardBlock::Sensors) {
        finish(now, model, Result::Fail, "humidifier not off for sensors at 2 of 4 cabinet");
        break;
      }
      say(now, "2 of 4 cabinet healthy: humidifier OFF (sensors) - correct");
      say(now, "4.4 >>> PLUG ch6, ch2 AND ch3 BACK IN NOW <<< Each needs 10 good reads and its serial re-checked (~2.5 min), "
               "then the recovery hold (%s)", secs(a, sizeof(a), f.sensorRecoveryHoldMs));
      expectMs_ = 1;            // recovery under way: the "never held at 3 of 4" check pauses
      item_ = 0;                // 0 = waiting for both requirements to be met again
      humidMetMs_ = tempMetMs_ = 0;
      humidBack_ = heatBack_ = false;
      phase(8, now);
      break;

    case 8: {   // recovery: when each requirement is met again, its loads must wait the hold
      const bool humidMet = h->healthyCabinet() >= f.minHealthyCabinet;
      const bool tempMet = humidMet && h->chamberHealthy();
      if (humidMet && humidMetMs_ == 0) { humidMetMs_ = now ? now : 1; say(now, "3 of 4 cabinet healthy again: humidifier recovery hold starts"); }
      if (tempMet && tempMetMs_ == 0) { tempMetMs_ = now ? now : 1; say(now, "chamber healthy again: heater recovery hold starts"); }
      if (wentOn_[2] && !humidBack_) {
        if (humidMetMs_ == 0 || elapsedMs(now, humidMetMs_) + 50 < f.sensorRecoveryHoldMs) {
          finish(now, model, Result::Fail, "humidifier resumed before the recovery hold");
          break;
        }
        humidBack_ = true;
        say(now, "humidifier ON %s after its requirement was met again - correct", secs(a, sizeof(a), elapsedMs(now, humidMetMs_)));
      }
      if (wentOn_[1] && !heatBack_) {
        if (tempMetMs_ == 0 || elapsedMs(now, tempMetMs_) + 50 < f.sensorRecoveryHoldMs) {
          finish(now, model, Result::Fail, "heater resumed before the recovery hold");
          break;
        }
        heatBack_ = true;
        say(now, "heater ON %s after its requirement was met again - correct", secs(a, sizeof(a), elapsedMs(now, tempMetMs_)));
      }
      if (humidBack_ && heatBack_) {
        finish(now, model, Result::Pass, "chamber out: heater off, humidifier kept running; 3 of 4: humidifier allowed; "
                                         "2 of 4: humidifier off; all back: each recovered, loads resumed after the hold");
      } else if (elapsedMs(now, phaseAtMs_) > 420000) {
        finish(now, model, Result::Fail, "the sensors did not recover and the loads did not resume within 7 min");
      }
      break;
    }
  }
}

// ------------------------------------------------------------------ run 41 (test 4c)
// Heat asked for, chamber plugged in. Two cabinet sensors unplugged in turn: at 3 of 4
// the heater is still allowed; at 2 of 4 it goes off (sensors). Both back: recovery,
// the hold, and the heater resumes. Judged on the guard's reason, so a normal
// max-run stop part-way through doesn't confuse it.
void ScenarioRunner::run41(const ScenarioView& v, Controller& ctl, CabinetModel& model) {
  const uint32_t now = v.nowMs;
  const SafetySettings& f = s_.safety;
  const SensorHealth* h = v.health;
  char a[16];
  if (!h) { finish(now, model, Result::Fail, "no health information"); return; }
  const GuardBlock hb = ctl.decisions().heat.block;

  switch (phase_) {
    case 0:
      say(now, "TEST 4c - heater and the cabinet sensors. Heat asked for, chamber plugged in. You will unplug two cabinet sensors.");
      demand_ = Demand{};
      demand_.heat = true;
      deadlineMs_ = ctl.hold().remainingMs() + f.heaterMinOffMs + s_.tuning.crossoverDelayMs + 20000;
      phase(1, now);
      break;
    case 1:
      if (v.live.heat) {
        say(now, "heater ON (bulb lit)");
        say(now, "4c.1 >>> UNPLUG ch2 (top front) NOW <<<");
        target_ = 0;
        phase(2, now);
      } else if (elapsedMs(now, phaseAtMs_) > deadlineMs_) {
        finish(now, model, Result::Fail, "the heater did not start");
      }
      break;
    case 2: case 4:
      if (!h->healthy(target_)) {
        say(now, "ch%d excluded: %s", channelOf(target_), exclusionName(h->reason(target_)));
        phase(phase_ + 1, now);
      } else if (elapsedMs(now, phaseAtMs_) > 90000) {
        finish(now, model, Result::Fail, "the sensor was never excluded - is it unplugged?");
      }
      break;
    case 3:   // 3 of 4 cabinet, chamber in: the heater is still allowed
      if (hb == GuardBlock::Sensors) { finish(now, model, Result::Fail, "heater held for sensors at 3 of 4 cabinet with the chamber in"); break; }
      say(now, "3 of 4 cabinet healthy, chamber in: heater still allowed - correct (%s)", v.live.heat ? "bulb lit" : guardBlockName(hb));
      say(now, "4c.2 >>> UNPLUG ch3 (bottom front) NOW <<<");
      target_ = 1;
      phase(4, now);
      break;
    case 5:   // 2 of 4: off now
      if (v.live.heat || hb != GuardBlock::Sensors) { finish(now, model, Result::Fail, "heater not off for sensors at 2 of 4 cabinet"); break; }
      say(now, "2 of 4 cabinet healthy: heater OFF (sensors) - correct (bulb dark)");
      say(now, "4c.3 >>> PLUG ch2 AND ch3 BACK IN NOW <<< ~3 min to recover, then the %s hold", secs(a, sizeof(a), f.sensorRecoveryHoldMs));
      tempMetMs_ = 0;
      phase(6, now);
      break;
    case 6:
      if (h->healthyCabinet() >= f.minHealthyCabinet && h->chamberHealthy() && tempMetMs_ == 0) {
        tempMetMs_ = now ? now : 1;
        say(now, "3 of 4 cabinet healthy again: heater recovery hold starts");
      }
      if (wentOn_[1]) {
        if (tempMetMs_ == 0 || elapsedMs(now, tempMetMs_) + 50 < f.sensorRecoveryHoldMs) {
          finish(now, model, Result::Fail, "heater resumed before the recovery hold");
          break;
        }
        say(now, "heater ON %s after the requirement was met again - correct", secs(a, sizeof(a), elapsedMs(now, tempMetMs_)));
        finish(now, model, Result::Pass, "heater allowed at 3 of 4 cabinet, off at 2 of 4; resumed after recovery and the hold");
      } else if (elapsedMs(now, phaseAtMs_) > 420000) {
        finish(now, model, Result::Fail, "the heater did not resume within 7 min");
      }
      break;
  }
}

// ------------------------------------------------------------------ run 42 (test 4d)
// Cooling asked for. Josh meters J10 pin 1 (red) to pin 2 (black): ~12 V while the
// SSR control is driven, ~0 V off. Chamber unplugged: cooling off (sensors).
// Chamber back: recovery, the hold, cooling resumes.
void ScenarioRunner::run42(const ScenarioView& v, Controller& ctl, CabinetModel& model) {
  const uint32_t now = v.nowMs;
  const SafetySettings& f = s_.safety;
  const SensorHealth* h = v.health;
  char a[16];
  if (!h) { finish(now, model, Result::Fail, "no health information"); return; }
  const GuardBlock cb = ctl.decisions().cool.block;

  switch (phase_) {
    case 0:
      say(now, "TEST 4d - cooling and the chamber sensor. Cooling asked for. Meter J10 pin 1 (red) to pin 2 (black).");
      demand_ = Demand{};
      demand_.cool = true;
      deadlineMs_ = ctl.hold().remainingMs() + f.compressorMinOffMs + s_.tuning.crossoverDelayMs + 20000;
      phase(1, now);
      break;
    case 1:
      if (v.live.cool) {
        say(now, "COOLING ON. >>> METER NOW: J10 pin 1 to pin 2 = about 12 V <<<");
        phase(2, now);
      } else if (elapsedMs(now, phaseAtMs_) > deadlineMs_) {
        finish(now, model, Result::Fail, "cooling did not start");
      }
      break;
    case 2:   // 15 s to meter, then the unplug
      if (elapsedMs(now, phaseAtMs_) < 15000) break;
      say(now, "4d.1 >>> UNPLUG THE CHAMBER SENSOR (ch6) NOW <<<");
      phase(3, now);
      break;
    case 3:
      if (!h->healthy(kChamberIndex)) {
        say(now, "ch6 excluded: %s", exclusionName(h->reason(kChamberIndex)));
        phase(4, now);
      } else if (elapsedMs(now, phaseAtMs_) > 90000) {
        finish(now, model, Result::Fail, "ch6 was never excluded - is it unplugged?");
      }
      break;
    case 4:
      if (v.live.cool || cb != GuardBlock::Sensors) { finish(now, model, Result::Fail, "cooling not off for sensors with the chamber out"); break; }
      say(now, "cooling OFF (sensors: chamber) - correct. >>> METER NOW: J10 pin 1 to pin 2 = about 0 V <<<");
      phase(5, now);
      break;
    case 5:   // 15 s to meter, then the replug
      if (elapsedMs(now, phaseAtMs_) < 15000) break;
      say(now, "4d.2 >>> PLUG ch6 BACK IN NOW <<< ~3 min to recover, then the %s hold", secs(a, sizeof(a), f.sensorRecoveryHoldMs));
      tempMetMs_ = 0;
      phase(6, now);
      break;
    case 6:
      if (h->chamberHealthy() && h->healthyCabinet() >= f.minHealthyCabinet && tempMetMs_ == 0) {
        tempMetMs_ = now ? now : 1;
        say(now, "chamber healthy again: cooling recovery hold starts");
      }
      if (wentOn_[0]) {
        if (tempMetMs_ == 0 || elapsedMs(now, tempMetMs_) + 50 < f.sensorRecoveryHoldMs) {
          finish(now, model, Result::Fail, "cooling resumed before the recovery hold");
          break;
        }
        say(now, "cooling ON %s after the chamber recovered - correct (meter about 12 V again)", secs(a, sizeof(a), elapsedMs(now, tempMetMs_)));
        finish(now, model, Result::Pass, "cooling off with the chamber sensor out; resumed after recovery and the hold");
      } else if (elapsedMs(now, phaseAtMs_) > 420000) {
        finish(now, model, Result::Fail, "cooling did not resume within 7 min");
      }
      break;
  }
}

// ------------------------------------------------------------------ run 5
// B3 test 5, divergence (scripted). Cabinet sensor ch3 scripted 5.5 C above the
// others: excluded once it has been out for 10 min. Then back in line: it recovers
// after 10 reads within 5 C of the median and its serial re-checked.
void ScenarioRunner::run5(const ScenarioView& v, Controller& ctl, CabinetModel& model) {
  (void)ctl;
  const uint32_t now = v.nowMs;
  const SafetySettings& f = s_.safety;
  const SensorHealth* h = v.health;
  constexpr int kIdx = 1;   // ch3, bottom front
  char a[16], b[16], c[120];
  if (!h) { finish(now, model, Result::Fail, "no health information"); return; }

  switch (phase_) {
    case 0:
      say(now, "TEST 5 - divergence. Cabinet sensor ch3 scripted 5.5 C above the others; excluded after %s out. No loads.",
          secs(a, sizeof(a), f.divergenceMs));
      demand_ = Demand{};
      model.setOffset(kIdx, f.divergenceC + 0.5f, 0);
      expectMs_ = 0;
      item_ = 0;
      phase(1, now);
      break;

    case 1:
      if (!v.sweepDone) {
        if (elapsedMs(now, phaseAtMs_) > f.divergenceMs + 180000) finish(now, model, Result::Fail, "never excluded for divergence");
        break;
      }
      if (h->diverging(kIdx) && expectMs_ == 0) {
        expectMs_ = now ? now : 1;
        say(now, "ch3 diverging (more than %.1f C from the median of the healthy cabinet sensors); timer started", (double)f.divergenceC);
      }
      if (h->reason(kIdx) == Exclusion::Divergence) {
        const uint32_t out = expectMs_ ? elapsedMs(now, expectMs_) : 0;
        if (expectMs_ == 0 || out < f.divergenceMs || out > f.divergenceMs + 2 * s_.tuning.sensorReadMs) {
          snprintf(c, sizeof(c), "excluded after %s diverging, expected %s", secs(a, sizeof(a), out), secs(b, sizeof(b), f.divergenceMs));
          finish(now, model, Result::Fail, c);
          break;
        }
        say(now, "ch3 EXCLUDED for divergence after %s - correct. Scripted back in line: it needs 10 reads within %.1f C, then its serial re-checked",
            secs(a, sizeof(a), out), (double)f.divergenceC);
        model.setOffset(kIdx, 0, 0);
        phase(2, now);
      } else if (!h->healthy(kIdx)) {
        snprintf(c, sizeof(c), "ch3 excluded for %s, expected divergence", exclusionName(h->reason(kIdx)));
        finish(now, model, Result::Fail, c);
      } else if (expectMs_ && elapsedMs(now, expectMs_) / 120000 > (uint32_t)item_) {
        item_++;
        say(now, "ch3 still diverging, %s so far", secs(a, sizeof(a), elapsedMs(now, expectMs_)));
      }
      break;

    case 2:
      if (h->healthy(kIdx)) {
        const uint32_t took = elapsedMs(now, phaseAtMs_);
        if (took + 1000 < (f.recoveryGoodReads - 1) * s_.tuning.sensorReadMs) {
          snprintf(c, sizeof(c), "recovered after only %s", secs(a, sizeof(a), took));
          finish(now, model, Result::Fail, c);
          break;
        }
        say(now, "ch3 RECOVERED %s after coming back in line", secs(a, sizeof(a), took));
        finish(now, model, Result::Pass, "excluded after 10 min of divergence; recovered after 10 reads within 5 C and a serial re-check");
      } else if (elapsedMs(now, phaseAtMs_) > 300000) {
        finish(now, model, Result::Fail, "ch3 did not recover within 5 min");
      }
      break;
  }
}

// ------------------------------------------------------------------ run 6
// B3 test 6, fan failure. Heat and humidify asked for; Josh unplugs J2.
// Heater off; humidifier unaffected; a kickstart at once, another after the retry
// interval; Josh replugs; the fan recovers and the heater resumes.
void ScenarioRunner::run6(const ScenarioView& v, Controller& ctl, CabinetModel& model) {
  const uint32_t now = v.nowMs;
  const SafetySettings& f = s_.safety;
  const FanMonitor* fm = v.fan;
  char a[16], c[120];
  if (!fm) { finish(now, model, Result::Fail, "no fan information"); return; }

  // Throughout: the humidifier is never held for the fan.
  if (ctl.decisions().humid.block == GuardBlock::Fan) {
    finish(now, model, Result::Fail, "the humidifier was held for the fan");
    return;
  }

  switch (phase_) {
    case 0:
      say(now, "TEST 6 - fan failure. Heat and humidify asked for. You will unplug J2 when told.");
      demand_ = Demand{};
      demand_.heat = demand_.humid = true;
      deadlineMs_ = ctl.hold().remainingMs() + f.heaterMinOffMs + s_.tuning.crossoverDelayMs + 20000;
      phase(1, now);
      break;

    case 1:
      if (v.live.heat && v.live.humid) {
        const uint32_t leftMs = f.heaterMaxRunMs - 10000;
        say(now, "heater and humidifier ON");
        say(now, "6.1 >>> UNPLUG THE PLENUM FAN PLUG (J2) NOW <<< (within %s)", secs(a, sizeof(a), leftMs));
        phase(2, now);
      } else if (elapsedMs(now, phaseAtMs_) > deadlineMs_) {
        finish(now, model, Result::Fail, "heater and humidifier did not both start");
      }
      break;

    case 2:   // waiting for the stall
      if (fm->stalled()) {
        if (fm->kicksThisStall() != 1) {
          snprintf(c, sizeof(c), "%lu kickstarts at the stall, expected 1", (unsigned long)fm->kicksThisStall());
          finish(now, model, Result::Fail, c);
          break;
        }
        say(now, "FAN STALL detected; kickstart 1 at once - correct");
        expectMs_ = now ? now : 1;
        phase(3, now);
      } else if (wentOff_[1] && ctl.decisions().heat.block != GuardBlock::Fan) {
        finish(now, model, Result::Fail, "the heater reached its max run before J2 was unplugged. Type run 6 again");
      } else if (elapsedMs(now, phaseAtMs_) > 120000) {
        finish(now, model, Result::Fail, "no stall seen in 2 min. Is J2 unplugged?");
      }
      break;

    case 3:   // heater off for the fan
      if (v.live.heat || ctl.decisions().heat.block != GuardBlock::Fan) {
        if (elapsedMs(now, phaseAtMs_) > 100) { finish(now, model, Result::Fail, "heater not off for the fan"); break; }
        break;
      }
      say(now, "heater OFF (fan) - correct; humidifier %s", v.live.humid ? "still ON - correct" : "not held for the fan - correct");
      say(now, "waiting for the next kickstart (every %s while the tach reads zero) - leave J2 out", secs(a, sizeof(a), f.fanStallRetryMs));
      phase(4, now);
      break;

    case 4:   // the retry kickstart
      if (fm->kicksThisStall() >= 2) {
        const uint32_t gap = elapsedMs(now, expectMs_);
        if (gap + 6000 < f.fanStallRetryMs || gap > f.fanStallRetryMs + 6000) {
          char b[16];
          snprintf(c, sizeof(c), "second kickstart %s after the first, expected %s", secs(a, sizeof(a), gap),
                   secs(b, sizeof(b), f.fanStallRetryMs));
          finish(now, model, Result::Fail, c);
          break;
        }
        say(now, "kickstart 2, %s after the first - correct", secs(a, sizeof(a), gap));
        say(now, "6.2 >>> PLUG J2 BACK IN NOW <<<");
        phase(5, now);
      } else if (!fm->stalled()) {
        finish(now, model, Result::Fail, "the fan recovered before J2 was replugged");
      } else if (elapsedMs(now, phaseAtMs_) > f.fanStallRetryMs + 20000) {
        finish(now, model, Result::Fail, "no second kickstart");
      }
      break;

    case 5:   // waiting for the tach
      if (!fm->stalled()) {
        say(now, "FAN RECOVERED - tach reads again");
        phase(6, now);
      } else if (elapsedMs(now, phaseAtMs_) > 3 * f.fanStallRetryMs + 30000) {
        finish(now, model, Result::Fail, "the fan did not recover. Check J2 is plugged in");
      }
      break;

    case 6:   // heater resumes
      if (v.live.heat) {
        finish(now, model, Result::Pass, "heater off and humidifier unaffected; kickstart at once and after the retry interval; "
                                         "heater resumed once the tach read again");
      } else if (elapsedMs(now, phaseAtMs_) > f.heaterMinOffMs + 30000) {
        snprintf(c, sizeof(c), "heater did not resume - held by %s", guardBlockName(ctl.decisions().heat.block));
        finish(now, model, Result::Fail, c);
      }
      break;
  }
}

// ------------------------------------------------------------------ run 15
// B3 test 15, I2C bus fault (real). Josh shorts one sensor's SDA to GND. The bus
// locks while that channel is selected: mux RST pulsed, I2C restarted, the channel
// excluded; the RTC and the other sensors keep working; the ESP32's internal
// pull-ups stay off. Short removed: the channel recovers.
void ScenarioRunner::run15(const ScenarioView& v, Controller& ctl, CabinetModel& model) {
  (void)ctl;
  const uint32_t now = v.nowMs;
  const SensorHealth* h = v.health;
  char c[120];
  if (!h) { finish(now, model, Result::Fail, "no health information"); return; }
  if (v.pullupsOn) { finish(now, model, Result::Fail, "the ESP32's internal I2C pull-ups are ON (5.3)"); return; }

  switch (phase_) {
    case 0:
      say(now, "TEST 15 - I2C bus fault. No loads.");
      demand_ = Demand{};
      stuckAtStart_ = v.busStuck;
      say(now, "15.1 >>> SHORT ONE SENSOR'S SDA TO GND NOW <<< (and leave it)");
      phase(1, now);
      break;

    case 1:   // waiting for the stuck bus, then the sweep that reports it
      if (v.busStuck > stuckAtStart_ && v.sweepDone) {
        target_ = -1;
        for (int i = 0; i < kNumSensors; i++) if (h->reason(i) == Exclusion::BusFault) target_ = i;
        if (target_ < 0) { finish(now, model, Result::Fail, "bus stuck, but no channel excluded for a bus fault"); break; }
        say(now, "BUS STUCK detected with ch%d selected: mux RST pulsed, I2C restarted, ch%d excluded (bus-fault)",
            channelOf(target_), channelOf(target_));
        item_ = 0;
        phase(2, now);
      } else if (elapsedMs(now, phaseAtMs_) > 120000) {
        finish(now, model, Result::Fail, "no stuck bus in 2 min. Is the SDA short in place?");
      }
      break;

    case 2:   // two more sweeps: the RTC and the other sensors keep working
      if (!v.sweepDone) break;
      if (!v.rtcOk) { finish(now, model, Result::Fail, "the RTC stopped reading during the fault"); break; }
      for (int i = 0; i < kNumSensors; i++) {
        if (i != target_ && !h->healthy(i)) {
          snprintf(c, sizeof(c), "ch%d excluded (%s) during the fault on ch%d", channelOf(i), exclusionName(h->reason(i)), channelOf(target_));
          finish(now, model, Result::Fail, c);
          return;
        }
      }
      if (++item_ < 2) break;
      say(now, "two sweeps with the short in place: RTC reading, the other five sensors healthy, internal pull-ups off - correct");
      say(now, "15.2 >>> REMOVE THE SHORT NOW <<< ch%d needs 10 good reads and its serial re-checked (~2.5 min)", channelOf(target_));
      phase(3, now);
      break;

    case 3:
      if (h->healthy(target_)) {
        say(now, "ch%d RECOVERED", channelOf(target_));
        finish(now, model, Result::Pass, "stuck bus detected, RST pulsed, channel excluded; RTC and others kept working; channel recovered");
      } else if (elapsedMs(now, phaseAtMs_) > 360000) {
        finish(now, model, Result::Fail, "the channel did not recover within 6 min of removing the short");
      }
      break;
  }
}

}  // namespace wineador

#endif  // WINEADOR_TEST_BUILD
