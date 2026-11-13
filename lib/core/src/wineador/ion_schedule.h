#pragma once
// The ion generator's runs (spec 4.6c, Rev Q). Plain logic: no hardware calls. The loop gives it
// the uptime, the clock and what is blocking each pass, and drives GPIO17 with on().
//
// Schedule. N = runs per day (0-4), M = minutes per run (0-5); N or M 0, or the enable off: no runs.
//   - Time trusted: fixed UTC slots 24/N hours apart from 00:00 UTC (2 a day: 00:00 and 12:00).
//     A run is due when the clock passes a slot's start while the board is running: a slot missed
//     while the board was off is not caught up, and each slot runs once (a clock stepped back
//     never repeats one; a clock stepped forward past a slot skips it).
//   - Time not trusted: every 24/N hours from boot, the first 24/N hours after boot. Once the clock
//     is trusted, the next run follows the clock.
//   - Bench profile (B3 table): every 30 minutes for 1 minute instead, whatever N and M (still none
//     with N or M 0); the cap unchanged, so the bench exercises it.
//   - The run's length is timed by uptime, so a clock correction never stretches it.
// Not during the boot hold, an OTA update or a Fault (5.5): a run due then is SKIPPED (logged), not
// moved; one running then is cut short, not resumed, and its on-time counts towards the cap.
// Switched off mid-run (or N or M set to 0): off at once, logged; switching back on waits for the
// next slot. A settings change mid-run leaves that run's length as it started.
//
// The cap (a safety value): 10 minutes of actual on-time in any 24 hours, never exceeded whatever
// the settings - a backstop behind the settings' own limit (runs x minutes at most 10). A run starts
// only with some of the 10 minutes left, and lasts M or what is left, whichever is shorter.
// It is KEPT ACROSS REBOOTS, and never by the clock: the loop saves the record in NVS at each run's
// start and end, each run with the uptime known to have passed since it started (added up over
// every boot it was saved in; the record is also saved once an hour while it holds a run, so each
// boot adds its uptime, less at most an hour). A stored run counts whole until that known age reaches 24 hours;
// the time the board was off only makes it older, so a wrong clock can't age a run away and
// repeated reboots can't keep one forever. A reboot during a run counts its whole planned length
// (the record saved at its start says so).

#include <stdint.h>
#include <stddef.h>
#include "wineador/settings.h"

namespace wineador {

enum class IonBlock : uint8_t { None, BootHold, Updating, Fault };
enum class IonState : uint8_t {
  Disabled,   // the enable is off
  NoRuns,     // runs per day or minutes per run is 0
  Waiting,    // the next run is due at nextRunUtcS() (trusted) or in nextRunInMs() (from boot)
  Running,    // on, leftMs() to go
  Held,       // blocked now (boot hold, update, fault): a run due now would be skipped
  NotYet,     // the cap record isn't restored yet (the first moments after boot)
};
enum class IonWhy : uint8_t { None, BootHold, Updating, Fault, Cap, SwitchedOff, RecordUnsaved };

struct IonInputs {
  uint64_t upMs = 0;            // uptime, ms (never wraps)
  bool clockTrusted = false;    // 5.5's trusted time
  int64_t utcS = 0;             // the clock, meaningful when trusted
  IonBlock block = IonBlock::None;
  bool enabled = true;
  uint8_t runsPerDay = 2;
  uint8_t minutesPerRun = 2;
};

class IonSchedule {
 public:
  static constexpr int kMaxRecords = 16;
  static constexpr size_t kBlobBytes = 1 + 1 + 8 + kMaxRecords * 17;   // version, count, last slot, records (17 B each)

  enum class Event : uint8_t { None, Started, Ended, CutShort, Skipped, SwitchedOff, RecordFailed, RecordSaved };
  struct Change {
    Event e = Event::None;
    IonWhy why = IonWhy::None;
    uint32_t ms = 0;             // Started: the run's length; Ended / CutShort / SwitchedOff: its on-time
    bool shortened = false;      // Started: shorter than the setting, by the cap
    bool fromClock = false;      // Started / Skipped: a UTC slot (slotUtcS) or from boot
    int64_t slotUtcS = 0;
  };

  void begin(const SafetySettings& f);

  // Once, early in the boot hold: the saved record (n = 0 if none or unreadable). Until then no run
  // starts.
  void restore(const uint8_t* blob, size_t n);
  bool restored() const { return restored_; }

  // Every pass.
  void update(const IonInputs& in);

  bool on() const { return running_; }
  IonState state() const { return state_; }
  uint32_t leftMs() const { return leftMs_; }                   // Running
  bool nextFromClock() const { return nextFromClock_; }
  int64_t nextRunUtcS() const { return nextUtcS_; }             // Waiting / Held, from the clock
  uint64_t nextRunInMs() const { return nextInMs_; }            // Waiting / Held, from boot
  IonWhy heldWhy() const { return heldWhy_; }
  // The last run due that was skipped, and why (for "last run skipped: boot hold").
  IonWhy lastSkip() const { return lastSkip_; }
  int64_t lastSkipUtcS() const { return lastSkipUtcS_; }        // 0 if it wasn't from the clock

  // The cap: actual on-time in the last 24 hours (stored runs count whole until they are certainly
  // 24 hours old), and what is left of the 10 minutes.
  uint32_t usedMs(uint64_t upMs) const;
  uint32_t capLeftMs(uint64_t upMs) const;

  bool take(Change* c);

  // The record to save in NVS (the loop writes it when saveWanted() is true; a few ms): at each
  // run's start and end, and once an hour while it holds any run.
  bool saveWanted() const { return saveWanted_; }
  size_t saveBlob(uint8_t* out, size_t n, uint64_t upMs);   // clears saveWanted
  // What the save did (4.6c). A run due is switched on only once its start record has been saved
  // (the loop saves, then calls this, in the same pass); if that save fails the run never starts.
  // While saves fail, NO NEW RUN STARTS (a run due is skipped, logged; the state is Held, "cap
  // record can't be saved") and the save is tried again every minute; the first that works lets
  // the runs resume. A run already going carries on to its end.
  void saveResult(bool ok, uint64_t upMs);
  bool saveFailed() const { return saveFailed_; }

  int recordCount() const { return nRec_; }

 private:
  struct Record {
    int64_t startUpMs = 0;      // uptime (negative: before this boot, by the uptime known to have passed)
    uint32_t plannedMs = 0;
    uint32_t onMs = 0;          // closed records: the actual on-time
    bool open = false;          // running now (this boot), or (restored) running when the board reset
    bool stored = false;        // restored at boot: counts whole until it is certainly 24 h old
  };
  void push(const Change& c);
  void prune(uint64_t upMs);
  uint32_t recordOn(const Record& r, uint64_t upMs) const;
  void startRun(const IonInputs& in, uint32_t runMs, bool fromClock, int64_t slotUtcS);
  void stopRun(const IonInputs& in, Event e, IonWhy why);

  SafetySettings f_;
  bool restored_ = false;
  Record rec_[kMaxRecords];
  int nRec_ = 0;
  int64_t lastSlotUtcS_ = -1;   // the newest slot start that has run or been skipped (each slot once)

  // the schedule
  bool havePrev_ = false;
  bool prevTrusted_ = false;
  int64_t prevIdx_ = 0;
  int64_t prevUtcS_ = 0;
  uint64_t prevPeriodMs_ = 0;
  uint64_t nextDueUpMs_ = 0;    // from boot
  uint8_t prevRuns_ = 0, prevMinutes_ = 0;
  bool prevEnabled_ = false;

  // the run
  bool running_ = false;
  int runRec_ = -1;
  uint64_t runStartUpMs_ = 0;
  uint32_t runMs_ = 0;

  // shown
  IonState state_ = IonState::NotYet;
  uint32_t leftMs_ = 0;
  bool nextFromClock_ = false;
  int64_t nextUtcS_ = 0;
  uint64_t nextInMs_ = 0;
  IonWhy heldWhy_ = IonWhy::None;
  IonWhy lastSkip_ = IonWhy::None;
  int64_t lastSkipUtcS_ = 0;

  bool saveWanted_ = false;
  bool saveFailed_ = false;
  bool startPending_ = false;   // a run due, waiting for its start record to be saved
  int pendingRec_ = -1;
  Change pending_;
  uint64_t retrySaveAtMs_ = 0;
  uint64_t lastSaveUpMs_ = 0;
  static constexpr int kQ = 8;
  Change q_[kQ];
  int qHead_ = 0, qLen_ = 0;
};

// The run period and length for these settings (0, 0 when there are no runs).
void ionRunTiming(const SafetySettings& f, uint8_t runsPerDay, uint8_t minutesPerRun, uint64_t* periodMs, uint32_t* runMs);

const char* ionStateKey(IonState s);   // "off", "no_runs", "waiting", "running", "held", "starting" (docs/api.md)
const char* ionWhyText(IonWhy w);      // "boot hold", "update", "fault", "at most 10 minutes in 24 hours", "switched off"

// The event text (spec 5.4; type "ion"), from event_text.h's words:
//   "ION RUN START: 2 min (slot 12:00 UTC)"     "ION RUN START: 1 min 30 s (12 h after boot; shortened by ...)"
//   "ION RUN END: on 2 min 0 s"                 "ION RUN CUT SHORT: fault; on 0 min 45 s"
//   "ION RUN SKIPPED: boot hold (slot 00:00 UTC)"   "ION RUN STOPPED: switched off; on 1 min 10 s"
//   "ION CAP RECORD NOT SAVED: ..."                   "ION CAP RECORD SAVED again: runs resume"
int formatIonEvent(char* out, size_t n, const IonSchedule::Change& c);

// One line for the status line: "ion off, next 12:00 UTC", "ion ON 1:20 left", "ion switched off",
// "ion no runs set", "ion held (boot hold)", "ion next in 11:58 (from boot)".
int formatIonState(char* out, size_t n, const IonSchedule& s);

}  // namespace wineador
