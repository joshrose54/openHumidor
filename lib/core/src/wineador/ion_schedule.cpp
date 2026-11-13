#include "wineador/ion_schedule.h"
#include "wineador/event_text.h"
#include <stdio.h>
#include <string.h>

namespace wineador {

static constexpr uint64_t kDayMs = 24ULL * 3600ULL * 1000ULL;
static constexpr uint32_t kMinRunMs = 1000;   // a run shorter than this (the cap all but used) is skipped
static constexpr uint8_t kBlobVersion = 2;
// A slot's start reached by the clock running: at most this far since the last pass (a pass can be
// seconds long only during an OTA flash, when nothing runs anyway). Further is a clock step.
static constexpr int64_t kStepS = 60;

void ionRunTiming(const SafetySettings& f, uint8_t runs, uint8_t mins, uint64_t* periodMs, uint32_t* runMs) {
  *periodMs = 0;
  *runMs = 0;
  if (runs == 0 || mins == 0) return;
  if (f.ionBenchPeriodMs) {             // bench profile (B3 table)
    *periodMs = f.ionBenchPeriodMs;
    *runMs = f.ionBenchRunMs;
    return;
  }
  *periodMs = kDayMs / runs;             // 24/N hours: 86 400 000 divides by 1-4 exactly
  *runMs = (uint32_t)mins * 60000UL;
}

void IonSchedule::begin(const SafetySettings& f) {
  *this = IonSchedule();
  f_ = f;
}

void IonSchedule::push(const Change& c) {
  if (qLen_ == kQ) { qHead_ = (qHead_ + 1) % kQ; qLen_--; }
  q_[(qHead_ + qLen_) % kQ] = c;
  qLen_++;
}

bool IonSchedule::take(Change* c) {
  if (qLen_ == 0) return false;
  *c = q_[qHead_];
  qHead_ = (qHead_ + 1) % kQ;
  qLen_--;
  return true;
}

// ---------------------------------------------------------------- the record (the cap)
uint32_t IonSchedule::recordOn(const Record& r, uint64_t upMs) const {
  if (!r.open) return r.onMs;
  if (running_ && runRec_ >= 0 && &r == &rec_[runRec_]) {
    const uint64_t e = upMs - runStartUpMs_;
    return e > r.plannedMs ? r.plannedMs : (uint32_t)e;
  }
  return r.plannedMs;   // restored open: the board reset during it, so its whole planned length
}

// This boot's runs count by their overlap with the last 24 hours of uptime. A stored run (restored
// at boot) counts its whole on-time until it is CERTAINLY 24 hours old: it is placed at minus the
// uptime known to have passed since it started (added up over every boot it was saved in), and the
// time the board was off only makes it older. The clock is never used, so a wrong clock can't age
// a run away (4.6c).
uint32_t IonSchedule::usedMs(uint64_t upMs) const {
  const int64_t now = (int64_t)upMs;
  const int64_t from = now - (int64_t)f_.ionCapWindowMs;
  uint64_t sum = 0;
  for (int i = 0; i < nRec_; i++) {
    const Record& r = rec_[i];
    const uint32_t on = recordOn(r, upMs);
    if (r.stored) {
      if (r.startUpMs > from) sum += on;
      continue;
    }
    const int64_t a = r.startUpMs, b = r.startUpMs + (int64_t)on;
    const int64_t lo = a > from ? a : from, hi = b < now ? b : now;
    if (hi > lo) sum += (uint64_t)(hi - lo);
  }
  return sum > 0xFFFFFFFFULL ? 0xFFFFFFFFUL : (uint32_t)sum;
}

uint32_t IonSchedule::capLeftMs(uint64_t upMs) const {
  const uint32_t u = usedMs(upMs);
  return u >= f_.ionCapMs ? 0 : f_.ionCapMs - u;
}

// Drops the records that no longer count (wholly older than the window).
void IonSchedule::prune(uint64_t upMs) {
  const int64_t from = (int64_t)upMs - (int64_t)f_.ionCapWindowMs;
  int k = 0;
  for (int i = 0; i < nRec_; i++) {
    const Record& r = rec_[i];
    bool keep;
    if (r.open) keep = true;
    else if (r.stored) keep = r.startUpMs > from;
    else keep = r.startUpMs + (int64_t)r.onMs > from;
    if (keep) {
      if (running_ && i == runRec_) runRec_ = k;
      rec_[k++] = r;
    }
  }
  nRec_ = k;
}

static void put64(uint8_t* p, int64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)((uint64_t)v >> (8 * i)); }
static void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static int64_t get64(const uint8_t* p) { uint64_t v = 0; for (int i = 7; i >= 0; i--) v = (v << 8) | p[i]; return (int64_t)v; }
static uint32_t get32(const uint8_t* p) { uint32_t v = 0; for (int i = 3; i >= 0; i--) v = (v << 8) | p[i]; return v; }

// Each record: the uptime known to have passed since the run started (ms), its planned length, its
// on-time, and whether it was still running (a reset during it counts its whole planned length).
size_t IonSchedule::saveBlob(uint8_t* out, size_t n, uint64_t upMs) {
  saveWanted_ = false;
  lastSaveUpMs_ = upMs;
  if (n < kBlobBytes) return 0;
  memset(out, 0, kBlobBytes);
  out[0] = kBlobVersion;
  out[1] = (uint8_t)nRec_;
  put64(out + 2, lastSlotUtcS_);
  for (int i = 0; i < nRec_; i++) {
    const Record& r = rec_[i];
    uint8_t* p = out + 10 + i * 17;
    const int64_t knownAgeMs = (int64_t)upMs - r.startUpMs;
    put64(p, knownAgeMs < 0 ? 0 : knownAgeMs);
    put32(p + 8, r.plannedMs);
    put32(p + 12, recordOn(r, upMs));
    p[16] = (uint8_t)(r.open ? 1 : 0);
  }
  return kBlobBytes;
}

void IonSchedule::saveResult(bool ok, uint64_t upMs) {
  if (!ok) {
    if (!saveFailed_) { Change c; c.e = Event::RecordFailed; push(c); }
    saveFailed_ = true;
    retrySaveAtMs_ = upMs + 60000;   // tried again every minute
  } else if (saveFailed_) {
    saveFailed_ = false;
    Change c; c.e = Event::RecordSaved; push(c);
  }
  if (startPending_) {               // a run waiting for its start record to be saved
    startPending_ = false;
    if (ok) {
      runRec_ = pendingRec_;
      rec_[runRec_].startUpMs = (int64_t)upMs;
      running_ = true;
      runStartUpMs_ = upMs;
      push(pending_);
    } else {                         // not saved: the run never starts, and leaves no record
      for (int i = pendingRec_; i + 1 < nRec_; i++) rec_[i] = rec_[i + 1];
      nRec_--;
      lastSkip_ = IonWhy::RecordUnsaved;
      lastSkipUtcS_ = pending_.fromClock ? pending_.slotUtcS : 0;
      Change c; c.e = Event::Skipped; c.why = IonWhy::RecordUnsaved; c.fromClock = pending_.fromClock; c.slotUtcS = pending_.slotUtcS;
      push(c);
    }
    pendingRec_ = -1;
  }
}

void IonSchedule::restore(const uint8_t* blob, size_t n) {
  restored_ = true;
  nRec_ = 0;
  if (!blob || n < 10 || blob[0] != kBlobVersion) return;
  const int count = blob[1] <= kMaxRecords ? blob[1] : kMaxRecords;
  if (n < (size_t)(10 + count * 17)) return;
  lastSlotUtcS_ = get64(blob + 2);
  for (int i = 0; i < count; i++) {
    const uint8_t* p = blob + 10 + i * 17;
    Record r;
    const int64_t knownAgeMs = get64(p);
    r.plannedMs = get32(p + 8);
    const bool open = (p[16] & 1) != 0;
    r.onMs = open ? r.plannedMs : get32(p + 12);   // a reset during a run: its whole planned length
    r.open = false;
    r.stored = true;
    if (r.onMs == 0 || knownAgeMs < 0 || knownAgeMs >= (int64_t)f_.ionCapWindowMs) continue;   // certainly 24 h old
    r.startUpMs = -knownAgeMs;                     // this boot's uptime 0 is at least that long after it
    if (nRec_ < kMaxRecords) rec_[nRec_++] = r;
  }
}

// ---------------------------------------------------------------- the runs
void IonSchedule::startRun(const IonInputs& in, uint32_t runMs, bool fromClock, int64_t slotUtcS) {
  prune(in.upMs);
  const uint32_t left = capLeftMs(in.upMs);
  if (left < kMinRunMs || nRec_ >= kMaxRecords) {   // no room in the record counts as the cap: never under-count
    lastSkip_ = IonWhy::Cap;
    lastSkipUtcS_ = fromClock ? slotUtcS : 0;
    Change c; c.e = Event::Skipped; c.why = IonWhy::Cap; c.fromClock = fromClock; c.slotUtcS = slotUtcS; push(c);
    return;
  }
  const uint32_t len = runMs < left ? runMs : left;
  // The record of this run is saved FIRST, with its planned length (a reset mid-run then counts it
  // whole); the generator switches on only once that save has worked (saveResult). A failed save
  // means the run never starts - so no run can go unrecorded, even after a reboot (4.6c).
  Record r;
  r.startUpMs = (int64_t)in.upMs;
  r.plannedMs = len;
  r.open = true;
  pendingRec_ = nRec_;
  rec_[nRec_++] = r;
  startPending_ = true;
  runMs_ = len;
  saveWanted_ = true;
  pending_ = Change();
  pending_.e = Event::Started;
  pending_.ms = len;
  pending_.shortened = len < runMs;
  pending_.fromClock = fromClock;
  pending_.slotUtcS = slotUtcS;
}

void IonSchedule::stopRun(const IonInputs& in, Event e, IonWhy why) {
  if (!running_) return;
  Record& r = rec_[runRec_];
  r.onMs = recordOn(r, in.upMs);
  r.open = false;
  running_ = false;
  runRec_ = -1;
  saveWanted_ = true;
  Change c; c.e = e; c.why = why; c.ms = r.onMs; push(c);
}

static IonWhy whyOf(IonBlock b) {
  switch (b) {
    case IonBlock::BootHold: return IonWhy::BootHold;
    case IonBlock::Updating: return IonWhy::Updating;
    case IonBlock::Fault: return IonWhy::Fault;
    default: return IonWhy::None;
  }
}

void IonSchedule::update(const IonInputs& in) {
  uint64_t period = 0;
  uint32_t runMs = 0;
  ionRunTiming(f_, in.runsPerDay, in.minutesPerRun, &period, &runMs);
  const bool active = in.enabled && period > 0;

  // 1. A run in progress: does it go on?
  if (running_) {
    if (!active) stopRun(in, Event::SwitchedOff, IonWhy::SwitchedOff);
    else if (in.block != IonBlock::None) stopRun(in, Event::CutShort, whyOf(in.block));
    else if (in.upMs - runStartUpMs_ >= runMs_) stopRun(in, Event::Ended, IonWhy::None);
    else if (usedMs(in.upMs) > f_.ionCapMs) stopRun(in, Event::CutShort, IonWhy::Cap);   // the backstop
  }

  // 2. Is a run due? (The schedule is followed whether or not anything is allowed to run, so that
  //    switching on, or a block ending, waits for the next slot.)
  const bool settingsChanged = !havePrev_ || in.runsPerDay != prevRuns_ || in.minutesPerRun != prevMinutes_ ||
                               period != prevPeriodMs_;
  bool due = false;
  bool dueFromClock = false;
  int64_t slotUtc = 0;
  if (period > 0) {
    if (in.clockTrusted) {
      const int64_t idx = (in.utcS * 1000) / (int64_t)period;
      if (!settingsChanged && prevTrusted_ && idx == prevIdx_ + 1) {
        slotUtc = idx * (int64_t)period / 1000;
        // Reached by running, not by a clock stepped forward over the slot's start: the clock moved
        // on by at most kStepS since the last pass.
        const bool ran = in.utcS - prevUtcS_ >= 0 && in.utcS - prevUtcS_ <= kStepS;
        if (ran && slotUtc > lastSlotUtcS_) { due = true; dueFromClock = true; }   // each slot once
      }
      // (a backward step, a forward jump past a slot, or just now trusted: nothing due)
      prevIdx_ = idx;
      prevUtcS_ = in.utcS;
      nextFromClock_ = true;
      nextUtcS_ = (idx + 1) * (int64_t)period / 1000;
    } else {
      if (settingsChanged || prevTrusted_ || nextDueUpMs_ == 0) {
        nextDueUpMs_ = (in.upMs / period + 1) * period;   // the next multiple of 24/N hours from boot
      } else if (in.upMs >= nextDueUpMs_) {
        due = true;
        while (nextDueUpMs_ <= in.upMs) nextDueUpMs_ += period;
      }
      nextFromClock_ = false;
      nextInMs_ = nextDueUpMs_ - in.upMs;
    }
  }
  prevTrusted_ = in.clockTrusted;
  prevRuns_ = in.runsPerDay;
  prevMinutes_ = in.minutesPerRun;
  prevPeriodMs_ = period;
  prevEnabled_ = in.enabled;
  havePrev_ = true;

  if (due && active && !running_ && !startPending_) {
    if (dueFromClock) lastSlotUtcS_ = slotUtc;
    if (!restored_) {
      // the record isn't known yet (only in the first moments after boot, inside the boot hold)
      lastSkip_ = IonWhy::BootHold;
      lastSkipUtcS_ = dueFromClock ? slotUtc : 0;
      Change c; c.e = Event::Skipped; c.why = IonWhy::BootHold; c.fromClock = dueFromClock; c.slotUtcS = slotUtc; push(c);
    } else if (in.block == IonBlock::None && saveFailed_) {
      // 4.6c: no new run starts while the cap record can't be saved
      lastSkip_ = IonWhy::RecordUnsaved;
      lastSkipUtcS_ = dueFromClock ? slotUtc : 0;
      Change c; c.e = Event::Skipped; c.why = IonWhy::RecordUnsaved; c.fromClock = dueFromClock; c.slotUtcS = slotUtc; push(c);
    } else if (in.block != IonBlock::None) {
      lastSkip_ = whyOf(in.block);
      lastSkipUtcS_ = dueFromClock ? slotUtc : 0;
      Change c; c.e = Event::Skipped; c.why = whyOf(in.block); c.fromClock = dueFromClock; c.slotUtcS = slotUtc; push(c);
    } else {
      startRun(in, runMs, dueFromClock, slotUtc);
    }
  }

  // 3. Once an hour while the record holds a run: saved again, so the uptime this boot has run adds
  //    to each stored run's known age (repeated reboots can't keep a run counting forever).
  if (saveFailed_ && in.upMs >= retrySaveAtMs_) saveWanted_ = true;   // the failed save, again
  if (restored_ && in.upMs - lastSaveUpMs_ >= 3600000ULL) {
    const int before = nRec_;
    prune(in.upMs);
    if (nRec_ > 0 || before > 0) saveWanted_ = true;
    else lastSaveUpMs_ = in.upMs;
  }

  // 4. What to show.
  heldWhy_ = in.block != IonBlock::None ? whyOf(in.block) : saveFailed_ ? IonWhy::RecordUnsaved : IonWhy::None;
  if (!in.enabled) state_ = IonState::Disabled;
  else if (period == 0) state_ = IonState::NoRuns;
  else if (running_) state_ = IonState::Running;
  else if (!restored_) state_ = IonState::NotYet;
  else if (in.block != IonBlock::None || saveFailed_) state_ = IonState::Held;
  else state_ = IonState::Waiting;
  leftMs_ = running_ ? (uint32_t)(runMs_ - (in.upMs - runStartUpMs_)) : 0;
}

// ---------------------------------------------------------------- text
const char* ionStateKey(IonState s) {
  switch (s) {
    case IonState::Disabled: return "off";
    case IonState::NoRuns: return "no_runs";
    case IonState::Waiting: return "waiting";
    case IonState::Running: return "running";
    case IonState::Held: return "held";
    default: return "starting";
  }
}

const char* ionWhyText(IonWhy w) {
  switch (w) {
    case IonWhy::BootHold: return "boot hold";
    case IonWhy::Updating: return "update";
    case IonWhy::Fault: return "fault";
    case IonWhy::Cap: return "at most 10 minutes in 24 hours";
    case IonWhy::SwitchedOff: return "switched off";
    case IonWhy::RecordUnsaved: return "cap record can't be saved";
    default: return "";
  }
}

static void dur(char* out, size_t n, uint32_t ms) {
  const uint32_t s = (ms + 500) / 1000;
  if (s % 60 == 0) snprintf(out, n, "%lu min", (unsigned long)(s / 60));
  else if (s < 60) snprintf(out, n, "%lu s", (unsigned long)s);
  else snprintf(out, n, "%lu min %lu s", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

static void onFor(char* out, size_t n, uint32_t ms) {
  const uint32_t s = (ms + 500) / 1000;
  snprintf(out, n, "%lu min %lu s", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

static void slotText(char* out, size_t n, const IonSchedule::Change& c) {
  if (c.fromClock) {
    const int64_t d = ((c.slotUtcS % 86400) + 86400) % 86400;
    snprintf(out, n, "slot %02d:%02d UTC", (int)(d / 3600), (int)(d / 60 % 60));
  } else {
    snprintf(out, n, "from boot, time not trusted");
  }
}

int formatIonEvent(char* out, size_t n, const IonSchedule::Change& c) {
  char a[24], b[40];
  switch (c.e) {
    case IonSchedule::Event::Started:
      dur(a, sizeof(a), c.ms);
      slotText(b, sizeof(b), c);
      return snprintf(out, n, "%s: %s (%s)%s", kEvIonStart, a, b, c.shortened ? "; shortened: at most 10 minutes in 24 hours" : "");
    case IonSchedule::Event::Ended:
      onFor(a, sizeof(a), c.ms);
      return snprintf(out, n, "%s: on %s", kEvIonEnd, a);
    case IonSchedule::Event::CutShort:
      onFor(a, sizeof(a), c.ms);
      return snprintf(out, n, "%s: %s; on %s", kEvIonCutShort, ionWhyText(c.why), a);
    case IonSchedule::Event::SwitchedOff:
      onFor(a, sizeof(a), c.ms);
      return snprintf(out, n, "%s: switched off; on %s", kEvIonStopped, a);
    case IonSchedule::Event::RecordFailed:
      return snprintf(out, n, "%s: no new run starts until it can be saved (tried again every minute)", kEvIonRecordFailed);
    case IonSchedule::Event::RecordSaved:
      return snprintf(out, n, "%s: runs resume", kEvIonRecordSaved);
    case IonSchedule::Event::Skipped:
      slotText(b, sizeof(b), c);
      return snprintf(out, n, "%s: %s (%s)", kEvIonSkipped, ionWhyText(c.why), b);
    default:
      if (n) out[0] = 0;
      return 0;
  }
}

int formatIonState(char* out, size_t n, const IonSchedule& s) {
  switch (s.state()) {
    case IonState::Disabled: return snprintf(out, n, "ion switched off");
    case IonState::NoRuns: return snprintf(out, n, "ion no runs set");
    case IonState::NotYet: return snprintf(out, n, "ion starting");
    case IonState::Running: {
      const uint32_t t = (s.leftMs() + 999) / 1000;
      return snprintf(out, n, "ion ON %lu:%02lu left", (unsigned long)(t / 60), (unsigned long)(t % 60));
    }
    case IonState::Held: return snprintf(out, n, "ion held (%s)", ionWhyText(s.heldWhy()));
    default:
      if (s.nextFromClock()) {
        const int64_t d = ((s.nextRunUtcS() % 86400) + 86400) % 86400;
        return snprintf(out, n, "ion off, next %02d:%02d UTC", (int)(d / 3600), (int)(d / 60 % 60));
      } else {
        const uint64_t m = (s.nextRunInMs() + 59999) / 60000;
        return snprintf(out, n, "ion off, next in %lu:%02lu (from boot)", (unsigned long)(m / 60), (unsigned long)(m % 60));
      }
  }
}

}  // namespace wineador
