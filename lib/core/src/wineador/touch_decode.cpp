#include "wineador/touch_decode.h"

namespace wineador {

TouchPoint decodeTouch(const uint8_t b[kTouchReadLen]) {
  TouchPoint t;
  const uint8_t n = b[0] & 0x0F;
  if (n > 2) return t;                       // not a valid read: valid stays false
  t.valid = true;
  t.count = n;
  if (n == 0) return t;
  t.x = (uint16_t)(((b[1] & 0x0F) << 8) | b[2]);
  t.y = (uint16_t)(((b[3] & 0x0F) << 8) | b[4]);
  return t;
}

TouchSetup::Next TouchSetup::next(uint32_t resets, uint32_t releasedMs, uint32_t nowMs) const {
  if (resets == 0 || nowMs - releasedMs < kLcdResetToTouchMs) return Next::Wait;
  return doneFor_ == resets ? Next::Read : Next::WriteCtrl;
}

void TouchSetup::written(uint32_t resets, bool ok) {
  if (ok) { doneFor_ = resets; return; }
  if (failedFor_ != resets) { failedFor_ = resets; failPending_ = true; }
}

bool TouchSetup::takeFailure() {
  const bool f = failPending_;
  failPending_ = false;
  return f;
}

void touchToScreen(LcdOrientation o, uint16_t px, uint16_t py, int* sx, int* sy) {
  const int x = px > kLcdWidth - 1 ? kLcdWidth - 1 : px;    // clamp to the panel
  const int y = py > kLcdHeight - 1 ? kLcdHeight - 1 : py;
  switch (o) {
    case LcdOrientation::Portrait: *sx = x; *sy = y; break;
    case LcdOrientation::LandscapeA: *sx = y; *sy = (kLcdWidth - 1) - x; break;
    case LcdOrientation::LandscapeB: *sx = (kLcdHeight - 1) - y; *sy = x; break;
  }
}

}  // namespace wineador
