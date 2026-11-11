#pragma once
// WiFi only for the NTP sync (spec 5.4, per board; Rev N). The radio's current
// bursts made the SD card reset and took most of the heap (B4 SD investigation),
// and Rev N has no dashboard, so on Rev N the radio is on only while a sync runs:
//   - a window opens at boot, then 24 h after each sync that worked;
//   - it closes as soon as the sync has worked, or after 2 minutes;
//   - after a window with no sync, the next one opens 1 hour later.
// Plain logic: it is told the time and the sync count, and says what to do.

#include <stdint.h>

namespace wineador {

class NtpWindow {
 public:
  static constexpr uint32_t kWindowMs = 2UL * 60UL * 1000UL;        // the longest the radio stays on
  static constexpr uint32_t kRetryMs = 60UL * 60UL * 1000UL;        // after a window with no sync
  static constexpr uint32_t kIntervalMs = 24UL * 60UL * 60UL * 1000UL;  // after a sync that worked

  enum class Action : uint8_t { None, Open, CloseSynced, CloseTimedOut };

  // Every loop pass. `syncCount` goes up by one with each NTP sync. The first call opens.
  Action update(uint32_t nowMs, uint32_t syncCount);
  // TEST BUILDS: open a window at the next update (if one isn't open).
  void requestNow() { now_ = true; }

  bool isOpen() const { return open_; }
  uint32_t openForMs(uint32_t nowMs) const { return open_ ? nowMs - openedMs_ : 0; }
  uint32_t waitMs() const { return waitMs_; }     // the wait set at the last close

 private:
  bool started_ = false, open_ = false, now_ = false;
  uint32_t openedMs_ = 0, closedMs_ = 0, waitMs_ = 0, syncsAtOpen_ = 0;
};

}  // namespace wineador
