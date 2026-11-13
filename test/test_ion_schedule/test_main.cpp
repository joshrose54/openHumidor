// The ion generator's runs (spec 4.6c, Rev Q): UTC slots from 00:00, from boot when the time isn't
// trusted, skipped (not moved) in the boot hold / an update / a Fault, cut short and not resumed,
// switched off mid-run, the settings' own limit (runs x minutes at most 10), and the 10-minute cap
// in any 24 hours kept across reboots - with a simulated month of random reboots, clock jumps and
// settings changes that checks no 24 hours ever has more than 10 minutes on.
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include "wineador/ion_schedule.h"
#include "wineador/settings.h"
#include "wineador/web_api.h"
#include "wineador/event_text.h"
#include "wineador/event_sentence.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

static constexpr int64_t kDayS = 86400;
static constexpr int64_t kT0 = 1794441600;   // 2026-11-12 00:00:00 UTC (a midnight)

// A board running the rule, one pass per second of "true" time.
struct Board {
  SafetySettings f = settingsFor(Profile::Real).safety;
  IonSchedule ion;
  uint64_t up = 0;              // this boot's uptime, ms
  int64_t trueMs = 0;           // since kT0
  bool trusted = true;
  int64_t clockErrS = 0;        // the board's clock minus the true time (while trusted)
  uint32_t bootHoldMs = 300000;
  IonInputs in;
  std::vector<uint8_t> nvs;     // the saved record
  int starts = 0, ends = 0, cuts = 0, skips = 0, stops = 0;
  IonWhy lastSkipWhy = IonWhy::None, lastCutWhy = IonWhy::None;
  bool restoreAtBoot = true;
  bool nvsBroken = false;        // every NVS write fails
  int saves = 0, recordFails = 0, recordSaves = 0;

  explicit Board(bool bench = false, int64_t startS = 0) {
    if (bench) f = settingsFor(Profile::Bench).safety;
    trueMs = startS * 1000;
    ion.begin(f);
    in.enabled = true;
    in.runsPerDay = 2;
    in.minutesPerRun = 2;
  }
  int64_t utc() const { return kT0 + trueMs / 1000 + clockErrS; }
  void restoreNow() { ion.restore(nvs.empty() ? nullptr : nvs.data(), nvs.size()); }
  void step(uint32_t ms = 1000, IonBlock extra = IonBlock::None) {
    up += ms;
    trueMs += ms;
    if (!ion.restored() && restoreAtBoot && up >= 2000) restoreNow();   // after the first sweep
    in.upMs = up;
    in.clockTrusted = trusted;
    in.utcS = utc();
    in.block = up < bootHoldMs ? IonBlock::BootHold : extra;
    ion.update(in);
    if (ion.saveWanted()) {
      std::vector<uint8_t> blob(IonSchedule::kBlobBytes, 0);
      ion.saveBlob(blob.data(), blob.size(), up);
      saves++;
      if (!nvsBroken) nvs = blob;          // a broken NVS keeps what it had
      ion.saveResult(!nvsBroken, up);
    }
    IonSchedule::Change c;
    while (ion.take(&c)) {
      switch (c.e) {
        case IonSchedule::Event::Started: starts++; break;
        case IonSchedule::Event::Ended: ends++; break;
        case IonSchedule::Event::CutShort: cuts++; lastCutWhy = c.why; break;
        case IonSchedule::Event::Skipped: skips++; lastSkipWhy = c.why; break;
        case IonSchedule::Event::SwitchedOff: stops++; break;
        case IonSchedule::Event::RecordFailed: recordFails++; break;
        case IonSchedule::Event::RecordSaved: recordSaves++; break;
        default: break;
      }
    }
  }
  void run(int64_t seconds, IonBlock extra = IonBlock::None) { for (int64_t i = 0; i < seconds; i++) step(1000, extra); }
  void runUntilUtc(int64_t utcS) { while (utc() < utcS) step(); }
  void reboot(uint32_t offMs = 5000) {
    trueMs += offMs;
    up = 0;
    ion.begin(f);
  }
};

// ---------------------------------------------------------------- the settings
void test_the_settings_and_their_limit() {
  const UserSettings u;
  TEST_ASSERT_TRUE(u.ionEnable);
  TEST_ASSERT_EQUAL_UINT8(2, u.ionRunsPerDay);
  TEST_ASSERT_EQUAL_UINT8(2, u.ionMinutesPerRun);
  TEST_ASSERT_TRUE(ionRunsOk(2, 5));
  TEST_ASSERT_TRUE(ionRunsOk(4, 2));
  TEST_ASSERT_TRUE(ionRunsOk(0, 5));
  TEST_ASSERT_TRUE(ionRunsOk(1, 5));
  TEST_ASSERT_FALSE(ionRunsOk(3, 4));    // 12
  TEST_ASSERT_FALSE(ionRunsOk(4, 3));
  TEST_ASSERT_FALSE(ionRunsOk(5, 1));    // runs over 4
  TEST_ASSERT_FALSE(ionRunsOk(1, 6));    // minutes over 5
  const SafetySettings f = settingsFor(Profile::Real).safety;
  TEST_ASSERT_EQUAL_UINT32(600000, f.ionCapMs);
  TEST_ASSERT_EQUAL_UINT32(86400000, f.ionCapWindowMs);
  const SafetySettings b = settingsFor(Profile::Bench).safety;
  TEST_ASSERT_EQUAL_UINT32(600000, b.ionCapMs);              // the cap unchanged on the bench
  TEST_ASSERT_EQUAL_UINT32(1800000, b.ionBenchPeriodMs);     // every 30 min...
  TEST_ASSERT_EQUAL_UINT32(60000, b.ionBenchRunMs);          // ...for 1 min
  Settings s = settingsFor(Profile::Real);
  TEST_ASSERT_NULL(validateSettings(s));
  s.user.ionRunsPerDay = 4;
  s.user.ionMinutesPerRun = 3;
  TEST_ASSERT_NOT_NULL(validateSettings(s));
  // a change to either is a change (the save limit's "nothing changed" test)
  UserSettings a, c;
  c.ionMinutesPerRun = 3;
  TEST_ASSERT_FALSE(sameUserSettings(a, c));
  c = a;
  c.ionRunsPerDay = 1;
  TEST_ASSERT_FALSE(sameUserSettings(a, c));
}

void test_the_api_refuses_more_than_10_minutes_a_day() {
  UserSettings now, out;
  const char* b = "{\"ion_runs_per_day\":4,\"ion_minutes_per_run\":3}";
  ApiResult r = checkSettingsWrite(b, strlen(b), now, false, &out);
  TEST_ASSERT_TRUE(r.refusal == Refusal::IonTooMuch);
  TEST_ASSERT_EQUAL_INT(400, httpStatus(r.refusal));
  TEST_ASSERT_EQUAL_STRING("ion_too_much", refusalCode(r.refusal));
  TEST_ASSERT_NOT_NULL(strstr(r.message, "at most 10 minutes a day in total"));
  // one field alone, judged with the other as it is now: 2 runs, so 6 min each is out of bounds,
  // 5 is fine; then 4 runs with 5 min each is too much
  b = "{\"ion_minutes_per_run\":5}";
  r = checkSettingsWrite(b, strlen(b), now, false, &out);
  TEST_ASSERT_TRUE(r.ok());
  TEST_ASSERT_EQUAL_UINT8(5, out.ionMinutesPerRun);
  b = "{\"ion_runs_per_day\":4}";
  r = checkSettingsWrite(b, strlen(b), out, false, &out);
  TEST_ASSERT_TRUE(r.refusal == Refusal::IonTooMuch);
  b = "{\"ion_minutes_per_run\":6}";
  r = checkSettingsWrite(b, strlen(b), now, false, &out);
  TEST_ASSERT_TRUE(r.refusal == Refusal::OutOfBounds);
  b = "{\"ion_runs_per_day\":1.5}";
  r = checkSettingsWrite(b, strlen(b), now, false, &out);
  TEST_ASSERT_TRUE(r.refusal == Refusal::OffStep);
  b = "{\"ion_runs_per_day\":\"2\"}";
  r = checkSettingsWrite(b, strlen(b), now, false, &out);
  TEST_ASSERT_TRUE(r.refusal == Refusal::WrongType);
  // the change event names both
  UserSettings x = now;
  x.ionRunsPerDay = 1;
  x.ionMinutesPerRun = 5;
  char d[160];
  describeUserChange(d, sizeof(d), now, x);
  TEST_ASSERT_EQUAL_STRING("ion_runs_per_day 2 -> 1, ion_minutes_per_run 2 -> 5", d);
}

// ---------------------------------------------------------------- the schedule
void test_utc_slots_from_midnight_for_each_number_of_runs() {
  for (int n = 1; n <= 4; n++) {
    Board b(false, 3600);           // boots at 01:00 UTC, trusted
    b.in.runsPerDay = (uint8_t)n;
    b.in.minutesPerRun = 2;
    b.run(2 * kDayS);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2 * n, b.starts, "runs in two days");
    TEST_ASSERT_EQUAL_INT(2 * n, b.ends);
  }
  // 2 a day: at 00:00 and 12:00 UTC, for 2 min each
  Board b(false, 3600);
  b.runUntilUtc(kT0 + 12 * 3600 - 1);
  TEST_ASSERT_FALSE(b.ion.on());
  TEST_ASSERT_EQUAL_INT64(kT0 + 12 * 3600, b.ion.nextRunUtcS());
  char t[64];
  formatIonState(t, sizeof(t), b.ion);
  TEST_ASSERT_EQUAL_STRING("ion off, next 12:00 UTC", t);
  b.step();
  TEST_ASSERT_TRUE(b.ion.on());
  b.run(60);
  formatIonState(t, sizeof(t), b.ion);
  TEST_ASSERT_EQUAL_STRING("ion ON 1:00 left", t);
  b.run(59);
  TEST_ASSERT_TRUE(b.ion.on());
  b.step();
  TEST_ASSERT_FALSE(b.ion.on());   // 120 s on
  TEST_ASSERT_EQUAL_INT64(kT0 + kDayS, b.ion.nextRunUtcS());
  // 3 a day: 00:00, 08:00, 16:00
  Board c(false, 3600);
  c.in.runsPerDay = 3;
  c.in.minutesPerRun = 3;
  c.step();
  TEST_ASSERT_EQUAL_INT64(kT0 + 8 * 3600, c.ion.nextRunUtcS());
}

void test_a_slot_missed_while_off_is_not_caught_up() {
  Board b(false, 11 * 3600 + 3500);   // boots at 11:58:20: the boot hold runs past 12:00
  b.run(600);
  TEST_ASSERT_EQUAL_INT(0, b.starts);
  TEST_ASSERT_EQUAL_INT(1, b.skips);   // 12:00 fell in the boot hold: skipped, logged
  TEST_ASSERT_TRUE(b.lastSkipWhy == IonWhy::BootHold);
  b.run(11 * 3600);
  TEST_ASSERT_EQUAL_INT(0, b.starts);  // not moved: the next run is at 00:00
  b.runUntilUtc(kT0 + kDayS + 10);
  TEST_ASSERT_EQUAL_INT(1, b.starts);
  // booting after a slot (12:01, the board was off at 12:00): no run until the next slot, no event
  Board c(false, 12 * 3600 + 60);
  c.run(11 * 3600);
  TEST_ASSERT_EQUAL_INT(0, c.starts);
  TEST_ASSERT_EQUAL_INT(0, c.skips);
}

void test_from_boot_when_the_time_is_not_trusted_then_the_clock() {
  Board b(false, 5 * 3600);
  b.trusted = false;
  b.clockErrS = -400 * kDayS;          // the clock says 1970-something: never used
  b.run(12 * 3600 - 1);                // the first run is 12 h after boot, not at boot
  TEST_ASSERT_EQUAL_INT(0, b.starts);
  char t[64];
  formatIonState(t, sizeof(t), b.ion);
  TEST_ASSERT_EQUAL_STRING("ion off, next in 0:01 (from boot)", t);
  b.step();
  TEST_ASSERT_EQUAL_INT(1, b.starts);
  b.run(12 * 3600);
  TEST_ASSERT_EQUAL_INT(2, b.starts);  // every 12 h from boot
  // NTP: trusted now (17:00 UTC + 24 h = 2 days after boot at 05:00... the true time is 05:00 + 24 h = 05:00)
  b.clockErrS = 0;
  b.trusted = true;
  b.step();
  TEST_ASSERT_TRUE(b.ion.nextFromClock());
  TEST_ASSERT_EQUAL_INT64(kT0 + kDayS + 12 * 3600, b.ion.nextRunUtcS());   // follows the clock: 12:00
  const int s0 = b.starts;
  b.runUntilUtc(kT0 + kDayS + 12 * 3600 + 5);
  TEST_ASSERT_EQUAL_INT(s0 + 1, b.starts);
}

void test_a_clock_stepped_back_never_repeats_a_slot_and_forward_skips() {
  Board b(false, 11 * 3600);
  b.runUntilUtc(kT0 + 12 * 3600 + 200);
  TEST_ASSERT_EQUAL_INT(1, b.starts);
  b.clockErrS -= 300;                  // back 5 min: 12:00 is passed again
  b.run(600);
  TEST_ASSERT_EQUAL_INT(1, b.starts);  // each slot once
  b.clockErrS += 300;
  b.runUntilUtc(kT0 + 23 * 3600);
  b.clockErrS += 2 * 3600;             // forward 2 h, past 00:00
  b.run(600);
  TEST_ASSERT_EQUAL_INT(1, b.starts);  // skipped by the jump
  b.runUntilUtc(kT0 + kDayS + 12 * 3600 + 10);
  TEST_ASSERT_EQUAL_INT(2, b.starts);
}

void test_the_runs_length_is_timed_by_uptime() {
  Board b(false, 11 * 3600);
  b.runUntilUtc(kT0 + 12 * 3600 + 10);
  TEST_ASSERT_TRUE(b.ion.on());
  b.clockErrS += 3600;                 // a clock correction mid-run
  b.run(109);
  TEST_ASSERT_TRUE(b.ion.on());
  b.step();
  TEST_ASSERT_FALSE(b.ion.on());
  TEST_ASSERT_EQUAL_INT(1, b.ends);
}

// ---------------------------------------------------------------- blocks
void test_skipped_in_an_update_or_a_fault_and_cut_short_not_resumed() {
  Board b(false, 11 * 3600);
  b.runUntilUtc(kT0 + 12 * 3600 - 2);
  b.run(5, IonBlock::Updating);        // due during an update
  TEST_ASSERT_EQUAL_INT(1, b.skips);
  TEST_ASSERT_TRUE(b.lastSkipWhy == IonWhy::Updating);
  TEST_ASSERT_TRUE(b.ion.lastSkip() == IonWhy::Updating);
  TEST_ASSERT_EQUAL_INT64(kT0 + 12 * 3600, b.ion.lastSkipUtcS());
  b.run(600);
  TEST_ASSERT_EQUAL_INT(0, b.starts);  // not moved
  TEST_ASSERT_TRUE(b.ion.state() == IonState::Waiting);
  b.run(5, IonBlock::Fault);
  TEST_ASSERT_TRUE(b.ion.state() == IonState::Held);
  // a fault mid-run: off at once, the rest dropped, its on-time counted
  b.runUntilUtc(kT0 + kDayS + 30);
  TEST_ASSERT_TRUE(b.ion.on());
  b.step(1000, IonBlock::Fault);
  TEST_ASSERT_FALSE(b.ion.on());
  TEST_ASSERT_EQUAL_INT(1, b.cuts);
  TEST_ASSERT_TRUE(b.lastCutWhy == IonWhy::Fault);
  TEST_ASSERT_UINT32_WITHIN(1000, 31000, b.ion.usedMs(b.up));
  b.run(600);
  TEST_ASSERT_FALSE(b.ion.on());       // not resumed
  TEST_ASSERT_EQUAL_INT(1, b.starts);
}

void test_switched_off_mid_run_and_back_on_waits_for_the_next_slot() {
  Board b(false, 11 * 3600);
  b.runUntilUtc(kT0 + 12 * 3600 + 30);
  TEST_ASSERT_TRUE(b.ion.on());
  b.in.enabled = false;
  b.step();
  TEST_ASSERT_FALSE(b.ion.on());
  TEST_ASSERT_EQUAL_INT(1, b.stops);
  TEST_ASSERT_TRUE(b.ion.state() == IonState::Disabled);
  b.in.enabled = true;
  b.run(3600);
  TEST_ASSERT_FALSE(b.ion.on());
  TEST_ASSERT_EQUAL_INT(1, b.starts);
  b.runUntilUtc(kT0 + kDayS + 5);
  TEST_ASSERT_EQUAL_INT(2, b.starts);
  // runs set to 0 mid-run: off at once
  b.in.runsPerDay = 0;
  b.step();
  TEST_ASSERT_FALSE(b.ion.on());
  TEST_ASSERT_EQUAL_INT(2, b.stops);
  TEST_ASSERT_TRUE(b.ion.state() == IonState::NoRuns);
  // switched off: the settings keep their values (they are the loop's, untouched here)
}

void test_a_settings_change_mid_run_leaves_its_length() {
  Board b(false, 11 * 3600);
  b.in.minutesPerRun = 2;
  b.runUntilUtc(kT0 + 12 * 3600 + 30);
  b.in.minutesPerRun = 5;              // longer: this run stays 2 min
  b.run(89);
  TEST_ASSERT_TRUE(b.ion.on());
  b.step();
  TEST_ASSERT_FALSE(b.ion.on());
  b.in.minutesPerRun = 1;
  b.runUntilUtc(kT0 + kDayS + 30);
  b.in.minutesPerRun = 5;
  b.in.runsPerDay = 1;                 // a change of runs mid-run: the run goes on as it started
  b.run(29);
  TEST_ASSERT_TRUE(b.ion.on());
  b.step();
  TEST_ASSERT_FALSE(b.ion.on());
}

// ---------------------------------------------------------------- the cap
void test_the_cap_with_settings_over_10_minutes() {
  // 4 x 5 = 20 min a day is refused as a setting; the cap still stops it if it ever got through.
  Board b(false, 3600);
  b.in.runsPerDay = 4;
  b.in.minutesPerRun = 5;
  b.run(2 * kDayS);
  // day 1: 06:00, 12:00 (10 min), 18:00 skipped; then every slot finds the cap just used 24 h ago
  TEST_ASSERT_TRUE(b.skips > 0);
  TEST_ASSERT_TRUE(b.lastSkipWhy == IonWhy::Cap);
  TEST_ASSERT_TRUE(b.ion.usedMs(b.up) <= 600000);
  char e[120];
  IonSchedule::Change c;
  c.e = IonSchedule::Event::Skipped;
  c.why = IonWhy::Cap;
  c.fromClock = true;
  c.slotUtcS = kT0 + 18 * 3600;
  formatIonEvent(e, sizeof(e), c);
  TEST_ASSERT_EQUAL_STRING("ION RUN SKIPPED: at most 10 minutes in 24 hours (slot 18:00 UTC)", e);
}

void test_a_run_is_shortened_to_what_is_left() {
  Board b(false, 3600);
  b.in.runsPerDay = 3;
  b.in.minutesPerRun = 4;             // 12 a day (only possible if the setting's limit were missed)
  b.runUntilUtc(kT0 + 16 * 3600 + 5);
  // 08:00 4 min, 16:00 4 min: 8 used
  b.runUntilUtc(kT0 + kDayS + 10);     // 00:00: 2 min left of the 10
  TEST_ASSERT_TRUE(b.ion.on());
  TEST_ASSERT_UINT32_WITHIN(1000, 110000, b.ion.leftMs());
}

void test_the_cap_survives_a_reboot_with_a_trusted_clock() {
  Board b(false, 3600);
  b.in.runsPerDay = 4;
  b.in.minutesPerRun = 5;
  b.runUntilUtc(kT0 + 12 * 3600 + 400);   // 06:00 and 12:00: 10 min used
  TEST_ASSERT_EQUAL_UINT32(600000, b.ion.usedMs(b.up));
  b.reboot();
  b.run(400);
  TEST_ASSERT_UINT32_WITHIN(1000, 600000, b.ion.usedMs(b.up));   // restored by age
  b.runUntilUtc(kT0 + 18 * 3600 + 30);
  TEST_ASSERT_FALSE(b.ion.on());           // 18:00: still capped after the reboot
  TEST_ASSERT_TRUE(b.lastSkipWhy == IonWhy::Cap);
}

void test_the_cap_with_the_time_not_trusted_counts_every_stored_run() {
  Board b(false, 3600);
  b.in.runsPerDay = 2;
  b.in.minutesPerRun = 5;
  b.runUntilUtc(kT0 + 12 * 3600 + 400);   // 12:00, 5 min
  b.reboot();
  b.trusted = false;
  b.run(400);
  TEST_ASSERT_UINT32_WITHIN(1000, 300000, b.ion.usedMs(b.up));
  b.run(25 * 3600);                         // after 24 h of uptime it no longer counts
  TEST_ASSERT_TRUE(b.ion.usedMs(b.up) <= 600000);
}

void test_a_reboot_during_a_run_counts_its_whole_planned_length() {
  Board b(false, 11 * 3600);
  b.in.minutesPerRun = 5;
  b.runUntilUtc(kT0 + 12 * 3600 + 20);    // 20 s into a 5 min run
  TEST_ASSERT_TRUE(b.ion.on());
  b.reboot(1000);
  b.run(10);
  TEST_ASSERT_UINT32_WITHIN(1000, 300000, b.ion.usedMs(b.up));
  // and untrusted: the same
  Board c(false, 11 * 3600);
  c.in.minutesPerRun = 5;
  c.runUntilUtc(kT0 + 12 * 3600 + 20);
  c.reboot(1000);
  c.trusted = false;
  c.run(10);
  TEST_ASSERT_EQUAL_UINT32(300000, c.ion.usedMs(c.up));
}

void test_no_run_before_the_record_is_restored() {
  Board b(false, 11 * 3600);
  b.restoreAtBoot = false;
  b.bootHoldMs = 0;
  b.runUntilUtc(kT0 + 12 * 3600 + 5);
  TEST_ASSERT_EQUAL_INT(0, b.starts);
  TEST_ASSERT_TRUE(b.ion.state() == IonState::NotYet);
}

void test_the_bench_runs_every_30_minutes_for_1_minute_until_the_cap() {
  Board b(true, 3600);                     // bench profile, trusted
  b.bootHoldMs = 30000;
  b.run(6 * 3600);                         // 01:00 - 07:00: slots every 30 min
  TEST_ASSERT_EQUAL_INT(10, b.starts);      // 10 runs of 1 min: the cap
  TEST_ASSERT_TRUE(b.skips >= 1);
  TEST_ASSERT_TRUE(b.lastSkipWhy == IonWhy::Cap);
  TEST_ASSERT_EQUAL_UINT32(600000, b.ion.usedMs(b.up));
  // no runs set: none on the bench either
  Board c(true, 3600);
  c.in.minutesPerRun = 0;
  c.run(3600);
  TEST_ASSERT_EQUAL_INT(0, c.starts);
}

// ---------------------------------------------------------------- a month
// Random reboots (with power off for a while, some during runs), clock jumps (NTP corrections of
// a trusted clock, up to a few minutes; untrusted spells with any clock, then trusted again),
// random blocks (updates, faults), the enable flipped, and settings changed - including settings
// over 10 minutes a day, which the dashboard refuses, so that the cap itself is exercised. Every
// second, the true on-time in the last 24 hours must be at most 10 minutes.
static void month(bool bench, unsigned seed, bool bigJumps = false) {
  srand(seed);
  Board b(bench, 3600);
  b.bootHoldMs = bench ? 30000 : 300000;
  std::vector<uint8_t> onSec(86400, 0);
  int64_t inWindow = 0, worst = 0, total = 0;
  int64_t sec = 0;
  int reboots = 0, jumps = 0, changes = 0;
  IonBlock block = IonBlock::None;
  int64_t blockLeft = 0;
  const int64_t days = 31;
  for (int64_t s = 0; s < days * kDayS; s++) {
    const int r = rand() % 100000;
    if (r < 3) {                                          // a reboot, ~3 a day... and more during runs
      b.reboot(1000 + (uint32_t)(rand() % 600000));
      // the board was off: the seconds pass with it off
      reboots++;
    } else if (r < 5 && b.ion.on()) {
      b.reboot(1000 + (uint32_t)(rand() % 5000));
      reboots++;
    } else if (r < 7) {                                  // an NTP correction while trusted
      if (b.trusted) {
        if (bigJumps && rand() % 3 == 0) b.clockErrS += (int64_t)(rand() % (6 * 86400)) - 3 * 86400;   // up to 3 days either way
        else b.clockErrS += (rand() % 361) - 180;
      }
      jumps++;
    } else if (r < 8) {                                  // trust lost (RTC lost time), clock anywhere
      b.trusted = false;
      b.clockErrS = (int64_t)(rand() % 2000000) - 1000000;
      jumps++;
    } else if (r < 10 && !b.trusted) {                   // NTP: trusted again, close to true
      b.trusted = true;
      b.clockErrS = (rand() % 11) - 5;
      jumps++;
    } else if (r < 14) {                                 // a settings change, sometimes over 10 a day
      b.in.runsPerDay = (uint8_t)(rand() % 5);
      b.in.minutesPerRun = (uint8_t)(rand() % 6);
      changes++;
    } else if (r < 15) {
      b.in.enabled = !b.in.enabled;
      changes++;
    } else if (r < 18 && blockLeft == 0) {
      block = (rand() % 2) ? IonBlock::Updating : IonBlock::Fault;
      blockLeft = 30 + rand() % 900;
    }
    if (blockLeft > 0 && --blockLeft == 0) block = IonBlock::None;
    // the true time moved on with a reboot: account the off seconds as off
    while (sec * 1000 < b.trueMs) {
      const size_t i = (size_t)(sec % 86400);
      inWindow -= onSec[i];
      onSec[i] = 0;
      sec++;
    }
    b.step(1000, block);
    const size_t i = (size_t)(sec % 86400);
    inWindow -= onSec[i];
    onSec[i] = b.ion.on() ? 1 : 0;
    inWindow += onSec[i];
    total += onSec[i];
    sec++;
    if (inWindow > worst) worst = inWindow;
    if (inWindow > 600) {
      char m[120];
      snprintf(m, sizeof(m), "seed %u: %lld s on in 24 h at day %.2f", seed, (long long)inWindow, (double)sec / 86400.0);
      TEST_FAIL_MESSAGE(m);
    }
    s = sec - 1;
  }
  printf("  month (%s%s, seed %u): %d reboots, %d clock changes, %d settings changes; %lld s on in all; "
         "worst 24 h %lld s; %d runs, %d skipped, %d cut short\n",
         bench ? "bench" : "real", bigJumps ? ", big trusted jumps" : "", seed, reboots, jumps, changes, (long long)total, (long long)worst, b.starts, b.skips, b.cuts);
  TEST_ASSERT_TRUE(worst <= 600);
  TEST_ASSERT_TRUE(b.starts + b.skips > 0);
  TEST_ASSERT_TRUE(reboots > 20);
}

void test_a_simulated_month_never_exceeds_the_cap() {
  for (unsigned seed = 1; seed <= 6; seed++) month(false, seed);
}

void test_a_run_whose_start_record_cant_be_saved_never_starts() {
  // The gap a run-then-save order would leave: with the NVS failing from boot, each boot would let
  // one unrecorded run through (the board only learns at its first save). The start record is
  // saved first, and the generator switches on only once that has worked.
  Board b(false, 11 * 3600);
  b.nvsBroken = true;
  int64_t onSeconds = 0;
  for (int boot = 0; boot < 6; boot++) {      // six boots, each through a slot
    for (int i = 0; i < 13 * 3600; i++) { b.step(); if (b.ion.on()) onSeconds++; }
    b.reboot(1000);
  }
  TEST_ASSERT_EQUAL_INT64_MESSAGE(0, onSeconds, "a run went ahead without its record saved");
  TEST_ASSERT_EQUAL_INT(0, b.starts);
  TEST_ASSERT_TRUE(b.lastSkipWhy == IonWhy::RecordUnsaved);
  TEST_ASSERT_EQUAL_INT(0, b.ion.recordCount());   // the run that never started leaves no record
  // the NVS works again: the next slot runs, and is on the record before it switches on
  b.nvsBroken = false;
  b.runUntilUtc(b.utc() + kDayS);
  TEST_ASSERT_TRUE(b.starts >= 1);
  TEST_ASSERT_FALSE(b.nvs.empty());
}

void test_no_new_run_while_the_cap_record_cant_be_saved() {
  // 4.6c: if the record can't be saved, no new run starts until a save works again (logged, shown
  // as held); the save is tried again every minute; the first that works lets the runs resume.
  Board b(false, 11 * 3600);
  b.runUntilUtc(kT0 + 12 * 3600 + 10);       // 12:00 runs; its start is saved
  TEST_ASSERT_TRUE(b.ion.on());
  b.nvsBroken = true;
  b.run(120);                                // the run's end can't be saved
  TEST_ASSERT_FALSE(b.ion.on());
  TEST_ASSERT_EQUAL_INT(1, b.ends);          // the run already going carried on to its end
  TEST_ASSERT_EQUAL_INT(1, b.recordFails);   // logged once, not every retry
  TEST_ASSERT_TRUE(b.ion.saveFailed());
  TEST_ASSERT_TRUE(b.ion.state() == IonState::Held);
  TEST_ASSERT_TRUE(b.ion.heldWhy() == IonWhy::RecordUnsaved);
  char t[64];
  formatIonState(t, sizeof(t), b.ion);
  TEST_ASSERT_EQUAL_STRING("ion held (cap record can't be saved)", t);
  const int s0 = b.saves;
  b.run(600);
  TEST_ASSERT_INT_WITHIN(1, 10, b.saves - s0);   // tried again every minute
  // 00:00: due, but no new run starts - skipped, logged
  b.runUntilUtc(kT0 + kDayS + 300);
  TEST_ASSERT_EQUAL_INT(1, b.starts);
  TEST_ASSERT_TRUE(b.lastSkipWhy == IonWhy::RecordUnsaved);
  TEST_ASSERT_EQUAL_INT(1, b.recordFails);
  // the NVS works again: the next retry saves, the runs resume at the next slot
  b.nvsBroken = false;
  b.run(61);
  TEST_ASSERT_FALSE(b.ion.saveFailed());
  TEST_ASSERT_EQUAL_INT(1, b.recordSaves);
  TEST_ASSERT_TRUE(b.ion.state() == IonState::Waiting);
  b.runUntilUtc(kT0 + kDayS + 12 * 3600 + 5);
  TEST_ASSERT_EQUAL_INT(2, b.starts);
  // the events read as sentences
  char e[160], o[200];
  IonSchedule::Change c;
  c.e = IonSchedule::Event::RecordFailed;
  formatIonEvent(e, sizeof(e), c);
  TEST_ASSERT_EQUAL_STRING("ION CAP RECORD NOT SAVED: no new run starts until it can be saved (tried again every minute)", e);
  eventSentence(o, sizeof(o), kEvTypeIon, e);
  TEST_ASSERT_EQUAL_STRING("Ion generator's cap record not saved: no new run starts until it can be saved (tried again every minute)", o);
  c.e = IonSchedule::Event::RecordSaved;
  formatIonEvent(e, sizeof(e), c);
  TEST_ASSERT_EQUAL_STRING("ION CAP RECORD SAVED again: runs resume", e);
}

void test_a_stored_run_never_ages_by_the_clock() {
  // 4.6c: every stored run counts as recent whatever the clock says - forward, back or untrusted.
  for (int k = 0; k < 3; k++) {
    Board b(false, 11 * 3600);
    b.in.minutesPerRun = 5;
    b.runUntilUtc(kT0 + 12 * 3600 + 400);    // 12:00: 5 min
    b.reboot(1000);
    if (k == 0) b.clockErrS += 30 * kDayS;   // a month ahead
    if (k == 1) b.clockErrS -= 30 * kDayS;   // a month behind
    if (k == 2) b.trusted = false;
    b.run(10);
    TEST_ASSERT_EQUAL_UINT32(300000, b.ion.usedMs(b.up));
  }
}

void test_repeated_reboots_dont_keep_a_run_forever() {
  // The record is saved once an hour while it holds a run, so each boot's uptime adds to a stored
  // run's known age: a run stops counting once it is certainly 24 h old, however often the board
  // restarts. Here: 10 min used, then a reboot every 6 h with nothing else to save.
  Board b(false, 11 * 3600);
  b.in.runsPerDay = 1;
  b.in.minutesPerRun = 10;
  b.runUntilUtc(kT0 + kDayS + 700);          // 00:00 day 2: 10 min
  TEST_ASSERT_EQUAL_UINT32(600000, b.ion.usedMs(b.up));
  b.in.enabled = false;
  for (int i = 0; i < 3; i++) { b.reboot(1000); b.run(6 * 3600); }
  TEST_ASSERT_EQUAL_UINT32(600000, b.ion.usedMs(b.up));   // ~18 h known: still counts
  b.reboot(1000);
  b.run(6 * 3600);
  b.reboot(1000);
  b.run(10);
  TEST_ASSERT_EQUAL_UINT32(0, b.ion.usedMs(b.up));        // 24 h known (less an hour per boot): gone
  TEST_ASSERT_EQUAL_INT(0, b.ion.recordCount());
}

void test_a_trusted_clock_jumping_forward_then_a_reboot() {
  // The exact sequence a big jump needs: the 10 minutes used, the trusted clock stepped forward
  // more than a day, a reboot (the restore ages the stored runs by that clock), then a slot.
  Board b(false, 3600);
  b.in.runsPerDay = 2;
  b.in.minutesPerRun = 5;
  b.runUntilUtc(kT0 + 12 * 3600 + 400);      // 00:00 is past at boot; 12:00 ran: 5 min
  b.reboot(1000);
  b.run(600);
  b.in.runsPerDay = 2;
  // make the second 5 min: the true next slot is 00:00
  b.runUntilUtc(kT0 + kDayS + 400);          // 00:00 day 2 ran: 10 min in the true last 24 h
  TEST_ASSERT_EQUAL_UINT32(600000, b.ion.usedMs(b.up));
  b.clockErrS += kDayS + 3600;               // the trusted clock steps forward 25 h
  b.reboot(1000);
  int64_t trueOnAfter = 0;
  for (int i = 0; i < 13 * 3600; i++) { b.step(); if (b.ion.on()) trueOnAfter++; }   // the next 13 true hours
  printf("  big forward jump, then a reboot: %lld s on in the next 13 h (10 min already used in the 24 h before)\n",
         (long long)trueOnAfter);
  TEST_ASSERT_EQUAL_INT64_MESSAGE(0, trueOnAfter, "the cap was exceeded in true time");
}

void test_a_simulated_month_with_big_trusted_clock_jumps() {
  // A clock that is trusted but jumps by hours or days (e.g. an RTC set wrong without its
  // lost-time flag, then corrected by NTP), with reboots in between.
  for (unsigned seed = 21; seed <= 32; seed++) month(false, seed, true);
}

void test_a_simulated_bench_month_never_exceeds_the_cap() {
  for (unsigned seed = 11; seed <= 13; seed++) month(true, seed);
}

// ---------------------------------------------------------------- text
void test_the_event_texts_and_sentences() {
  char e[160], o[200];
  IonSchedule::Change c;
  c.e = IonSchedule::Event::Started;
  c.ms = 120000;
  c.fromClock = true;
  c.slotUtcS = kT0 + 12 * 3600;
  formatIonEvent(e, sizeof(e), c);
  TEST_ASSERT_EQUAL_STRING("ION RUN START: 2 min (slot 12:00 UTC)", e);
  eventSentence(o, sizeof(o), kEvTypeIon, e);
  TEST_ASSERT_EQUAL_STRING("Ion generator run started: 2 min (slot 12:00 UTC)", o);
  c.ms = 90000;
  c.shortened = true;
  c.fromClock = false;
  formatIonEvent(e, sizeof(e), c);
  TEST_ASSERT_EQUAL_STRING("ION RUN START: 1 min 30 s (from boot, time not trusted); shortened: at most 10 minutes in 24 hours", e);
  c = IonSchedule::Change();
  c.e = IonSchedule::Event::Ended;
  c.ms = 120000;
  formatIonEvent(e, sizeof(e), c);
  TEST_ASSERT_EQUAL_STRING("ION RUN END: on 2 min 0 s", e);
  c.e = IonSchedule::Event::CutShort;
  c.why = IonWhy::Fault;
  c.ms = 45000;
  formatIonEvent(e, sizeof(e), c);
  TEST_ASSERT_EQUAL_STRING("ION RUN CUT SHORT: fault; on 0 min 45 s", e);
  eventSentence(o, sizeof(o), kEvTypeIon, e);
  TEST_ASSERT_EQUAL_STRING("Ion generator run cut short: fault; on 0 min 45 s", o);
  c.e = IonSchedule::Event::SwitchedOff;
  c.ms = 70000;
  formatIonEvent(e, sizeof(e), c);
  TEST_ASSERT_EQUAL_STRING("ION RUN STOPPED: switched off; on 1 min 10 s", e);
  c.e = IonSchedule::Event::Skipped;
  c.why = IonWhy::BootHold;
  c.fromClock = true;
  c.slotUtcS = kT0;
  formatIonEvent(e, sizeof(e), c);
  TEST_ASSERT_EQUAL_STRING("ION RUN SKIPPED: boot hold (slot 00:00 UTC)", e);
  eventSentence(o, sizeof(o), kEvTypeIon, e);
  TEST_ASSERT_EQUAL_STRING("Ion generator run skipped: boot hold (slot 00:00 UTC)", o);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_settings_and_their_limit);
  RUN_TEST(test_the_api_refuses_more_than_10_minutes_a_day);
  RUN_TEST(test_utc_slots_from_midnight_for_each_number_of_runs);
  RUN_TEST(test_a_slot_missed_while_off_is_not_caught_up);
  RUN_TEST(test_from_boot_when_the_time_is_not_trusted_then_the_clock);
  RUN_TEST(test_a_clock_stepped_back_never_repeats_a_slot_and_forward_skips);
  RUN_TEST(test_the_runs_length_is_timed_by_uptime);
  RUN_TEST(test_skipped_in_an_update_or_a_fault_and_cut_short_not_resumed);
  RUN_TEST(test_switched_off_mid_run_and_back_on_waits_for_the_next_slot);
  RUN_TEST(test_a_settings_change_mid_run_leaves_its_length);
  RUN_TEST(test_the_cap_with_settings_over_10_minutes);
  RUN_TEST(test_a_run_is_shortened_to_what_is_left);
  RUN_TEST(test_the_cap_survives_a_reboot_with_a_trusted_clock);
  RUN_TEST(test_the_cap_with_the_time_not_trusted_counts_every_stored_run);
  RUN_TEST(test_a_reboot_during_a_run_counts_its_whole_planned_length);
  RUN_TEST(test_no_run_before_the_record_is_restored);
  RUN_TEST(test_the_bench_runs_every_30_minutes_for_1_minute_until_the_cap);
  RUN_TEST(test_a_simulated_month_never_exceeds_the_cap);
  RUN_TEST(test_no_new_run_while_the_cap_record_cant_be_saved);
  RUN_TEST(test_a_run_whose_start_record_cant_be_saved_never_starts);
  RUN_TEST(test_a_stored_run_never_ages_by_the_clock);
  RUN_TEST(test_repeated_reboots_dont_keep_a_run_forever);
  RUN_TEST(test_a_trusted_clock_jumping_forward_then_a_reboot);
  RUN_TEST(test_a_simulated_month_with_big_trusted_clock_jumps);
  RUN_TEST(test_a_simulated_bench_month_never_exceeds_the_cap);
  RUN_TEST(test_the_event_texts_and_sentences);
  return UNITY_END();
}
