#pragma once
// What the LCD shows, and what a touch does (spec 5.5). Plain logic: no pixels, no hardware.
// The firmware feeds it the state each pass (UiInputs) and touch samples; it gives back a
// UiView — every text and flag the drawing needs — plus the actions a touch asked for (save
// the settings, open the Allow update window). lib/lcdgfx draws a UiView; the driver sends it.
//
//   Screens: walk-past, settings, alerts. Settings and alerts go back to the walk-past screen
//   after 60 s without a touch (unsaved settings dropped), or when an update starts.
//   Backlight: full, dimmed 60 s after the last touch; a touch while dimmed only wakes it.
//   Mode icons: loads actually running; Fault > Updating > Starting > running modes > Standby.
//   Allow update: a 5-minute window; pressing again restarts it; closed after one update,
//   at 5 minutes, or by a reboot (it lives in RAM). Q5's OTA asks updateAllowed().

#include <stdint.h>
#include <stddef.h>
#include "wineador/alerts.h"
#include "wineador/lcd_layout.h"

namespace wineador {

constexpr uint32_t kUiIdleMs = 60000;               // 5.5: back to walk-past, and dim, after 60 s
constexpr uint32_t kUpdateWindowMs = 5UL * 60000UL; // 5.5: Allow update, 5 minutes

enum class LcdScreen : uint8_t { WalkPast, Settings, Alerts };
enum class ModeIcon : uint8_t { Cooling, Heating, Humidifying, Drying, Standby, Starting, Updating, Fault };
enum class BuildTag : uint8_t { None, Bench, Test, BenchTest };
const char* modeName(ModeIcon m);

struct UiInputs {
  uint32_t nowMs = 0;                 // millis()
  uint64_t uptimeMs = 0;              // never wraps (alert start times)
  // readings (4.3): the mean of the healthy cabinet sensors
  int cabinetOk = 0;                  // healthy cabinet sensors, of 4
  bool fresh = false;                 // a sweep within twice the sensor read rate
  float meanC = 0, meanRhPct = 0;
  float targetC = 20, rhSetPct = 62;  // the user settings now
  // what is running (the outputs, not the calls)
  bool cool = false, heat = false, humid = false;
  bool stripping = false;             // cooling is an RH strip: shown as Drying
  bool bootHold = false;
  uint32_t bootHoldLeftMs = 0;
  bool updating = false;              // an OTA update in progress (Q5)
  uint8_t updatePct = 0;
  AlertInputs alerts;                 // includes clockBad: the alert (5.5's "clock lost or not set")
  bool clockOk = true;                // the time may be shown (false: "--:--", and "for 12 min")
  int64_t utcNowS = 0;                // the clock now (meaningful when clockOk)
  bool wifi = false;
  BuildTag tag = BuildTag::None;
};

constexpr int kMaxModes = 3;

struct AlertRowView {
  char title[44] = "";
  char when[20] = "";
  uint8_t rank = 0;
};

struct UiView {
  LcdScreen screen = LcdScreen::WalkPast;
  // ---- walk-past
  char timeText[8] = "--:--";
  BuildTag tag = BuildTag::None;
  bool wifi = false;
  bool readingsOk = false;
  char tempText[8] = "--";            // "20.3"
  char rhText[8] = "--";              // "62"
  char setTempText[16] = "";          // "Set 20.0 °C"
  char setRhText[16] = "";            // "Set 62 %"
  uint8_t nModes = 0;
  ModeIcon modes[kMaxModes] = {};
  char modeText[kMaxModes][20] = {};  // "Heating", "Starting 4:37", "Updating 63%"
  bool updateWindow = false;
  char updateText[40] = "";           // "Update allowed - 4:12 left"
  bool alertLine = false;
  char alertText[44] = "";
  char moreText[16] = "";             // "+2 more"
  uint8_t alertRank = 0;
  bool settingsEnabled = true;        // not while Updating
  // ---- settings
  char draftTemp[8] = "";             // "20.5"
  char draftRh[8] = "";               // "62"
  bool tempDown = true, tempUp = true, rhDown = true, rhUp = true;
  bool draftChanged = false;
  char allowText[24] = "Allow update";
  char allowSub[12] = "";             // "4:59" while the window is open
  // ---- alerts
  uint8_t alertCount = 0;
  uint8_t page = 0, pages = 1;
  uint8_t rows = 0;
  AlertRowView row[layout::kAlertRows];
  // ---- backlight
  uint8_t backlightPct = 100;
};

class UpdateWindow {
 public:
  void open(uint32_t nowMs) { open_ = true; openedMs_ = nowMs; }   // again while open: restarts
  void close() { open_ = false; }
  bool active(uint32_t nowMs);                                       // closes itself at 5 min
  uint32_t remainingMs(uint32_t nowMs);
  // Q5: an update arrived. True if inside the window; the window closes (one update only).
  bool consume(uint32_t nowMs);

 private:
  bool open_ = false;
  uint32_t openedMs_ = 0;
};

class LcdUi {
 public:
  void begin(uint32_t nowMs, uint8_t dimPct);

  // One touch sample, in landscape screen coordinates. A touch acts once, as the finger goes
  // down; it must lift before the next. While dimmed, the first touch only wakes the screen.
  void touch(bool down, int x, int y, uint32_t nowMs);

  // Builds the view from the state now; runs the timeouts.
  void update(const UiInputs& in);
  const UiView& view() const { return v_; }
  LcdScreen screen() const { return screen_; }

  bool dimmed(uint32_t nowMs) const;
  uint8_t backlightPct(uint32_t nowMs) const { return dimmed(nowMs) ? dimPct_ : 100; }

  // Save was pressed with changed values: the firmware applies them (and logs "LCD").
  bool takeSave(float* targetC, float* rhPct);
  // The Allow update window was opened (or restarted) by a touch: the firmware logs it.
  bool takeWindowOpened();
  UpdateWindow& window() { return window_; }
  const AlertTracker& alerts() const { return alerts_; }

 private:
  void press(int x, int y, uint32_t nowMs);
  void toWalkPast() { screen_ = LcdScreen::WalkPast; }
  uint8_t dimPct_ = 20;
  LcdScreen screen_ = LcdScreen::WalkPast;
  uint32_t lastTouchMs_ = 0;
  bool down_ = false;
  bool wasUpdating_ = false;
  float draftT_ = 20, draftRh_ = 62;
  bool saveReady_ = false, windowOpened_ = false;
  float saveT_ = 0, saveRh_ = 0;
  uint8_t page_ = 0;
  UiInputs in_;
  AlertTracker alerts_;
  UpdateWindow window_;
  UiView v_;
};

// "4:37" from milliseconds left (rounded up to the second).
const char* formatMss(char* out, size_t n, uint32_t ms);

}  // namespace wineador
