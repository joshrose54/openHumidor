#include "wineador/guards.h"

namespace wineador {

const char* guardBlockName(GuardBlock b) {
  switch (b) {
    case GuardBlock::None:            return "clear";
    case GuardBlock::SettingsProblem: return "settings-problem";
    case GuardBlock::BootHold:        return "boot-hold";
    case GuardBlock::RelayExercise:   return "relay-exercise";
    case GuardBlock::Disabled:        return "disabled";
    case GuardBlock::Sensors:         return "sensors";
    case GuardBlock::Fan:             return "fan";
    case GuardBlock::CabinetCeiling:  return "cabinet-ceiling";
    case GuardBlock::ChamberCeiling:  return "chamber-ceiling";
    case GuardBlock::RhCeiling:       return "rh-ceiling";
    case GuardBlock::Stripping:       return "stripping";
    case GuardBlock::CompressorRun:   return "compressor-run";
    case GuardBlock::MaxRun:          return "max-run";
    case GuardBlock::MinOff:          return "min-off";
    case GuardBlock::Crossover:       return "crossover";
    case GuardBlock::OtherLoad:       return "other-load";
  }
  return "?";
}

void RuntimeGuards::begin(const Settings& s, uint32_t nowMs) {
  s_ = s;
  // Worst case at boot (4.4): every load counts as having just stopped, so min off
  // AND the crossover delay (from the other temperature load) run from boot.
  cool_ = heat_ = humid_ = Timer{};
  cool_.offSinceMs = heat_.offSinceMs = humid_.offSinceMs = nowMs;
}

void RuntimeGuards::track(Timer& t, bool liveNow, uint32_t nowMs) {
  if (liveNow && !t.on) { t.on = true; t.onSinceMs = nowMs; }
  if (!liveNow && t.on) { t.on = false; t.offSinceMs = nowMs; }
}

namespace {

uint32_t left(uint32_t needMs, uint32_t hadMs) { return hadMs >= needMs ? 0 : needMs - hadMs; }

// Applies the shared decision shape: stop guards first, then max run, then start
// guards, then min-on.
//   stop     the first stop guard that fails (None if all pass)
//   start    the first start guard that fails, with its time left (None if all pass)
void decide(LoadDecision& d, bool demanded, bool live, uint32_t onForMs, uint32_t minOnMs,
            uint32_t maxRunMs, GuardBlock stop, GuardBlock start, uint32_t startWaitMs) {
  d = LoadDecision{};
  d.demanded = demanded;
  if (stop != GuardBlock::None) { d.block = stop; return; }
  if (live && onForMs >= maxRunMs) { d.block = GuardBlock::MaxRun; return; }
  if (live) {
    // Already running: start guards no longer matter. Keep it on while asked,
    // or until its minimum on has passed.
    if (demanded) { d.run = true; return; }
    if (onForMs < minOnMs) { d.run = true; d.heldOnByMinOn = true; return; }
    return;   // demand gone, minimum on passed: off
  }
  if (!demanded) return;
  if (start != GuardBlock::None) { d.block = start; d.waitMs = startWaitMs; return; }
  d.run = true;
}

}  // namespace

GuardDecisions RuntimeGuards::evaluate(const GuardInputs& in, const Demand& dm, const LoadOutputs& actual) {
  const uint32_t now = in.nowMs;
  track(cool_, actual.cool, now);
  track(heat_, actual.heat, now);
  track(humid_, actual.humid, now);

  const SafetySettings& f = s_.safety;
  const TuningSettings& t = s_.tuning;
  const UserSettings& u = s_.user;
  const CabinetSummary& c = in.cab;

  const bool cabinetEnough = in.readingsFresh && c.cabinetOk >= f.minHealthyCabinet;
  const bool chamberHealthy = in.readingsFresh && c.chamberOk;

  // Stop guards common to all three loads, in order of importance.
  GuardBlock common = GuardBlock::None;
  if (!in.settingsOk)          common = GuardBlock::SettingsProblem;
  else if (in.bootHoldActive)  common = GuardBlock::BootHold;

  GuardDecisions out;

  // ---------------------------------------------------------- compressor
  {
    GuardBlock stop = common;
    if (stop == GuardBlock::None && in.exerciseCycling)                    stop = GuardBlock::RelayExercise;
    if (stop == GuardBlock::None && !(chamberHealthy && cabinetEnough))    stop = GuardBlock::Sensors;
    if (stop == GuardBlock::None && !in.fanOk)                             stop = GuardBlock::Fan;

    GuardBlock start = GuardBlock::None;
    uint32_t wait = 0;
    const uint32_t offFor = elapsedMs(now, cool_.offSinceMs);
    const uint32_t heatOffFor = elapsedMs(now, heat_.offSinceMs);
    if (heat_.on)                                    { start = GuardBlock::OtherLoad; }
    else if (in.exerciseWaiting)                     { start = GuardBlock::RelayExercise; }
    else if (offFor < f.compressorMinOffMs)          { start = GuardBlock::MinOff; wait = left(f.compressorMinOffMs, offFor); }
    else if (heatOffFor < t.crossoverDelayMs)
                                                     { start = GuardBlock::Crossover; wait = left(t.crossoverDelayMs, heatOffFor); }
    decide(out.cool, dm.cool, cool_.on, elapsedMs(now, cool_.onSinceMs), f.compressorMinOnMs,
           f.compressorMaxRunMs, stop, start, wait);
  }

  // ---------------------------------------------------------- heater
  {
    GuardBlock stop = common;
    if (stop == GuardBlock::None && in.exerciseCycling)                    stop = GuardBlock::RelayExercise;
    if (stop == GuardBlock::None && !u.heaterEnable)                       stop = GuardBlock::Disabled;
    if (stop == GuardBlock::None && !(chamberHealthy && cabinetEnough))    stop = GuardBlock::Sensors;
    if (stop == GuardBlock::None && !in.fanOk)                             stop = GuardBlock::Fan;
    if (stop == GuardBlock::None && c.cabinetOk > 0 &&
        c.maxC > u.targetTempC + f.cabinetCeilingAboveSetpointC)           stop = GuardBlock::CabinetCeiling;
    if (stop == GuardBlock::None && c.chamberOk && c.chamberC > f.chamberCeilingC)
                                                                           stop = GuardBlock::ChamberCeiling;

    GuardBlock start = GuardBlock::None;
    uint32_t wait = 0;
    const uint32_t offFor = elapsedMs(now, heat_.offSinceMs);
    const uint32_t coolOffFor = elapsedMs(now, cool_.offSinceMs);
    // Both asked for at once with neither running: cooling goes first (its relay
    // side is the safe, de-energised one), so the heater waits behind it.
    if (cool_.on || out.cool.run)                    { start = GuardBlock::OtherLoad; }
    else if (in.exerciseWaiting)                     { start = GuardBlock::RelayExercise; }
    else if (offFor < f.heaterMinOffMs)              { start = GuardBlock::MinOff; wait = left(f.heaterMinOffMs, offFor); }
    else if (coolOffFor < t.crossoverDelayMs)
                                                     { start = GuardBlock::Crossover; wait = left(t.crossoverDelayMs, coolOffFor); }
    decide(out.heat, dm.heat, heat_.on, elapsedMs(now, heat_.onSinceMs), f.heaterMinOnMs,
           f.heaterMaxRunMs, stop, start, wait);
  }

  // ---------------------------------------------------------- humidifier
  {
    GuardBlock stop = common;
    if (stop == GuardBlock::None && !u.humidifierEnable)                   stop = GuardBlock::Disabled;
    if (stop == GuardBlock::None && !cabinetEnough)                        stop = GuardBlock::Sensors;
    if (stop == GuardBlock::None && c.cabinetOk > 0 && c.maxRhPct >= f.rhHardCeilingPct)
                                                                           stop = GuardBlock::RhCeiling;
    if (stop == GuardBlock::None && in.strippingActive)                    stop = GuardBlock::Stripping;
    // Stage 2 counts every compressor run as temperature-driven: stripping is stage 4.
    if (stop == GuardBlock::None && !t.humidifyDuringCooling && (cool_.on || out.cool.run))
                                                                           stop = GuardBlock::CompressorRun;

    GuardBlock start = GuardBlock::None;
    uint32_t wait = 0;
    const uint32_t offFor = elapsedMs(now, humid_.offSinceMs);
    if (offFor < f.humidMinOffMs) { start = GuardBlock::MinOff; wait = left(f.humidMinOffMs, offFor); }
    decide(out.humid, dm.humid, humid_.on, elapsedMs(now, humid_.onSinceMs), 0 /* no minimum on */,
           f.humidMaxRunMs, stop, start, wait);
  }

  // Never both temperature loads (belt and braces: the relay also makes it impossible).
  if (out.cool.run && out.heat.run) { out.heat.run = false; out.heat.block = GuardBlock::OtherLoad; }
  return out;
}

}  // namespace wineador
