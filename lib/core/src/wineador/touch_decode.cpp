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

}  // namespace wineador
