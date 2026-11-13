#pragma once
// Small sums the BP2 sketches print, kept here so the Mac tests check them.

#include <stdint.h>

namespace wineador {

// BP2 test 27: the 12V scale factor from the meter's reading on TP6 ("12V") and the pin's
// millivolts at the same moment. 0 if the pin reads nothing.
inline float scaleFromMeter(float meterV, uint32_t pinMv) { return pinMv ? meterV / ((float)pinMv / 1000.0f) : 0.0f; }

// BP2 test 20: U8's input current from the drop across R21/R22 (3.4 ohm, 3A.3).
constexpr float kInrushOhms = 3.4f;
inline float u8InputAmps(float dropV) { return dropV / kInrushOhms; }

// BP2 test 22: the blower PWM value for a duty in percent, at `bits` resolution
// (100% = 2^bits = always high, 0 = always low).
inline uint32_t blowerDuty(uint8_t pct, uint8_t bits) {
  if (pct > 100) pct = 100;
  return (uint32_t)pct * (1u << bits) / 100u;
}

}  // namespace wineador
