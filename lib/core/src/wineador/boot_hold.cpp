#include "wineador/boot_hold.h"

namespace wineador {

void BootHold::begin(uint32_t nowMs, uint32_t holdMs) {
  startMs_ = nowMs;
  holdMs_ = holdMs;
  remaining_ = holdMs;
  ended_ = (holdMs == 0);
}

void BootHold::update(uint32_t nowMs) {
  if (ended_) return;
  uint32_t el = elapsedMs(nowMs, startMs_);
  if (el >= holdMs_) {
    ended_ = true;
    remaining_ = 0;
  } else {
    remaining_ = holdMs_ - el;
  }
}

}  // namespace wineador
