#pragma once
// Plenum fan duty logic (spec 4.6, 5.3). Plain logic — the fan driver applies
// whatever output() returns.
//
//   - 0% = stopped. 1-19% is never commanded: such a command is refused and
//     the previous duty stays.
//   - Kickstart: any start from 0% runs at 100% for 1 s, then the target duty.
//     At boot the fan counts as stopped, so the first command kickstarts it.

#include "wineador/types.h"

namespace wineador {

class FanController {
 public:
  FanController(uint8_t minDutyPct, uint32_t kickstartMs)
      : minDuty_(minDutyPct), kickMs_(kickstartMs) {}

  // Ask for a duty. Returns false (and changes nothing) if it is 1..min-1 or over 100.
  bool command(uint8_t pct, uint32_t nowMs);

  // The duty to drive right now. Call every pass; it ends the kickstart on time.
  uint8_t output(uint32_t nowMs);

  bool kicking() const { return kicking_; }
  uint8_t target() const { return target_; }
  uint32_t kickstarts() const { return kickstarts_; }

 private:
  uint8_t minDuty_;
  uint32_t kickMs_;
  uint8_t target_ = 0;
  uint8_t lastOutput_ = 0;   // what was last driven; 0 = stopped
  bool kicking_ = false;
  uint32_t kickStartMs_ = 0;
  uint32_t kickstarts_ = 0;
};

}  // namespace wineador
