#include "wineador/controller.h"
#include <stdio.h>
#include <string.h>

namespace wineador {

void Controller::begin(const Settings& s, uint32_t nowMs) {
  const SafetySettings& f = s.safety;
  hold_.begin(nowMs, f.bootHoldMs);
  guards_.begin(s, nowMs);
  seq_.begin(f.relaySettleMs, nowMs);
  exercise_.begin(nowMs, f.relayExercisePeriodMs, f.relayExerciseOnMs, f.relayExerciseOffMs,
                  f.relayExerciseCycles);
  decisions_ = GuardDecisions{};
  demand_ = Demand{};
}

LoadOutputs Controller::pass(const PassInputs& in, const Demand& demand) {
  demand_ = demand;
  hold_.update(in.nowMs);
  exercise_.update(in.nowMs, !in.live.cool && !in.live.heat);

  GuardInputs g;
  g.nowMs = in.nowMs;
  g.settingsOk = in.settingsOk;
  g.bootHoldActive = hold_.active();
  g.exerciseWaiting = exercise_.waiting();
  g.exerciseCycling = exercise_.cycling();
  g.fanOk = in.fanOk;
  g.readingsFresh = in.readingsFresh;
  g.cab = in.cab;
  g.strippingActive = in.strippingActive;
  decisions_ = guards_.evaluate(g, demand, in.live);

  return seq_.step(in.nowMs, decisions_.cool.run, decisions_.heat.run, decisions_.humid.run,
                   exercise_.relayHold());
}

const char* demandText(char* out, size_t n, const Demand& d) {
  out[0] = 0;
  if (!d.cool && !d.heat && !d.humid) { snprintf(out, n, "none"); return out; }
  size_t len = 0;
  auto add = [&](const char* w) {
    int k = snprintf(out + len, n - len, "%s%s", len ? "+" : "", w);
    if (k > 0) len = (len + k < n) ? len + k : n - 1;
  };
  if (d.cool) add("cool");
  if (d.heat) add("heat");
  if (d.humid) add("humid");
  return out;
}

int formatGuardText(char* out, size_t n, const Controller& c) {
  char dem[24];
  demandText(dem, sizeof(dem), c.demand());
  int len = snprintf(out, n, "demand %s", dem);
  auto add = [&](const char* fmt, auto... args) {
    if (len < 0 || (size_t)len >= n) return;
    int k = snprintf(out + len, n - len, fmt, args...);
    if (k > 0) len = ((size_t)(len + k) < n) ? len + k : (int)n - 1;
  };

  const GuardDecisions& d = c.decisions();
  struct Item { const char* name; const LoadDecision* ld; };
  const Item items[] = {{"cool", &d.cool}, {"heat", &d.heat}, {"humid", &d.humid}};

  bool first = true;
  for (const Item& it : items) {
    const LoadDecision& ld = *it.ld;
    // Held = asked for (or a stop guard is on) and not running.
    if (ld.run || ld.block == GuardBlock::None || !ld.demanded) continue;
    add("%s %s %s", first ? " | held:" : ",", it.name, guardBlockName(ld.block));
    if (ld.block == GuardBlock::BootHold)  add(" %lus", (unsigned long)((c.hold().remainingMs() + 999) / 1000));
    else if (ld.waitMs)                    add(" %lus", (unsigned long)((ld.waitMs + 999) / 1000));
    first = false;
  }
  first = true;
  for (const Item& it : items) {
    if (!it.ld->heldOnByMinOn) continue;
    add("%s %s", first ? " | min-on:" : ",", it.name);
    first = false;
  }
  const RelayExercise& ex = c.exercise();
  if (ex.waiting()) add("%s", " | relay exercise waiting for loads off");
  else if (ex.cycling()) add(" | RELAY EXERCISE %u/%u", (unsigned)ex.cycle(), (unsigned)ex.cycles());
  return len;
}

}  // namespace wineador
