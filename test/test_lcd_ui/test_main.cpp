// What the LCD shows and what a touch does (spec 5.5), as plain logic: UK time with summer time,
// the alerts and their order, the mode icons, the countdowns, the settings steps and bounds,
// the 60 s timeouts and dimming, the first touch only waking, the Allow update window, and the
// panel-to-screen touch conversion (lib/core uk_time, alerts, lcd_ui, touch_decode).
#include <unity.h>
#include <string.h>
#include "wineador/uk_time.h"
#include "wineador/alerts.h"
#include "wineador/lcd_ui.h"
#include "wineador/touch_decode.h"
#include "wineador/settings.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

static int64_t utc(int y, int mo, int d, int h, int mi, int s = 0) {
  return daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
}

// ---------------------------------------------------------------- UK time
void test_civil_days_round_trip() {
  TEST_ASSERT_EQUAL_INT64(0, daysFromCivil(1970, 1, 1));
  TEST_ASSERT_EQUAL_INT(4, weekdayFromDays(0));                       // a Thursday
  TEST_ASSERT_EQUAL_INT(0, weekdayFromDays(daysFromCivil(2026, 3, 29)));   // a Sunday
  for (int64_t d = -800; d < 30000; d += 37) {
    int y, m, dd;
    civilFromDays(d, &y, &m, &dd);
    TEST_ASSERT_EQUAL_INT64(d, daysFromCivil(y, m, dd));
  }
}

// Summer time 2026: 29 March 01:00 UTC to 25 October 01:00 UTC. 2027: 28 March, 31 October.
void test_summer_time_changes() {
  TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 29, 1, 0), ukSummerStartUtc(2026));
  TEST_ASSERT_EQUAL_INT64(utc(2026, 10, 25, 1, 0), ukSummerEndUtc(2026));
  TEST_ASSERT_EQUAL_INT64(utc(2027, 3, 28, 1, 0), ukSummerStartUtc(2027));
  TEST_ASSERT_EQUAL_INT64(utc(2027, 10, 31, 1, 0), ukSummerEndUtc(2027));
  // March: 00:59:59 GMT, then 02:00 BST
  LocalTime t = ukLocal(utc(2026, 3, 29, 0, 59, 59));
  TEST_ASSERT_FALSE(t.summer);
  TEST_ASSERT_EQUAL_INT(0, t.hour);
  TEST_ASSERT_EQUAL_INT(59, t.minute);
  t = ukLocal(utc(2026, 3, 29, 1, 0));
  TEST_ASSERT_TRUE(t.summer);
  TEST_ASSERT_EQUAL_INT(2, t.hour);
  TEST_ASSERT_EQUAL_INT(0, t.minute);
  // October: 01:59 BST (00:59 UTC), then 01:00 GMT again (the hour happens twice)
  t = ukLocal(utc(2026, 10, 25, 0, 59));
  TEST_ASSERT_TRUE(t.summer);
  TEST_ASSERT_EQUAL_INT(1, t.hour);
  TEST_ASSERT_EQUAL_INT(59, t.minute);
  t = ukLocal(utc(2026, 10, 25, 1, 0));
  TEST_ASSERT_FALSE(t.summer);
  TEST_ASSERT_EQUAL_INT(1, t.hour);
  TEST_ASSERT_EQUAL_INT(0, t.minute);
}

void test_local_date_rolls_over_at_local_midnight() {
  // 23:30 UTC on 14 July is 00:30 on 15 July in London
  const LocalTime t = ukLocal(utc(2026, 7, 14, 23, 30));
  TEST_ASSERT_EQUAL_INT(15, t.day);
  TEST_ASSERT_EQUAL_INT(0, t.hour);
  char s[8];
  TEST_ASSERT_EQUAL_STRING("00:30", formatHhMm(s, sizeof(s), t));
  // winter: no change
  TEST_ASSERT_EQUAL_STRING("14:32", formatHhMm(s, sizeof(s), ukLocal(utc(2026, 11, 13, 14, 32))));
}

// ---------------------------------------------------------------- alerts
void test_alert_order_and_leaving() {
  AlertTracker t;
  AlertInputs in;
  in.sensorsKnown = true;
  in.sdFailed = true;
  t.update(in, 1000);
  in.excludedMask = 1u << 3;
  t.update(in, 2000);
  in.fanStalled = true;
  t.update(in, 3000);
  in.clockBad = true;
  in.heaterIneffective = true;
  t.update(in, 4000);
  // fault, ineffective, sensor, SD, clock
  TEST_ASSERT_EQUAL_INT(5, t.count());
  TEST_ASSERT_TRUE(t.at(0).slot == AlertSlot::FaultFan);
  TEST_ASSERT_TRUE(t.at(1).slot == AlertSlot::HeaterIneffective);
  TEST_ASSERT_TRUE(t.at(2).slot == AlertSlot::Sensor3);
  TEST_ASSERT_TRUE(t.at(3).slot == AlertSlot::SdFailed);
  TEST_ASSERT_TRUE(t.at(4).slot == AlertSlot::ClockBad);
  TEST_ASSERT_EQUAL_UINT64(1000, t.at(3).sinceMs);
  TEST_ASSERT_EQUAL_STRING("Excluded: mid_rear", t.at(2).title);
  // resolved: leaves by itself; back again: a new start time
  in.sdFailed = false;
  t.update(in, 5000);
  TEST_ASSERT_EQUAL_INT(4, t.count());
  in.sdFailed = true;
  t.update(in, 6000);
  TEST_ASSERT_EQUAL_UINT64(6000, t.at(3).sinceMs);
}

void test_alerts_oldest_first_within_a_rank() {
  AlertTracker t;
  AlertInputs in;
  in.sensorsKnown = true;
  in.excludedMask = 1u << 5;           // ambient first
  t.update(in, 1000);
  in.excludedMask |= 1u << 0;          // then top_front
  in.manualMask = 1u << 0;
  t.update(in, 2000);
  TEST_ASSERT_TRUE(t.at(0).slot == AlertSlot::Sensor5);
  TEST_ASSERT_TRUE(t.at(1).slot == AlertSlot::Sensor0);
  TEST_ASSERT_EQUAL_STRING("Excluded: top_front (manual)", t.at(1).title);
}

void test_fault_causes_named() {
  AlertTracker t;
  AlertInputs in;
  in.sensorsKnown = true;
  in.sensorFault = SensorFault::Chamber;
  t.update(in, 1);
  TEST_ASSERT_EQUAL_STRING("Fault: chamber sensor", t.at(0).title);
  in.sensorFault = SensorFault::Cabinet;
  t.update(in, 2);
  TEST_ASSERT_EQUAL_STRING("Fault: cabinet sensors", t.at(0).title);
  TEST_ASSERT_EQUAL_UINT64(1, t.at(0).sinceMs);       // the same fault: still from when it started
  in.sensorFault = SensorFault::RecoveryHold;
  t.update(in, 3);
  TEST_ASSERT_EQUAL_STRING("Fault: sensor recovery", t.at(0).title);
  in.settingsBad = true;
  t.update(in, 4);
  TEST_ASSERT_EQUAL_INT(2, t.count());
  // before the first sweep, the sensors are not judged
  AlertTracker u;
  AlertInputs b;
  b.sensorFault = SensorFault::Cabinet;
  b.excludedMask = 0x3F;
  u.update(b, 1);
  TEST_ASSERT_EQUAL_INT(0, u.count());
}

void test_alert_when() {
  char s[24];
  const int64_t now = utc(2026, 11, 13, 14, 32);
  formatAlertWhen(s, sizeof(s), 0, 0, true, now);
  TEST_ASSERT_EQUAL_STRING("14:32", s);
  formatAlertWhen(s, sizeof(s), 0, 3ull * 3600000ull, true, now);      // 3 h ago
  TEST_ASSERT_EQUAL_STRING("11:32", s);
  formatAlertWhen(s, sizeof(s), 0, 20ull * 3600000ull, true, now);     // yesterday
  TEST_ASSERT_EQUAL_STRING("12 Nov 18:32", s);
  formatAlertWhen(s, sizeof(s), 0, 12ull * 60000ull, false, now);      // clock not trusted
  TEST_ASSERT_EQUAL_STRING("for 12 min", s);
  formatFor(s, sizeof(s), 30000);
  TEST_ASSERT_EQUAL_STRING("for <1 min", s);
  formatFor(s, sizeof(s), (3 * 60 + 5) * 60000ull);
  TEST_ASSERT_EQUAL_STRING("for 3 h 5 min", s);
  formatFor(s, sizeof(s), (2 * 24 + 4) * 3600000ull);
  TEST_ASSERT_EQUAL_STRING("for 2 d 4 h", s);
}

// The boot sequence: no Fault for the pass between a good first sweep and the gate seeing it;
// the clock alert only after the boot sweep read the RTC, though "--:--" shows before it.
void test_boot_sequence_fault_and_clock() {
  // before the first sweep: no healthy sensors - but sensorsKnown is false, so no alert
  AlertTracker t;
  AlertInputs a;
  a.sensorFault = sensorFaultFrom(false, 0, 3, false);
  a.excludedMask = 0x3F;
  t.update(a, 100);
  TEST_ASSERT_EQUAL_INT(0, t.count());
  // the first sweep made all six healthy; the gate hasn't seen it yet (not holding): no fault
  TEST_ASSERT_TRUE(sensorFaultFrom(true, 4, 3, false) == SensorFault::None);
  // later a cabinet sensor drops out, then comes back with the recovery hold running
  TEST_ASSERT_TRUE(sensorFaultFrom(true, 2, 3, false) == SensorFault::Cabinet);
  TEST_ASSERT_TRUE(sensorFaultFrom(false, 4, 3, false) == SensorFault::Chamber);
  TEST_ASSERT_TRUE(sensorFaultFrom(true, 3, 3, true) == SensorFault::RecoveryHold);
  // the clock
  ClockFacts c;                                   // boot: nothing read yet
  TEST_ASSERT_TRUE(clockBad(c));                  // "--:--"
  TEST_ASSERT_FALSE(clockAlert(c));               // but no alert yet
  c.firstSweepDone = c.clockSet = c.trusted = c.rtcOk = true;   // the RTC read, OSF clear
  TEST_ASSERT_FALSE(clockBad(c));
  TEST_ASSERT_FALSE(clockAlert(c));
  c.trusted = false;                              // OSF set at boot: lost time, NTP not yet
  TEST_ASSERT_TRUE(clockAlert(c));
  c.trusted = true;
  c.rtcOk = false;                                // RTC unreadable, no NTP since boot
  TEST_ASSERT_TRUE(clockAlert(c));
  c.ntpSynced = true;                             // NTP has set it
  TEST_ASSERT_FALSE(clockAlert(c));
  ClockFacts never;                               // the sweep done, the RTC never read, no NTP
  never.firstSweepDone = true;
  TEST_ASSERT_TRUE(clockAlert(never));
}

// ---------------------------------------------------------------- the session
static UiInputs base() {
  UiInputs in;
  in.nowMs = 100000;
  in.uptimeMs = 100000;
  in.cabinetOk = 4;
  in.fresh = true;
  in.meanC = 20.26f;
  in.meanRhPct = 61.6f;
  in.utcNowS = utc(2026, 11, 13, 14, 32);
  in.alerts.sensorsKnown = true;
  return in;
}

static void tap(LcdUi& ui, UiInputs& in, Rect r, uint32_t gapMs = 300) {
  in.nowMs += gapMs;
  in.uptimeMs += gapMs;
  ui.touch(true, r.x + r.w / 2, r.y + r.h / 2, in.nowMs);
  ui.update(in);
  ui.touch(false, 0, 0, in.nowMs + 50);
}

static void later(LcdUi& ui, UiInputs& in, uint32_t ms) {
  in.nowMs += ms;
  in.uptimeMs += ms;
  ui.update(in);
}

void test_readings_and_dashes() {
  UiInputs in = base();
  LcdUi ui;
  ui.begin(in.nowMs, 20);
  ui.update(in);
  TEST_ASSERT_EQUAL_STRING("20.3", ui.view().tempText);
  TEST_ASSERT_EQUAL_STRING("62", ui.view().rhText);
  TEST_ASSERT_EQUAL_STRING("14:32", ui.view().timeText);
  in.cabinetOk = 2;
  ui.update(in);
  TEST_ASSERT_EQUAL_STRING("--", ui.view().tempText);
  TEST_ASSERT_EQUAL_STRING("--", ui.view().rhText);
  in.cabinetOk = 3;
  in.fresh = false;
  ui.update(in);
  TEST_ASSERT_EQUAL_STRING("--", ui.view().tempText);
  in.fresh = true;
  in.alerts.clockBad = true;
  in.clockOk = false;
  ui.update(in);
  TEST_ASSERT_EQUAL_STRING("20.3", ui.view().tempText);
  TEST_ASSERT_EQUAL_STRING("--:--", ui.view().timeText);
}

void test_mode_icons_and_priority() {
  UiInputs in = base();
  LcdUi ui;
  ui.begin(in.nowMs, 20);
  ui.update(in);
  TEST_ASSERT_EQUAL_UINT8(1, ui.view().nModes);
  TEST_ASSERT_TRUE(ui.view().modes[0] == ModeIcon::Standby);
  in.heat = true;
  in.humid = true;
  ui.update(in);
  TEST_ASSERT_EQUAL_UINT8(2, ui.view().nModes);
  TEST_ASSERT_TRUE(ui.view().modes[0] == ModeIcon::Heating);
  TEST_ASSERT_TRUE(ui.view().modes[1] == ModeIcon::Humidifying);
  in = base();
  in.cool = true;
  in.stripping = true;
  ui.update(in);
  TEST_ASSERT_EQUAL_UINT8(1, ui.view().nModes);
  TEST_ASSERT_TRUE(ui.view().modes[0] == ModeIcon::Drying);
  // Starting shows alone, with m:ss
  in = base();
  in.bootHold = true;
  in.bootHoldLeftMs = 277001;
  ui.update(in);
  TEST_ASSERT_TRUE(ui.view().modes[0] == ModeIcon::Starting);
  TEST_ASSERT_EQUAL_STRING("Starting 4:38", ui.view().modeText[0]);
  // Updating beats Starting; Fault beats everything
  in.updating = true;
  in.updatePct = 63;
  ui.update(in);
  TEST_ASSERT_TRUE(ui.view().modes[0] == ModeIcon::Updating);
  TEST_ASSERT_EQUAL_STRING("Updating 63%", ui.view().modeText[0]);
  in.alerts.fanStalled = true;
  in.heat = true;
  ui.update(in);
  TEST_ASSERT_EQUAL_UINT8(1, ui.view().nModes);
  TEST_ASSERT_TRUE(ui.view().modes[0] == ModeIcon::Fault);
  // a non-fault alert doesn't make it Fault
  in = base();
  in.alerts.sdFailed = true;
  ui.update(in);
  TEST_ASSERT_TRUE(ui.view().modes[0] == ModeIcon::Standby);
  TEST_ASSERT_EQUAL_STRING("SD card failure", ui.view().alertText);
}

void test_alert_line_more() {
  UiInputs in = base();
  in.alerts.sdFailed = true;
  in.alerts.dip12v = true;
  in.alerts.excludedMask = 0x03;
  LcdUi ui;
  ui.begin(in.nowMs, 20);
  ui.update(in);
  TEST_ASSERT_TRUE(ui.view().alertLine);
  TEST_ASSERT_EQUAL_STRING("12V supply dip", ui.view().alertText);
  TEST_ASSERT_EQUAL_STRING("+3 more", ui.view().moreText);
  in = base();
  ui.update(in);
  TEST_ASSERT_FALSE(ui.view().alertLine);
}

void test_settings_steps_bounds_save_cancel() {
  UiInputs in = base();
  LcdUi ui;
  ui.begin(in.nowMs, 20);
  ui.update(in);
  tap(ui, in, layout::kSettingsPill);
  TEST_ASSERT_TRUE(ui.screen() == LcdScreen::Settings);
  TEST_ASSERT_EQUAL_STRING("20.0", ui.view().draftTemp);
  tap(ui, in, layout::kTempPlus);
  tap(ui, in, layout::kRhMinus);
  TEST_ASSERT_EQUAL_STRING("20.5", ui.view().draftTemp);
  TEST_ASSERT_EQUAL_STRING("61", ui.view().draftRh);
  TEST_ASSERT_TRUE(ui.view().draftChanged);
  // Cancel drops it
  tap(ui, in, layout::kCancel);
  TEST_ASSERT_TRUE(ui.screen() == LcdScreen::WalkPast);
  float t, r;
  TEST_ASSERT_FALSE(ui.takeSave(&t, &r));
  // to the bounds and no further
  tap(ui, in, layout::kSettingsPill);
  TEST_ASSERT_EQUAL_STRING("20.0", ui.view().draftTemp);   // reopened from the saved values
  for (int i = 0; i < 30; i++) tap(ui, in, layout::kTempMinus);
  for (int i = 0; i < 30; i++) tap(ui, in, layout::kRhPlus);
  TEST_ASSERT_EQUAL_STRING("15.0", ui.view().draftTemp);
  TEST_ASSERT_EQUAL_STRING("70", ui.view().draftRh);
  TEST_ASSERT_FALSE(ui.view().tempDown);
  TEST_ASSERT_TRUE(ui.view().tempUp);
  TEST_ASSERT_FALSE(ui.view().rhUp);
  tap(ui, in, layout::kSave);
  TEST_ASSERT_TRUE(ui.screen() == LcdScreen::WalkPast);
  TEST_ASSERT_TRUE(ui.takeSave(&t, &r));
  TEST_ASSERT_EQUAL_FLOAT(15.0f, t);
  TEST_ASSERT_EQUAL_FLOAT(70.0f, r);
  TEST_ASSERT_FALSE(ui.takeSave(&t, &r));                 // once
  // Save with nothing changed: back, nothing to apply
  tap(ui, in, layout::kSettingsPill);
  tap(ui, in, layout::kSave);
  TEST_ASSERT_FALSE(ui.takeSave(&t, &r));
}

void test_sixty_second_timeouts_and_dimming() {
  UiInputs in = base();
  LcdUi ui;
  ui.begin(in.nowMs, 20);
  ui.update(in);
  TEST_ASSERT_EQUAL_UINT8(100, ui.view().backlightPct);
  tap(ui, in, layout::kSettingsPill);
  tap(ui, in, layout::kTempPlus);
  later(ui, in, 59000);
  TEST_ASSERT_TRUE(ui.screen() == LcdScreen::Settings);
  TEST_ASSERT_EQUAL_UINT8(100, ui.view().backlightPct);
  later(ui, in, 1000);                                     // 60 s without a touch
  TEST_ASSERT_TRUE(ui.screen() == LcdScreen::WalkPast);
  TEST_ASSERT_EQUAL_UINT8(20, ui.view().backlightPct);
  float t, r;
  TEST_ASSERT_FALSE(ui.takeSave(&t, &r));                  // the unsaved change was dropped
  // the first touch only wakes: the Settings button under it does nothing
  tap(ui, in, layout::kSettingsPill, 1000);
  TEST_ASSERT_TRUE(ui.screen() == LcdScreen::WalkPast);
  TEST_ASSERT_EQUAL_UINT8(100, ui.view().backlightPct);
  tap(ui, in, layout::kSettingsPill);
  TEST_ASSERT_TRUE(ui.screen() == LcdScreen::Settings);
  // the alerts screen times out the same way
  in.alerts.sdFailed = true;
  tap(ui, in, layout::kCancel);
  tap(ui, in, layout::kAlertPill);
  TEST_ASSERT_TRUE(ui.screen() == LcdScreen::Alerts);
  later(ui, in, 60000);
  TEST_ASSERT_TRUE(ui.screen() == LcdScreen::WalkPast);
}

void test_a_held_finger_acts_once() {
  UiInputs in = base();
  LcdUi ui;
  ui.begin(in.nowMs, 20);
  ui.update(in);
  tap(ui, in, layout::kSettingsPill);
  const Rect p = layout::kTempPlus;
  for (int i = 0; i < 10; i++) {                           // ten samples, finger still down
    in.nowMs += 50;
    ui.touch(true, p.x + 5, p.y + 5, in.nowMs);
    ui.update(in);
  }
  TEST_ASSERT_EQUAL_STRING("20.5", ui.view().draftTemp);
}

void test_allow_update_window() {
  UiInputs in = base();
  LcdUi ui;
  ui.begin(in.nowMs, 20);
  ui.update(in);
  TEST_ASSERT_FALSE(ui.window().active(in.nowMs));
  TEST_ASSERT_FALSE(ui.window().consume(in.nowMs));       // outside the window: refused
  tap(ui, in, layout::kSettingsPill);
  tap(ui, in, layout::kAllowUpdate);
  TEST_ASSERT_TRUE(ui.takeWindowOpened());
  TEST_ASSERT_TRUE(ui.view().updateWindow);
  TEST_ASSERT_EQUAL_STRING("5:00", ui.view().allowSub);
  tap(ui, in, layout::kCancel);
  later(ui, in, 48000);
  TEST_ASSERT_EQUAL_STRING("Update allowed - 4:12 left", ui.view().updateText);
  // pressing again restarts the 5 minutes
  tap(ui, in, layout::kSettingsPill);
  tap(ui, in, layout::kAllowUpdate);
  TEST_ASSERT_EQUAL_STRING("5:00", ui.view().allowSub);
  // closes at 5 minutes
  later(ui, in, kUpdateWindowMs);
  TEST_ASSERT_FALSE(ui.view().updateWindow);
  TEST_ASSERT_FALSE(ui.window().consume(in.nowMs));
  // one update only
  ui.window().open(in.nowMs);
  TEST_ASSERT_TRUE(ui.window().consume(in.nowMs + 1000));
  TEST_ASSERT_FALSE(ui.window().consume(in.nowMs + 2000));
}

void test_update_starting_closes_screens() {
  UiInputs in = base();
  LcdUi ui;
  ui.begin(in.nowMs, 20);
  ui.update(in);
  tap(ui, in, layout::kSettingsPill);
  tap(ui, in, layout::kTempPlus);
  in.updating = true;
  ui.update(in);
  TEST_ASSERT_TRUE(ui.screen() == LcdScreen::WalkPast);
  TEST_ASSERT_FALSE(ui.view().settingsEnabled);
  float t, r;
  TEST_ASSERT_FALSE(ui.takeSave(&t, &r));
  tap(ui, in, layout::kSettingsPill);                      // not available while Updating
  TEST_ASSERT_TRUE(ui.screen() == LcdScreen::WalkPast);
  in.alerts.sdFailed = true;
  ui.update(in);
  tap(ui, in, layout::kAlertPill);                         // alerts still open while Updating
  TEST_ASSERT_TRUE(ui.screen() == LcdScreen::Alerts);
  // the alerts screen was open when the update started: it closes too
  in.updating = false;
  ui.update(in);
  in.updating = true;
  ui.update(in);
  TEST_ASSERT_TRUE(ui.screen() == LcdScreen::WalkPast);
}

void test_alert_pages() {
  UiInputs in = base();
  in.alerts.sensorFault = SensorFault::Chamber;
  in.alerts.fanStalled = in.alerts.settingsBad = in.alerts.dip12v = true;
  in.alerts.heaterIneffective = in.alerts.coolingIneffective = in.alerts.humidIneffective = true;
  in.alerts.excludedMask = 0x3F;
  in.alerts.sdFailed = in.alerts.clockBad = true;
  LcdUi ui;
  ui.begin(in.nowMs, 20);
  ui.update(in);
  TEST_ASSERT_EQUAL_UINT8(15, ui.view().alertCount);
  tap(ui, in, layout::kAlertPill);
  TEST_ASSERT_EQUAL_UINT8(3, ui.view().pages);
  TEST_ASSERT_EQUAL_UINT8(5, ui.view().rows);
  tap(ui, in, layout::kPageUp);                            // already first
  TEST_ASSERT_EQUAL_UINT8(0, ui.view().page);
  tap(ui, in, layout::kPageDown);
  tap(ui, in, layout::kPageDown);
  tap(ui, in, layout::kPageDown);                          // already last
  TEST_ASSERT_EQUAL_UINT8(2, ui.view().page);
  TEST_ASSERT_EQUAL_STRING("Clock lost or not set", ui.view().row[4].title);
  // alerts clear: the page stays inside the list
  in.alerts = AlertInputs{};
  in.alerts.sensorsKnown = true;
  in.alerts.sdFailed = true;
  ui.update(in);
  TEST_ASSERT_EQUAL_UINT8(0, ui.view().page);
  TEST_ASSERT_EQUAL_UINT8(1, ui.view().rows);
  in.alerts.sdFailed = false;
  ui.update(in);
  TEST_ASSERT_EQUAL_UINT8(0, ui.view().rows);
  tap(ui, in, layout::kBack);
  TEST_ASSERT_TRUE(ui.screen() == LcdScreen::WalkPast);
}

void test_countdown_format() {
  char s[12];
  TEST_ASSERT_EQUAL_STRING("5:00", formatMss(s, sizeof(s), 300000));
  TEST_ASSERT_EQUAL_STRING("0:07", formatMss(s, sizeof(s), 6200));
  TEST_ASSERT_EQUAL_STRING("0:01", formatMss(s, sizeof(s), 1));
  TEST_ASSERT_EQUAL_STRING("0:00", formatMss(s, sizeof(s), 0));
}

// ---------------------------------------------------------------- touch, orientation
void test_touch_to_screen() {
  int x, y;
  touchToScreen(LcdOrientation::Portrait, 10, 20, &x, &y);
  TEST_ASSERT_EQUAL_INT(10, x);
  TEST_ASSERT_EQUAL_INT(20, y);
  // landscape A: the panel's (0,0) is the screen's bottom-left
  touchToScreen(LcdOrientation::LandscapeA, 0, 0, &x, &y);
  TEST_ASSERT_EQUAL_INT(0, x);
  TEST_ASSERT_EQUAL_INT(319, y);
  touchToScreen(LcdOrientation::LandscapeA, 319, 479, &x, &y);
  TEST_ASSERT_EQUAL_INT(479, x);
  TEST_ASSERT_EQUAL_INT(0, y);
  // landscape B is A turned 180 degrees
  for (int px = 0; px < 320; px += 53)
    for (int py = 0; py < 480; py += 61) {
      int ax, ay, bx, by;
      touchToScreen(LcdOrientation::LandscapeA, (uint16_t)px, (uint16_t)py, &ax, &ay);
      touchToScreen(LcdOrientation::LandscapeB, (uint16_t)px, (uint16_t)py, &bx, &by);
      TEST_ASSERT_EQUAL_INT(479 - ax, bx);
      TEST_ASSERT_EQUAL_INT(319 - ay, by);
    }
  touchToScreen(LcdOrientation::LandscapeA, 900, 900, &x, &y);   // off the panel: clamped
  TEST_ASSERT_TRUE(x >= 0 && x < 480 && y >= 0 && y < 320);
}

void test_orientations() {
  TEST_ASSERT_EQUAL_HEX8(0x48, lcdMadctl(LcdOrientation::Portrait));
  TEST_ASSERT_EQUAL_HEX8(0x28, lcdMadctl(LcdOrientation::LandscapeA));
  TEST_ASSERT_EQUAL_HEX8(0xE8, lcdMadctl(LcdOrientation::LandscapeB));
  // A and B differ by MX and MY only (180 degrees), both with rows and columns exchanged
  TEST_ASSERT_EQUAL_HEX8(0xC0, lcdMadctl(LcdOrientation::LandscapeA) ^ lcdMadctl(LcdOrientation::LandscapeB));
  TEST_ASSERT_EQUAL_UINT16(480, lcdWidth(LcdOrientation::LandscapeA));
  TEST_ASSERT_EQUAL_UINT16(320, lcdHeight(LcdOrientation::LandscapeB));
  TEST_ASSERT_EQUAL_UINT16(kScreenW, lcdWidth(LcdOrientation::LandscapeA));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_civil_days_round_trip);
  RUN_TEST(test_summer_time_changes);
  RUN_TEST(test_local_date_rolls_over_at_local_midnight);
  RUN_TEST(test_alert_order_and_leaving);
  RUN_TEST(test_alerts_oldest_first_within_a_rank);
  RUN_TEST(test_fault_causes_named);
  RUN_TEST(test_alert_when);
  RUN_TEST(test_boot_sequence_fault_and_clock);
  RUN_TEST(test_readings_and_dashes);
  RUN_TEST(test_mode_icons_and_priority);
  RUN_TEST(test_alert_line_more);
  RUN_TEST(test_settings_steps_bounds_save_cancel);
  RUN_TEST(test_sixty_second_timeouts_and_dimming);
  RUN_TEST(test_a_held_finger_acts_once);
  RUN_TEST(test_allow_update_window);
  RUN_TEST(test_update_starting_closes_screens);
  RUN_TEST(test_alert_pages);
  RUN_TEST(test_countdown_format);
  RUN_TEST(test_touch_to_screen);
  RUN_TEST(test_orientations);
  return UNITY_END();
}
