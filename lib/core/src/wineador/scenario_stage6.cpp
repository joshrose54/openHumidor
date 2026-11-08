// TEST BUILDS ONLY — the stage 6 scenarios.
//
//   run 24    B3 test 24 (logging), the automatic part: a chain that makes the
//             control event types appear in the events file. Josh adds the
//             physical ones by hand (see the bench steps).
//   run soak  B4 step 1: 21 h on the real_test build (REAL timers). A cycle of
//             phases sets the model's room value and drift so every load cycles
//             several times and runs into its real limits; every pass audited.
#include "wineador/scenario.h"

#if defined(WINEADOR_TEST_BUILD)

#include <stdio.h>

namespace wineador {

namespace {
const char* secs6(char* out, size_t n, uint32_t ms) {
  if (ms < 60000) snprintf(out, n, "%lu.%lu s", (unsigned long)(ms / 1000), (unsigned long)((ms % 1000) / 100));
  else if (ms < 3600000UL) snprintf(out, n, "%lu:%02lu", (unsigned long)(ms / 60000), (unsigned long)((ms / 1000) % 60));
  else snprintf(out, n, "%lu:%02lu:%02lu", (unsigned long)(ms / 3600000UL), (unsigned long)((ms / 60000) % 60),
                (unsigned long)((ms / 1000) % 60));
  return out;
}
}  // namespace

// ------------------------------------------------------------------ run 24
// Each step waits for its event, with a time limit; a step that times out FAILs.
void ScenarioRunner::run24(const ScenarioView& v, Controller& ctl, CabinetModel& model, TemperatureControl& tc) {
  const uint32_t now = v.nowMs;
  const HumidControl& hc = *v.hc;
  const uint32_t inPhase = elapsedMs(now, phaseAtMs_);
  auto timeout = [&](uint32_t ms, const char* what) {
    if (inPhase > ms) { finish(now, model, Result::Fail, what); return true; }
    return false;
  };
  audit_.update(now, v.live);
  if (audit_.breached()) { finish(now, model, Result::Fail, audit_.breach()); return; }

  switch (phase_) {
    case 0:
      say(now, "TEST 24 (automatic part) - cooling, heating (crossover), strip, humidify, heater ineffective, relay "
               "exercise. About 12 minutes. Each makes its events in the events file.");
      model.setCabinetC(21.0f);
      phase(1, now);
      break;
    case 1:   // cooling run to its end -> compressor on/off, harvest
      if (wentOff_[0]) { say(now, "1/6 cooling run done (compressor on/off, harvest)"); model.setCabinetC(18.0f); phase(2, now); }
      else timeout(minutes(4), "cooling run did not happen");
      break;
    case 2:   // heating straight after: guard block (crossover), crossover, interlock, harvest
      if (wentOff_[1]) {
        say(now, "2/6 heating run done (guard block crossover, crossover, K1 both ways, harvest)");
        model.setCabinetC(20.3f);
        model.setCabinetRh(68.0f);
        model.rates().coolCPerMin = -0.3f;    // gentle, so vapour pressure releases the strip before heating is asked for
        phase(3, now);
      } else timeout(minutes(4), "heating run did not happen");
      break;
    case 3:   // strip start / stop
      if (passesInPhase_ > 1 && !tc.stripping() && tc.stripCount() > 0 && !v.live.cool) {
        say(now, "3/6 strip done (strip start / stop, compressor for stripping)");
        model.setCabinetC(20.0f);
        model.setCabinetRh(56.0f);
        phase(4, now);
      } else timeout(minutes(8), "strip did not start and stop");
      break;
    case 4:   // humidify call, humidifier on / off
      if (hc.calls() > 0 && !hc.call() && !v.live.humid) {
        say(now, "4/6 humidify done (call start / end, humidifier on / off)");
        model.rates().heatResponds = false;
        model.setCabinetC(18.0f);
        phase(5, now);
      } else timeout(minutes(6), "humidify call did not start and end");
      break;
    case 5:   // heater ineffective
      if (tc.heaterIneffective()) {
        say(now, "5/6 heater ineffective done (flagged; it would retry in 2 min - not waited for)");
        model.rates().heatResponds = true;
        model.setCabinetC(20.0f);
        phase(6, now);
      } else timeout(minutes(4), "heater ineffective did not happen");
      break;
    case 6:   // relay exercise, once both loads are off
      if (!v.live.cool && !v.live.heat && passesInPhase_ > 1 && !flag_) {
        flag_ = true;
        ctl.requestExercise(now);
        say(now, "6/6 relay exercise requested");
      }
      if (flag_ && !ctl.exercise().waiting() && !ctl.exercise().cycling() && inPhase > seconds(2)) {
        finish(now, model, Result::Pass, "automatic event types produced: compressor, heater, humidifier, interlock, harvest, "
                                         "guard, crossover, strip, humidify, ineffective, exercise. Now the physical ones.");
      } else timeout(minutes(3), "relay exercise did not finish");
      break;
  }
}

// ------------------------------------------------------------------ run soak
namespace {
struct SoakPhase {
  const char* name;
  uint32_t minutes;
  float roomC;
  float driftCPerMin;   // magnitude, towards the room value
  float setRh;          // < 0: leave RH as it is
  float coolVpKpaPerMin;
};
// One cycle, about 9.5 h. Repeated until the end (21 h). A2 cooling/heating rates (0.1 C/min).
const SoakPhase kPhases[] = {
  {"warm: normal cooling cycles",               90, 23.0f, 0.02f, -1.0f, 0.002f},
  {"heat wave: compressor into its max run",    45, 30.0f, 0.15f, -1.0f, 0.002f},
  {"settle",                                    30, 20.0f, 0.02f, -1.0f, 0.002f},
  {"cold: heating cycles",                      90, 15.0f, 0.04f, -1.0f, 0.0f},
  {"cold snap: heater into its max run",        70, 10.0f, 0.15f, -1.0f, 0.0f},
  // Fast enough (0.3 C/min) that the other load is wanted ~5 min after one stops:
  // the 10 min crossover delay has to hold it.
  {"hot flip: into the crossover delay",        30, 30.0f, 0.30f, -1.0f, 0.0f},
  {"cold flip: into the crossover delay",       35, 10.0f, 0.30f, -1.0f, 0.0f},
  {"settle",                                    20, 20.0f, 0.02f, 62.0f, 0.0f},
  {"dry: humidifier runs into its max run",     80, 20.5f, 0.006f, 55.0f, 0.0f},
  // At setpoint temperature, so no temperature cooling dries it first: the strip must.
  {"humid: RH strip",                           80, 20.0f, 0.006f, 72.0f, 0.02f},
};
constexpr int kNumPhases = sizeof(kPhases) / sizeof(kPhases[0]);

// The least each must reach over the 21 h (calibrated on the Mac with the same
// model; the board must match it).
constexpr uint32_t kMinStarts[3] = {10, 6, 4};       // compressor, heater, humidifier (the Mac run, 21 h: 15 / 12 / 15)
constexpr uint32_t kMinMaxRun[3] = {2, 1, 2};        // stops at the max run (the Mac run: 4 / 2 / 4)
constexpr uint32_t kMinStrips = 2, kMinCrossovers = 4;   // the Mac run: 2, 8
}  // namespace

void ScenarioRunner::runSoak(const ScenarioView& v, Controller& ctl, CabinetModel& model, TemperatureControl& tc) {
  (void)ctl;
  const uint32_t now = v.nowMs;
  const HumidControl& hc = *v.hc;
  char a[16], m[220];
  audit_.update(now, v.live);
  if (audit_.breached()) { finish(now, model, Result::Fail, audit_.breach()); return; }

  // phase changes
  if (soakPhase_ < 0 || elapsedMs(now, phaseAtMs_) >= minutes(kPhases[soakPhase_].minutes)) {
    soakPhase_++;
    if (soakPhase_ >= kNumPhases) { soakPhase_ = 0; soakCycle_++; }
    const SoakPhase& p = kPhases[soakPhase_];
    ModelRates& r = model.rates();
    r.roomC = p.roomC;
    r.driftCPerMin = p.driftCPerMin;
    r.coolVpKpaPerMin = p.coolVpKpaPerMin;
    if (p.setRh > 0) model.setCabinetRh(p.setRh);
    phase(soakPhase_ + 1, now);
    say(now, "SOAK cycle %lu phase %d/%d: %s (%lu min; room %.1f C, drift %.3f C/min%s)", (unsigned long)soakCycle_ + 1,
        soakPhase_ + 1, kNumPhases, p.name, (unsigned long)p.minutes, p.roomC, p.driftCPerMin, p.setRh > 0 ? ", RH set" : "");
  }

  // a progress line every 15 min
  if (elapsedMs(now, lastReportMs_) >= minutes(15)) {
    lastReportMs_ = now;
    say(now, "SOAK %s: starts compressor %lu heater %lu humidifier %lu | max-run stops %lu/%lu/%lu | strips %lu | "
             "crossovers %lu | cabinet %.2f C RH %.1f%% | audit clean",
        secs6(a, sizeof(a), elapsedMs(now, startMs_)), (unsigned long)audit_.starts(0), (unsigned long)audit_.starts(1),
        (unsigned long)audit_.starts(2), (unsigned long)audit_.maxRunStops(0), (unsigned long)audit_.maxRunStops(1),
        (unsigned long)audit_.maxRunStops(2), (unsigned long)tc.stripCount(), (unsigned long)tc.crossovers(),
        model.cabinetC(), model.cabinetRh());
  }

  if (elapsedMs(now, startMs_) < kSoakMs) return;
  // the end: the counts
  const char* shortOf = nullptr;
  static const char* names[3] = {"compressor starts", "heater starts", "humidifier starts"};
  static const char* maxNames[3] = {"compressor max-run stops", "heater max-run stops", "humidifier max-run stops"};
  for (int L = 0; L < 3 && !shortOf; L++) {
    if (audit_.starts(L) < kMinStarts[L]) shortOf = names[L];
    else if (audit_.maxRunStops(L) < kMinMaxRun[L]) shortOf = maxNames[L];
  }
  if (!shortOf && tc.stripCount() < kMinStrips) shortOf = "strips";
  if (!shortOf && tc.crossovers() < kMinCrossovers) shortOf = "crossovers";
  snprintf(m, sizeof(m), "21 h: starts %lu/%lu/%lu (need %lu/%lu/%lu), max-run stops %lu/%lu/%lu, strips %lu, crossovers %lu, "
                         "humidifier calls %lu; no guard breached",
           (unsigned long)audit_.starts(0), (unsigned long)audit_.starts(1), (unsigned long)audit_.starts(2),
           (unsigned long)kMinStarts[0], (unsigned long)kMinStarts[1], (unsigned long)kMinStarts[2],
           (unsigned long)audit_.maxRunStops(0), (unsigned long)audit_.maxRunStops(1), (unsigned long)audit_.maxRunStops(2),
           (unsigned long)tc.stripCount(), (unsigned long)tc.crossovers(), (unsigned long)hc.calls());
  if (shortOf) {
    char f[260];
    snprintf(f, sizeof(f), "too few %s. %s", shortOf, m);
    finish(now, model, Result::Fail, f);
  } else {
    finish(now, model, Result::Pass, m);
  }
}

}  // namespace wineador

#endif  // WINEADOR_TEST_BUILD
