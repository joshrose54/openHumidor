#pragma once
// Heater PWM cap (spec 4.5, 5.5). Starting value 100 %: the heater runs at full
// duty while on. Below 100 % it is switched slowly — on for cap % of each 1 s
// period, counted from when the heater went on. This only gates the heater pin
// while the heater is ON; the guards and the interlock see the heater as on for
// the whole run, so min on / min off / max run are unaffected.

#include "wineador/types.h"

namespace wineador {

inline bool heaterPinOn(uint32_t nowMs, uint32_t onSinceMs, uint8_t capPct, uint32_t periodMs) {
  if (capPct >= 100) return true;
  if (capPct == 0 || periodMs == 0) return false;
  const uint32_t pos = elapsedMs(nowMs, onSinceMs) % periodMs;
  return pos < (uint32_t)((uint64_t)periodMs * capPct / 100);
}

}  // namespace wineador
