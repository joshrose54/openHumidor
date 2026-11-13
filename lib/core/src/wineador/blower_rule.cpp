#include "wineador/blower_rule.h"
#include "wineador/event_text.h"
#include "wineador/types.h"
#include <stdio.h>

namespace wineador {

void BlowerRule::begin(const TuningSettings& t) {
  t_ = t;
  mode_ = BlowerMode::Full;
  why_ = BlowerWhy::Boot;
  pct_ = 100;
  holding_ = false;
  holdSinceMs_ = 0;
  spreadC_ = spreadRh_ = 0;
  qHead_ = qLen_ = 0;
}

void BlowerRule::push(const Change& c) {
  if (qLen_ == kQ) { qHead_ = (qHead_ + 1) % kQ; qLen_--; }   // keep the newest
  q_[(qHead_ + qLen_) % kQ] = c;
  qLen_++;
}

bool BlowerRule::take(Change* c) {
  if (qLen_ == 0) return false;
  *c = q_[qHead_];
  qHead_ = (qHead_ + 1) % kQ;
  qLen_--;
  return true;
}

void BlowerRule::enter(BlowerMode m, BlowerWhy w) {
  mode_ = m;
  why_ = w;
  holding_ = false;
  holdSinceMs_ = 0;
}

uint32_t BlowerRule::holdLeftMs(uint32_t nowMs) const {
  if (mode_ != BlowerMode::Boost || !holding_) return 0;
  const uint32_t e = elapsedMs(nowMs, holdSinceMs_);
  return e >= t_.blowerIdleHoldMs ? 0 : t_.blowerIdleHoldMs - e;
}

uint8_t BlowerRule::update(const BlowerInputs& in) {
  const uint32_t now = in.nowMs;
  // 1-3: the overrides.
  if (in.updating) {
    if (mode_ == BlowerMode::Full) { Change c; c.e = Event::FullEnd; c.why = why_; push(c); }
    if (mode_ != BlowerMode::Updating) enter(BlowerMode::Updating, BlowerWhy::None);
  } else if (in.fault || !in.fresh) {
    const BlowerWhy w = in.fault ? BlowerWhy::Fault : (mode_ == BlowerMode::Full && why_ == BlowerWhy::Boot) ? BlowerWhy::Boot
                                                                                                             : BlowerWhy::Stale;
    if (mode_ != BlowerMode::Full || why_ != w) {
      // A change of reason is one end and one start, so each spell's reason is in the log. Boot's
      // own spell is not logged as a start (every boot has it: the boot event says so).
      if (mode_ == BlowerMode::Full && why_ != BlowerWhy::Boot) { Change c; c.e = Event::FullEnd; c.why = why_; push(c); }
      enter(BlowerMode::Full, w);
      Change c; c.e = Event::FullStart; c.why = w; push(c);
    }
  } else if (mode_ == BlowerMode::Full || mode_ == BlowerMode::Updating) {
    // 4.6b: after any spell at full speed (or an update) the rule starts in boost.
    if (mode_ == BlowerMode::Full && why_ != BlowerWhy::Boot) { Change c; c.e = Event::FullEnd; c.why = why_; push(c); }
    Change c;
    c.e = Event::Boost;
    c.after = mode_ == BlowerMode::Updating ? BlowerWhy::Updating : why_;   // what it starts after
    enter(BlowerMode::Boost, BlowerWhy::None);
    push(c);
  }

  // 4: the spread, on each finished sweep.
  if ((mode_ == BlowerMode::Idle || mode_ == BlowerMode::Boost) && in.sweepDone && in.cabinetOk >= 2) {
    spreadC_ = in.spreadC;
    spreadRh_ = in.spreadRhPct;
    const bool wide = in.spreadC > t_.blowerBoostAboveC || in.spreadRhPct > t_.blowerBoostAboveRhPct;
    const bool low = in.spreadC < t_.blowerIdleBelowC && in.spreadRhPct < t_.blowerIdleBelowRhPct;
    if (wide) {
      if (mode_ == BlowerMode::Idle) {
        Change c; c.e = Event::Boost; c.spreadC = in.spreadC; c.spreadRhPct = in.spreadRhPct; push(c);
      }
      enter(BlowerMode::Boost, BlowerWhy::None);
    } else if (mode_ == BlowerMode::Boost) {
      if (low) {
        if (!holding_) { holding_ = true; holdSinceMs_ = now; }
      } else {
        holding_ = false;   // a sweep not below both: the 10 minutes start again
      }
    }
  }
  if (mode_ == BlowerMode::Boost && holding_ && elapsedMs(now, holdSinceMs_) >= t_.blowerIdleHoldMs) {
    Change c; c.e = Event::Idle; c.spreadC = spreadC_; c.spreadRhPct = spreadRh_; c.heldMs = elapsedMs(now, holdSinceMs_);
    push(c);
    enter(BlowerMode::Idle, BlowerWhy::None);
  }

  pct_ = (mode_ == BlowerMode::Idle || mode_ == BlowerMode::Updating) ? t_.blowerIdlePct : 100;
  return pct_;
}

const char* blowerModeKey(BlowerMode m) {
  switch (m) {
    case BlowerMode::Idle: return "idle";
    case BlowerMode::Boost: return "boost";
    case BlowerMode::Full: return "full";
    default: return "updating";
  }
}

const char* blowerWhyText(BlowerWhy w) {
  switch (w) {
    case BlowerWhy::Boot: return "no reading yet since boot";
    case BlowerWhy::Fault: return "fault";
    case BlowerWhy::Stale: return "no fresh reading";
    default: return "";
  }
}

static void minsText(char* out, size_t n, uint32_t ms) {
  if (ms % 60000 == 0) snprintf(out, n, "%lu min", (unsigned long)(ms / 60000));
  else snprintf(out, n, "%lu s", (unsigned long)(ms / 1000));
}

int formatBlowerEvent(char* out, size_t n, const BlowerRule::Change& c, const TuningSettings& t) {
  char hold[16];
  minsText(hold, sizeof(hold), t.blowerIdleHoldMs);
  switch (c.e) {
    case BlowerRule::Event::Boost:
      if (c.after != BlowerWhy::None) {
        const char* a = c.after == BlowerWhy::Boot ? "after boot" : c.after == BlowerWhy::Updating ? "after the update"
                      : c.after == BlowerWhy::Fault ? "after full speed (fault)" : "after full speed (no fresh reading)";
        return snprintf(out, n, "%s: %s; idle after %s of low spread", kEvBlowersBoost, a, hold);
      }
      return snprintf(out, n, "%s: spread %.2f C, %.1f points (above %.1f C or %.0f points)", kEvBlowersBoost,
                      (double)c.spreadC, (double)c.spreadRhPct, (double)t.blowerBoostAboveC, (double)t.blowerBoostAboveRhPct);
    case BlowerRule::Event::Idle:
      return snprintf(out, n, "%s: spread below %.1f C and %.0f points for %s; %u%%", kEvBlowersIdle,
                      (double)t.blowerIdleBelowC, (double)t.blowerIdleBelowRhPct, hold, (unsigned)t.blowerIdlePct);
    case BlowerRule::Event::FullStart:
      return snprintf(out, n, "%s %s", kEvBlowersFull, blowerWhyText(c.why));
    case BlowerRule::Event::FullEnd:
      return snprintf(out, n, "%s (%s)", kEvBlowersFullEnd, blowerWhyText(c.why));
    default:
      if (n) out[0] = 0;
      return 0;
  }
}

int formatBlowerState(char* out, size_t n, const BlowerRule& b, uint32_t nowMs) {
  switch (b.mode()) {
    case BlowerMode::Idle: return snprintf(out, n, "%u%% idle", (unsigned)b.pct());
    case BlowerMode::Updating: return snprintf(out, n, "%u%% idle (updating)", (unsigned)b.pct());
    case BlowerMode::Full: return snprintf(out, n, "%u%% FULL (%s)", (unsigned)b.pct(), blowerWhyText(b.why()));
    default: {
      if (!b.holding()) return snprintf(out, n, "%u%% boost", (unsigned)b.pct());
      const uint32_t s = (b.holdLeftMs(nowMs) + 999) / 1000;
      return snprintf(out, n, "%u%% boost (idle in %lu:%02lu)", (unsigned)b.pct(), (unsigned long)(s / 60), (unsigned long)(s % 60));
    }
  }
}

}  // namespace wineador
