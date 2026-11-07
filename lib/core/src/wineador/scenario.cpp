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
  fanKick_ = false;
  lastKickMs_ = nowMs;
  for (int i = 0; i < kNumSensors; i++) model.setOffset(i, 0, 0);
  savedRates_ = model.rates();
  if (n == 1) {
    // Test 1 is about timing only. With the real profile the heater's 60 min
    // max run would warm the model past the cabinet ceiling, so the loads'
    // effect on the model is switched off while it runs.
    model.rates().coolResponds = model.rates().heatResponds = model.rates().humidResponds = false;
  }
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

void ScenarioRunner::update(const ScenarioView& v, Controller& ctl, CabinetModel& model) {
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
    case 13: run13(v, ctl, model); break;
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
// B3 test 2, heater guards. Heater running; trip one guard; the heater must be
// off on the very next pass. Then clear it and let the heater restart.
//   item 0: cabinet ceiling (one cabinet sensor scripted above setpoint + 3 C)
//   item 1: chamber ceiling (chamber scripted above 30 C)
//   item 2: fan (Josh unplugs J2; the tach reads zero)
void ScenarioRunner::run2(const ScenarioView& v, Controller& ctl, CabinetModel& model) {
  const uint32_t now = v.nowMs;
  const SafetySettings& f = s_.safety;
  const float target = s_.user.targetTempC;
  char a[16], c[96];

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
        say(now, "heater ON");
        phase(2, now);
      } else if (elapsedMs(now, phaseAtMs_) > deadlineMs_) {
        snprintf(c, sizeof(c), "heater did not start - held by %s", guardBlockName(ctl.decisions().heat.block));
        finish(now, model, Result::Fail, c);
      }
      break;

    case 2:   // trip the next guard
      if (item_ == 0) {
        const float want = target + f.cabinetCeilingAboveSetpointC + 0.5f;
        model.setOffset(0, want - model.tempC(0), 0);
        say(now, "2.1 TRIP cabinet ceiling: cabinet sensor ch2 scripted to %.1f C (ceiling %.1f C)", (double)want,
            (double)(target + f.cabinetCeilingAboveSetpointC));
        phase(3, now);
      } else if (item_ == 1) {
        const float want = f.chamberCeilingC + 0.5f;
        model.setOffset(kChamberIndex, want - model.tempC(kChamberIndex), 0);
        say(now, "2.2 TRIP chamber ceiling: chamber sensor ch6 scripted to %.1f C (ceiling %.1f C)", (double)want, (double)f.chamberCeilingC);
        phase(3, now);
      } else {
        const uint32_t ranFor = elapsedMs(now, onAtMs_[1]);
        const uint32_t leftMs = f.heaterMaxRunMs > ranFor ? f.heaterMaxRunMs - ranFor : 0;
        say(now, "2.3 FAN: >>> UNPLUG THE PLENUM FAN PLUG (J2) NOW <<< (within %s, before the heater reaches its max run). Waiting for the tach to read zero",
            secs(a, sizeof(a), leftMs > 5000 ? leftMs - 5000 : 0));
        phase(5, now);
      }
      break;

    case 3: {   // the very next pass: the heater must be off
      const GuardBlock want = item_ == 0 ? GuardBlock::CabinetCeiling : GuardBlock::ChamberCeiling;
      const GuardBlock got = ctl.decisions().heat.block;
      if (v.live.heat) {
        finish(now, model, Result::Fail, "heater still on one pass after the trip");
        break;
      }
      if (got != want) {
        snprintf(c, sizeof(c), "heater off, but for the wrong reason: %s (expected %s)", guardBlockName(got), guardBlockName(want));
        finish(now, model, Result::Fail, c);
        break;
      }
      say(now, "heater OFF on the next pass (%s) - correct. Trip cleared; the heater restarts after its min off (%s)",
          guardBlockName(got), secs(a, sizeof(a), f.heaterMinOffMs));
      model.setOffset(item_ == 0 ? 0 : kChamberIndex, 0, 0);
      phase(4, now);
      break;
    }

    case 4:   // waiting for the restart
      if (v.live.heat) {
        say(now, "heater ON again");
        item_++;
        phase(2, now);
      } else if (elapsedMs(now, phaseAtMs_) > f.heaterMinOffMs + 10000) {
        snprintf(c, sizeof(c), "heater did not restart after the trip was cleared - held by %s", guardBlockName(ctl.decisions().heat.block));
        finish(now, model, Result::Fail, c);
      }
      break;

    case 5:   // waiting for the tach to read zero
      if (!v.fanOk) {
        if (v.live.heat) {               // the guards see it on the next pass
          say(now, "tach reads zero");
          phase(6, now);
        } else if (wentOff_[1] && ctl.decisions().heat.block == GuardBlock::Fan) {
          // The tach dropped between passes: the guards saw it on this same pass.
          say(now, "tach reads zero; heater OFF on that same pass (fan) - correct");
          say(now, ">>> PLUG J2 BACK IN NOW <<< Waiting for the fan (it is kicked to 100%% for 1 s every 10 s until the tach reads)");
          demand_ = Demand{};
          lastKickMs_ = now;
          phase(7, now);
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

    case 6: {   // the very next pass
      const GuardBlock got = ctl.decisions().heat.block;
      if (v.live.heat) { finish(now, model, Result::Fail, "heater still on one pass after the tach read zero"); break; }
      if (got != GuardBlock::Fan) {
        snprintf(c, sizeof(c), "heater off, but for the wrong reason: %s (expected fan)", guardBlockName(got));
        finish(now, model, Result::Fail, c);
        break;
      }
      say(now, "heater OFF on the next pass (fan) - correct");
      say(now, ">>> PLUG J2 BACK IN NOW <<< Waiting for the fan (it is kicked to 100%% for 1 s every 10 s until the tach reads)");
      demand_ = Demand{};
      lastKickMs_ = now;
      phase(7, now);
      break;
    }

    case 7:   // waiting for the fan
      if (v.fanOk) {
        say(now, "fan running again");
        say(now, "Not tripped on the board at stage 2: sensors (native tests now; on the board at stage 3, test 4), max run (run 1), relay side (run 3, native tests)");
        finish(now, model, Result::Pass, "heater off on the next pass for the cabinet ceiling, the chamber ceiling and the fan");
      } else if (elapsedMs(now, lastKickMs_) >= 10000) {
        fanKick_ = true;
        lastKickMs_ = now;
      } else if (elapsedMs(now, phaseAtMs_) > 120000) {
        finish(now, model, Result::Fail, "the fan did not come back in 2 min. Check J2 is plugged in");
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

}  // namespace wineador

#endif  // WINEADOR_TEST_BUILD
