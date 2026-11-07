#include "wineador/fan_controller.h"

namespace wineador {

bool FanController::command(uint8_t pct, uint32_t nowMs) {
  if (pct > 100) return false;
  if (pct > 0 && pct < minDuty_) return false;   // 1-19%: refused (4.6)

  target_ = pct;
  if (pct == 0) {
    kicking_ = false;                 // stopping cancels any kickstart
  } else if (lastOutput_ == 0 && !kicking_) {
    kicking_ = true;                  // a start from 0%: kickstart
    kickStartMs_ = nowMs;
    kickstarts_ = kickstarts_ + 1;
  }
  return true;
}

uint8_t FanController::output(uint32_t nowMs) {
  if (kicking_ && elapsedMs(nowMs, kickStartMs_) >= kickMs_) kicking_ = false;
  lastOutput_ = kicking_ ? 100 : target_;
  return lastOutput_;
}

}  // namespace wineador
