#pragma once
// Rev Q's hardware watchdog (spec 3A.5, 3A.11): the plain-logic half. U5 (MAX823) resets the
// board unless its WDI input changes level at least every 1.12 s (minimum). WDI is GPIO1:
//   - an input with both internal pulls off until the control loop is running (an undriven WDI
//     disables the watchdog, so a slow boot can't trip it);
//   - then its level changes EVERY loop pass, from the loop — never a timer, so a hung loop
//     is caught;
//   - never more than 0.9 s between changes;
//   - paused by setting it back to an input with both pulls off (never pad hold), e.g. for an
//     OTA erase.
// The driver (src/drivers/hw_watchdog) writes the pin; this says what to write and keeps the
// longest gap between changes, so BP2 can see how close the loop ever came.

#include <stdint.h>

namespace wineador {

class WdiToggle {
 public:
  static constexpr uint32_t kMaxGapMs = 900;   // spec 3A.11: never more than 0.9 s between changes

  enum class Pin : uint8_t { InputNoPulls, Output };

  // The control loop is running: drive the pin (first level: high) and start timing. Once only.
  void start(uint32_t nowMs);
  // Every loop pass. Changes the level if running; returns the level to write.
  bool pass(uint32_t nowMs);
  // Input, both pulls off: the watchdog is disabled until resume().
  void pause();
  void resume(uint32_t nowMs);

  Pin pin() const { return running_ ? Pin::Output : Pin::InputNoPulls; }
  bool running() const { return running_; }
  bool level() const { return level_; }
  uint32_t changes() const { return changes_; }
  // The longest time between two changes since start (pauses not counted).
  uint32_t longestGapMs() const { return longest_; }
  // True once each time a gap over kMaxGapMs is seen; *gapMs = that gap.
  bool takeLongGap(uint32_t* gapMs);

 private:
  bool started_ = false;
  bool running_ = false;
  bool level_ = false;
  uint32_t lastMs_ = 0;
  uint32_t longest_ = 0;
  uint32_t changes_ = 0;
  bool longGap_ = false;
  uint32_t longGapMs_ = 0;
};

}  // namespace wineador
