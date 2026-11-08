#include "wineador/humid_control.h"
#include "wineador/psychro.h"
#include <stdarg.h>
#include <stdio.h>

namespace wineador {

void HumidControl::begin(const Settings& s, uint32_t nowMs) {
  (void)nowMs;
  s_ = s;
  call_ = persisting_ = false;
  calls_ = runs_ = 0;
  prevOn_ = false;
  armed_ = false;
  flag_ = hold_ = false;
  evHead_ = evCount_ = 0;
}

float HumidControl::riseNeededKpa() const {
  // 0.5 RH points at setpoint conditions, as vapour pressure (5.5)
  return saturationVpKpa(s_.user.targetTempC) * s_.tuning.humidIneffectiveRisePct / 100.0f;
}

uint32_t HumidControl::retryLeftMs(uint32_t nowMs) const {
  if (!hold_) return 0;
  const uint32_t had = elapsedMs(nowMs, holdSinceMs_);
  const uint32_t need = s_.tuning.humidIneffectiveRetryMs;
  return had >= need ? 0 : need - had;
}

void HumidControl::push(const HumidEvent& e) {
  if (evCount_ == kEvents) { evHead_ = (evHead_ + 1) % kEvents; evCount_--; }
  events_[(evHead_ + evCount_) % kEvents] = e;
  evCount_++;
}

bool HumidControl::takeEvent(HumidEvent& e) {
  if (evCount_ == 0) return false;
  e = events_[evHead_];
  evHead_ = (evHead_ + 1) % kEvents;
  evCount_--;
  return true;
}

HumidOutput HumidControl::update(const HumidInputs& in) {
  const TuningSettings& t = s_.tuning;
  const CabinetSummary& c = in.cab;
  const bool on = in.live.humid;
  const bool rising = on && !prevOn_, falling = !on && prevOn_;
  prevOn_ = on;
  if (rising) runs_++;

  if (in.scriptedDemand) {
    call_ = persisting_ = false;
    armed_ = false;
    hold_ = false;
    return HumidOutput{};
  }

  // ------------------------------------------------ call
  const bool readingsOk = in.readingsFresh && c.cabinetOk >= s_.safety.minHealthyCabinet;
  if (call_) {
    if (readingsOk && c.meanRhPct >= s_.user.rhSetpointPct) {
      call_ = false;
      HumidEvent e;
      e.type = HumidEvent::Type::CallEnd;
      e.rh = c.meanRhPct;
      push(e);
    }
  } else {
    const bool low = readingsOk && !in.stripping && c.meanRhPct <= triggerPct();
    if (!low) {
      persisting_ = false;
    } else {
      if (!persisting_) { persisting_ = true; persistSinceMs_ = in.nowMs; }
      if (elapsedMs(in.nowMs, persistSinceMs_) >= t.humidifyPersistMs) {
        call_ = true;
        persisting_ = false;
        calls_++;
        HumidEvent e;
        e.type = HumidEvent::Type::CallStart;
        e.rh = c.meanRhPct;
        e.count = calls_;
        push(e);
      }
    }
  }

  // ------------------------------------------------ ineffective check (vapour pressure)
  const bool vpOk = in.readingsFresh && c.cabinetOk > 0;
  if (hold_ && elapsedMs(in.nowMs, holdSinceMs_) >= t.humidIneffectiveRetryMs) hold_ = false;
  if (rising) { armed_ = vpOk; armedAtMs_ = in.nowMs; startVp_ = c.meanVpKpa; }
  if (falling) armed_ = false;          // ended inside the window: no result
  if (armed_ && on) {
    const float change = c.meanVpKpa - startVp_;
    if (vpOk && change >= riseNeededKpa()) {
      armed_ = false;
      if (flag_) {
        flag_ = false;
        HumidEvent e;
        e.type = HumidEvent::Type::Effective;
        e.rh = c.meanRhPct;
        e.vpChange = change;
        push(e);
      }
    } else if (elapsedMs(in.nowMs, armedAtMs_) >= t.humidIneffectiveWindowMs) {
      armed_ = false;
      flag_ = true;
      hold_ = true;
      holdSinceMs_ = in.nowMs;
      HumidEvent e;
      e.type = HumidEvent::Type::Ineffective;
      e.rh = c.meanRhPct;
      e.vpChange = change;
      push(e);
    }
  }

  HumidOutput o;
  o.demand = call_;
  o.ineffectiveHold = hold_;
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
}  // namespace

int formatHumidEvent(char* out, size_t n, const HumidEvent& e, const Settings& s) {
  Out o(out, n);
  const TuningSettings& t = s.tuning;
  const float need = saturationVpKpa(s.user.targetTempC) * t.humidIneffectiveRisePct / 100.0f;
  switch (e.type) {
    case HumidEvent::Type::None: break;
    case HumidEvent::Type::CallStart:
      o.add("HUMIDIFY CALL START (%lu since boot): mean RH %.1f%% <= %.1f%% for %lu s; humidifier asked for until %.0f%%",
            (unsigned long)e.count, e.rh, s.user.rhSetpointPct - t.humidifyBandPct,
            (unsigned long)(t.humidifyPersistMs / 1000), s.user.rhSetpointPct);
      break;
    case HumidEvent::Type::CallEnd:
      o.add("HUMIDIFY CALL END: mean RH %.1f%% reached the %.0f%% setpoint", e.rh, s.user.rhSetpointPct);
      break;
    case HumidEvent::Type::Ineffective:
      o.add("HUMIDIFIER INEFFECTIVE: vapour pressure changed %+.4f kPa in %lu s (needs +%.4f). Humidifier off, flagged; "
            "tried again in %lu s. Check the tank, wick and fan",
            e.vpChange, (unsigned long)(t.humidIneffectiveWindowMs / 1000), need,
            (unsigned long)(t.humidIneffectiveRetryMs / 1000));
      break;
    case HumidEvent::Type::Effective:
      o.add("HUMIDIFIER EFFECTIVE AGAIN: vapour pressure rose %+.4f kPa. Flag cleared", e.vpChange);
      break;
  }
  return (int)o.len;
}

int formatHumidText(char* out, size_t n, const HumidControl& hc, uint32_t nowMs) {
  Out o(out, n);
  o.add("humid %s", hc.call() ? "CALL" : "idle");
  if (hc.persisting()) o.add(" persist %lus", (unsigned long)(hc.persistMs(nowMs) / 1000));
  o.add(" | calls %lu runs %lu", (unsigned long)hc.calls(), (unsigned long)hc.runs());
  if (hc.ineffective()) {
    o.add(" | HUMID INEFFECTIVE");
    if (hc.held()) o.add(" (retry %lus)", (unsigned long)((hc.retryLeftMs(nowMs) + 999) / 1000));
  }
  return (int)o.len;
}

}  // namespace wineador
