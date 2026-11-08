#include "wineador/temp_control.h"
#include "wineador/psychro.h"
#include <stdarg.h>
#include <stdio.h>
#include <math.h>

namespace wineador {

const char* harvestEndName(HarvestEnd e) {
  switch (e) {
    case HarvestEnd::None:        return "-";
    case HarvestEnd::Convergence: return "chamber converged";
    case HarvestEnd::Backstop:    return "BACKSTOP (no convergence)";
    case HarvestEnd::LoadStarted: return "a load started";
  }
  return "?";
}

const char* stripStopName(StripStop s) {
  switch (s) {
    case StripStop::None:           return "-";
    case StripStop::VapourPressure: return "vapour pressure reached target";
    case StripStop::Floor:          return "temperature floor";
    case StripStop::Heating:        return "heating called (heating wins)";
    case StripStop::Sensors:        return "too few healthy cabinet sensors or stale readings";
    case StripStop::Disabled:       return "stripping disabled";
    case StripStop::Scripted:       return "scenario asking for loads directly";
  }
  return "?";
}

void TemperatureControl::begin(const Settings& s, uint32_t nowMs) {
  (void)nowMs;
  s_ = s;
  coolCall_ = heatCall_ = false;
  prev_ = LoadOutputs{};
  last_ = Last::None;
  crossovers_ = 0;
  harvest_ = HarvestKind::None;
  lastHarvestEnd_ = HarvestEnd::None;
  lastHarvestMs_ = 0;
  gap0_ = -1;
  stripActive_ = persisting_ = false;
  stripCount_ = 0;
  lastStripStop_ = StripStop::None;
  heatCheck_ = coolCheck_ = Check{};
  heaterFlag_ = coolingFlag_ = heaterHold_ = false;
  idleOverride_ = -1;
  fanPct_ = s.tuning.fanIdlePct;
  evHead_ = evCount_ = 0;
}

float TemperatureControl::stripTargetVpKpa() const {
  return vapourPressureKpa(s_.user.targetTempC, s_.user.rhSetpointPct);
}

uint32_t TemperatureControl::heaterRetryLeftMs(uint32_t nowMs) const {
  if (!heaterHold_) return 0;
  const uint32_t had = elapsedMs(nowMs, heaterHoldSinceMs_);
  const uint32_t need = s_.tuning.heaterIneffectiveRetryMs;
  return had >= need ? 0 : need - had;
}

bool TemperatureControl::setIdleFanOverride(int pct) {
  if (pct < 0) { idleOverride_ = -1; return true; }
  if (pct > 100) return false;
  if (pct > 0 && pct < s_.safety.fanMinDutyPct) return false;   // 1-19 %: never commanded (4.6)
  idleOverride_ = pct;
  return true;
}

void TemperatureControl::push(const ControlEvent& e) {
  if (evCount_ == kEvents) { evHead_ = (evHead_ + 1) % kEvents; evCount_--; }   // drop the oldest
  events_[(evHead_ + evCount_) % kEvents] = e;
  evCount_++;
}

bool TemperatureControl::takeEvent(ControlEvent& e) {
  if (evCount_ == 0) return false;
  e = events_[evHead_];
  evHead_ = (evHead_ + 1) % kEvents;
  evCount_--;
  return true;
}

// ---------------------------------------------------------------- the parts
// Load edges: crossover count, harvest start, ineffective checks armed / ended.
void TemperatureControl::edges(const ControlInputs& in) {
  const CabinetSummary& c = in.cab;
  const bool chamberOk = in.readingsFresh && c.chamberOk;
  const bool coolOn = !prev_.cool && in.live.cool, coolOff = prev_.cool && !in.live.cool;
  const bool heatOn = !prev_.heat && in.live.heat, heatOff = prev_.heat && !in.live.heat;

  // A load starting ends any harvest.
  if ((coolOn || heatOn) && harvest_ != HarvestKind::None) {
    ControlEvent e;
    e.type = ControlEvent::Type::HarvestEnd;
    e.kind = harvest_;
    e.harvestEnd = lastHarvestEnd_ = HarvestEnd::LoadStarted;
    e.ms = lastHarvestMs_ = elapsedMs(in.nowMs, harvestStartMs_);
    push(e);
    harvest_ = HarvestKind::None;
  }

  if (coolOn || heatOn) {
    const Last now = coolOn ? Last::Cool : Last::Heat;
    if (last_ != Last::None && last_ != now) {
      crossovers_++;
      ControlEvent e;
      e.type = ControlEvent::Type::Crossover;
      e.a = coolOn ? 1.0f : 0.0f;
      e.count = crossovers_;
      push(e);
    }
    last_ = now;
  }

  // Ineffective checks: armed at load start from the chamber reading then (5.6).
  if (heatOn) { heatCheck_.armed = chamberOk && !in.scriptedDemand; heatCheck_.startMs = in.nowMs; heatCheck_.startC = c.chamberC; }
  if (coolOn) { coolCheck_.armed = chamberOk && !in.scriptedDemand; coolCheck_.startMs = in.nowMs; coolCheck_.startC = c.chamberC; }
  if (heatOff) heatCheck_.armed = false;   // ended inside the window: no result
  if (coolOff) coolCheck_.armed = false;

  // A load stopping starts a harvest.
  if ((coolOff || heatOff) && !in.live.cool && !in.live.heat) {
    harvest_ = coolOff ? HarvestKind::Cooling : HarvestKind::Heating;
    harvestStartMs_ = in.nowMs;
    gap0_ = (chamberOk && c.cabinetOk > 0) ? fabsf(c.chamberC - c.meanC) : -1.0f;
    harvestDuty_ = s_.tuning.fanCoolHarvestStartPct;
    ControlEvent e;
    e.type = ControlEvent::Type::HarvestStart;
    e.kind = harvest_;
    e.a = gap0_;
    push(e);
  }
  prev_ = in.live;
}

// Harvest end: chamber within the margin of the cabinet mean, or the backstop.
void TemperatureControl::harvest(const ControlInputs& in) {
  if (harvest_ == HarvestKind::None) return;
  const CabinetSummary& c = in.cab;
  const bool known = in.readingsFresh && c.chamberOk && c.cabinetOk > 0;
  if (known && gap0_ < 0) gap0_ = fabsf(c.chamberC - c.meanC);   // chamber came back mid-harvest
  HarvestEnd end = HarvestEnd::None;
  if (known && fabsf(c.chamberC - c.meanC) <= s_.tuning.harvestConvergenceC) end = HarvestEnd::Convergence;
  else if (elapsedMs(in.nowMs, harvestStartMs_) >= s_.tuning.harvestBackstopMs) end = HarvestEnd::Backstop;
  if (end == HarvestEnd::None) return;
  ControlEvent e;
  e.type = ControlEvent::Type::HarvestEnd;
  e.kind = harvest_;
  e.harvestEnd = lastHarvestEnd_ = end;
  e.ms = lastHarvestMs_ = elapsedMs(in.nowMs, harvestStartMs_);
  push(e);
  harvest_ = HarvestKind::None;
}

// Deadband with the run-end thresholds (4.5).
void TemperatureControl::deadband(const ControlInputs& in) {
  const CabinetSummary& c = in.cab;
  if (in.scriptedDemand || !in.readingsFresh || c.cabinetOk == 0) {
    coolCall_ = heatCall_ = false;    // nothing to act on: ask for nothing (the guards hold anyway)
    return;
  }
  const TuningSettings& t = s_.tuning;
  const float sp = s_.user.targetTempC;
  const float x = c.controlC;
  if (!coolCall_ && x > sp + t.coolingHysteresisC) coolCall_ = true;
  if (coolCall_ && x <= sp) coolCall_ = false;
  if (!heatCall_ && x < sp - t.heatingDeadbandC) heatCall_ = true;
  if (heatCall_ && x >= sp - t.heatingStopBelowSetpointC) heatCall_ = false;
}

void TemperatureControl::stopStrip(StripStop why, const CabinetSummary& c) {
  stripActive_ = false;
  persisting_ = false;                // the full persistence runs again from zero
  lastStripStop_ = why;
  ControlEvent e;
  e.type = ControlEvent::Type::StripStop;
  e.strip = why;
  e.a = c.meanRhPct;
  e.b = c.meanVpKpa;
  push(e);
}

// RH stripping (4.7): trigger on RH (sustained), release on vapour pressure.
void TemperatureControl::strip(const ControlInputs& in) {
  const CabinetSummary& c = in.cab;
  const TuningSettings& t = s_.tuning;
  const float floorC = s_.user.targetTempC - t.rhStripFloorBelowSetpointC;
  const bool sensorsOk = in.readingsFresh && c.cabinetOk >= s_.safety.minHealthyCabinet;
  const bool heating = heatCall_ || in.live.heat;

  if (stripActive_) {
    if (in.scriptedDemand)                     stopStrip(StripStop::Scripted, c);
    else if (!sensorsOk)                       stopStrip(StripStop::Sensors, c);
    else if (c.minC < floorC)                  stopStrip(StripStop::Floor, c);       // the floor wins
    else if (heating)                          stopStrip(StripStop::Heating, c);
    else if (!t.rhStripEnable)                 stopStrip(StripStop::Disabled, c);
    else if (c.meanVpKpa <= stripTargetVpKpa()) stopStrip(StripStop::VapourPressure, c);
    return;
  }

  // Persistence counts only while every condition holds: RH high, every cabinet
  // sensor above the floor, no heating, enough sensors. Any break restarts it.
  const bool eligible = !in.scriptedDemand && t.rhStripEnable && sensorsOk && !heating && c.minC >= floorC &&
                        c.meanRhPct >= s_.user.rhSetpointPct + t.rhStripOffsetPct;
  if (!eligible) { persisting_ = false; return; }
  if (!persisting_) { persisting_ = true; persistSinceMs_ = in.nowMs; }
  if (elapsedMs(in.nowMs, persistSinceMs_) < t.rhStripPersistMs) return;
  if (c.meanVpKpa <= stripTargetVpKpa()) return;   // nothing to remove (only at odd setpoint pairs)

  stripActive_ = true;
  persisting_ = false;
  stripCount_++;
  ControlEvent e;
  e.type = ControlEvent::Type::StripStart;
  e.a = c.meanRhPct;
  e.b = c.meanVpKpa;
  e.count = stripCount_;
  push(e);
}

// Ineffective checks (5.6) and the heater's retry hold.
void TemperatureControl::checks(const ControlInputs& in) {
  const TuningSettings& t = s_.tuning;
  const CabinetSummary& c = in.cab;
  const bool chamberOk = in.readingsFresh && c.chamberOk;

  if (in.scriptedDemand) { heatCheck_.armed = coolCheck_.armed = false; heaterHold_ = false; return; }
  if (heaterHold_ && elapsedMs(in.nowMs, heaterHoldSinceMs_) >= t.heaterIneffectiveRetryMs) heaterHold_ = false;

  if (heatCheck_.armed && in.live.heat) {
    const float rise = c.chamberC - heatCheck_.startC;
    if (chamberOk && rise >= t.heaterIneffectiveRiseC) {
      heatCheck_.armed = false;
      if (heaterFlag_) {
        heaterFlag_ = false;
        ControlEvent e;
        e.type = ControlEvent::Type::HeaterEffective;
        e.a = rise;
        push(e);
      }
    } else if (elapsedMs(in.nowMs, heatCheck_.startMs) >= t.heaterIneffectiveWindowMs) {
      heatCheck_.armed = false;
      heaterFlag_ = true;
      heaterHold_ = true;              // heater off; tried again after the retry time
      heaterHoldSinceMs_ = in.nowMs;
      ControlEvent e;
      e.type = ControlEvent::Type::HeaterIneffective;
      e.a = chamberOk ? rise : NAN;
      push(e);
    }
  }

  if (coolCheck_.armed && in.live.cool) {
    const float fall = coolCheck_.startC - c.chamberC;
    if (chamberOk && fall >= t.coolingIneffectiveFallC) {
      coolCheck_.armed = false;
      if (coolingFlag_) {
        coolingFlag_ = false;
        ControlEvent e;
        e.type = ControlEvent::Type::CoolingEffective;
        e.a = fall;
        push(e);
      }
    } else if (elapsedMs(in.nowMs, coolCheck_.startMs) >= t.coolingIneffectiveWindowMs) {
      coolCheck_.armed = false;
      coolingFlag_ = true;             // flag only: cooling keeps being called
      ControlEvent e;
      e.type = ControlEvent::Type::CoolingIneffective;
      e.a = chamberOk ? fall : NAN;
      push(e);
    }
  }
}

// Plenum fan duty (4.6).
uint8_t TemperatureControl::fan(const ControlInputs& in, const Demand& asked) {
  const TuningSettings& t = s_.tuning;
  if (in.live.cool) return t.fanCompressorPct;
  if (in.live.heat) return t.fanHeatingPct;
  if (harvest_ == HarvestKind::Heating) return t.fanHeatingPct;
  if (harvest_ == HarvestKind::Cooling) {
    const CabinetSummary& c = in.cab;
    const float m = t.harvestConvergenceC;
    if (gap0_ > m && in.readingsFresh && c.chamberOk && c.cabinetOk > 0) {
      float frac = (fabsf(c.chamberC - c.meanC) - m) / (gap0_ - m);
      if (frac < 0) frac = 0;
      if (frac > 1) frac = 1;
      const int span = (int)t.fanCoolHarvestStartPct - (int)t.fanCoolHarvestEndPct;
      int duty = (int)t.fanCoolHarvestEndPct + 5 * (int)ceilf(span * frac / 5.0f);   // 5 % steps
      if (duty > t.fanCoolHarvestStartPct) duty = t.fanCoolHarvestStartPct;
      if (duty < harvestDuty_) harvestDuty_ = (uint8_t)duty;                       // never rises
    }
    return harvestDuty_;   // gap unknown (chamber unhealthy): holds where it is
  }
  const uint8_t idle = idleOverride_ >= 0 ? (uint8_t)idleOverride_ : t.fanIdlePct;
  if (idle == 0) {                    // fan stopped at idle: start it before the load
    if (asked.cool) return t.fanCompressorPct;
    if (asked.heat) return t.fanHeatingPct;
  }
  return idle;
}

ControlOutput TemperatureControl::update(const ControlInputs& in) {
  edges(in);
  harvest(in);
  deadband(in);
  strip(in);
  checks(in);

  ControlOutput o;
  o.demand.cool = coolCall_ || stripActive_;
  o.demand.heat = heatCall_;
  o.strippingActive = stripActive_;
  o.heaterIneffectiveHold = heaterHold_;
  o.fanPct = fanPct_ = fan(in, o.demand);
  lastCab_ = in.cab;
  return o;
}

// ---------------------------------------------------------------- text
namespace {
struct Out {
  char* p;
  size_t n;
  size_t len = 0;
  Out(char* p_, size_t n_) : p(p_), n(n_) { if (n) p[0] = 0; }
  void add(const char* fmt, ...) {
    if (len + 1 >= n) return;
    va_list a;
    va_start(a, fmt);
    int w = vsnprintf(p + len, n - len, fmt, a);
    va_end(a);
    if (w > 0) len = (len + w < n) ? len + w : n - 1;
  }
};
const char* kindName(HarvestKind k) { return k == HarvestKind::Cooling ? "cooling" : "heating"; }
}  // namespace

int formatControlEvent(char* out, size_t n, const ControlEvent& e, const Settings& s) {
  Out o(out, n);
  const TuningSettings& t = s.tuning;
  const float target = vapourPressureKpa(s.user.targetTempC, s.user.rhSetpointPct);
  switch (e.type) {
    case ControlEvent::Type::None: break;
    case ControlEvent::Type::StripStart:
      o.add("RH STRIP START (%lu since boot): mean RH %.1f%% >= %.1f%% for %lu s; cooling called. "
            "Vapour pressure %.3f kPa, releases at %.3f",
            (unsigned long)e.count, e.a, s.user.rhSetpointPct + t.rhStripOffsetPct,
            (unsigned long)(t.rhStripPersistMs / 1000), e.b, target);
      break;
    case ControlEvent::Type::StripStop:
      o.add("RH STRIP STOP: %s. Mean RH %.1f%%, vapour pressure %.3f kPa (target %.3f)",
            stripStopName(e.strip), e.a, e.b, target);
      break;
    case ControlEvent::Type::HarvestStart:
      if (e.a >= 0) o.add("HARVEST START (%s): chamber %.2f C from the cabinet mean; ends within %.1f C",
                          kindName(e.kind), e.a, t.harvestConvergenceC);
      else          o.add("HARVEST START (%s): chamber sensor unhealthy - runs to the %lu s backstop",
                          kindName(e.kind), (unsigned long)(t.harvestBackstopMs / 1000));
      break;
    case ControlEvent::Type::HarvestEnd:
      o.add("HARVEST END (%s): %s after %lu.%lu s", kindName(e.kind), harvestEndName(e.harvestEnd),
            (unsigned long)(e.ms / 1000), (unsigned long)((e.ms % 1000) / 100));
      break;
    case ControlEvent::Type::Crossover:
      o.add("CROSSOVER %lu since boot: %s", (unsigned long)e.count,
            e.a > 0.5f ? "cooling started, heating ran last" : "heating started, cooling ran last");
      break;
    case ControlEvent::Type::HeaterIneffective:
      if (isnan(e.a)) o.add("HEATER INEFFECTIVE: chamber unreadable at the end of the %lu s window. ",
                            (unsigned long)(t.heaterIneffectiveWindowMs / 1000));
      else o.add("HEATER INEFFECTIVE: chamber rose %.2f C in %lu s (needs %.1f). ", e.a,
                 (unsigned long)(t.heaterIneffectiveWindowMs / 1000), t.heaterIneffectiveRiseC);
      o.add("Heater off, flagged; tried again in %lu s. Check the KSD9700 cut-out first",
            (unsigned long)(t.heaterIneffectiveRetryMs / 1000));
      break;
    case ControlEvent::Type::HeaterEffective:
      o.add("HEATER EFFECTIVE AGAIN: chamber rose %.2f C. Flag cleared", e.a);
      break;
    case ControlEvent::Type::CoolingIneffective:
      if (isnan(e.a)) o.add("COOLING INEFFECTIVE: chamber unreadable at the end of the %lu s window. ",
                            (unsigned long)(t.coolingIneffectiveWindowMs / 1000));
      else o.add("COOLING INEFFECTIVE: chamber fell %.2f C in %lu s (needs %.1f). ", e.a,
                 (unsigned long)(t.coolingIneffectiveWindowMs / 1000), t.coolingIneffectiveFallC);
      o.add("Flagged; cooling still called");
      break;
    case ControlEvent::Type::CoolingEffective:
      o.add("COOLING EFFECTIVE AGAIN: chamber fell %.2f C. Flag cleared", e.a);
      break;
  }
  return (int)o.len;
}

int formatControlText(char* out, size_t n, const TemperatureControl& tc, const CabinetSummary& c,
                      uint32_t nowMs) {
  Out o(out, n);
  if (c.cabinetOk > 0) o.add("ctl %.2fC %s", c.controlC, c.controlWarmest ? "WARMEST" : "mean");
  else                 o.add("ctl --");
  const char* mode = tc.stripping() ? "STRIP" : tc.coolCall() ? "cool-call" : tc.heatCall() ? "heat-call" : "idle";
  o.add(" %s", mode);
  if (c.cabinetOk > 0) o.add(" | vp %.3f/%.3fkPa", c.meanVpKpa, tc.stripTargetVpKpa());
  o.add(" | strips %lu", (unsigned long)tc.stripCount());
  if (tc.stripPersisting()) o.add(" persist %lus", (unsigned long)(tc.stripPersistMs(nowMs) / 1000));
  o.add(" | xover %lu", (unsigned long)tc.crossovers());
  if (tc.harvest() != HarvestKind::None)
    o.add(" | harvest %s %lus", kindName(tc.harvest()), (unsigned long)(tc.harvestForMs(nowMs) / 1000));
  if (tc.idleFanOverride() >= 0) o.add(" | idle fan override %d%%", tc.idleFanOverride());
  if (tc.heaterIneffective()) {
    o.add(" | HEATER INEFFECTIVE");
    if (tc.heaterHeld()) o.add(" (retry %lus)", (unsigned long)((tc.heaterRetryLeftMs(nowMs) + 999) / 1000));
  }
  if (tc.coolingIneffective()) o.add(" | COOLING INEFFECTIVE");
  return (int)o.len;
}

}  // namespace wineador
