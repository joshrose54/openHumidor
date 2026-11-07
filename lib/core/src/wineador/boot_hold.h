#pragma once
// Boot hold (spec 4.4, "Timing state at boot"): on every boot the compressor,
// heater and humidifier are held off for the full startup delay. Nothing is
// carried over from before the restart.
//
// Once the hold has ended it stays ended — it cannot come back when millis()
// wraps after 49 days.

#include "wineador/types.h"

namespace wineador {

class BootHold {
 public:
  void begin(uint32_t nowMs, uint32_t holdMs);
  void update(uint32_t nowMs);

  bool active() const { return !ended_; }
  uint32_t remainingMs() const { return ended_ ? 0 : remaining_; }
  uint32_t lengthMs() const { return holdMs_; }

 private:
  uint32_t startMs_ = 0;
  uint32_t holdMs_ = 0;
  uint32_t remaining_ = 0;
  bool ended_ = false;
};

}  // namespace wineador
