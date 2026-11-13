#include "touch.h"
#include <Arduino.h>
#include <Wire.h>
#include "wineador/lcd_seq.h"
#include "lcd.h"

namespace touch {

#if defined(WINEADOR_BOARD_REVQ)

bool read(wineador::TouchPoint& t) {
  t = wineador::TouchPoint{};
  if (millis() - lcd::resetReleasedMs() < wineador::kLcdResetToTouchMs) return false;   // 3A.11: 300 ms
  Wire1.beginTransmission(wineador::kTouchAddr);       // I2C1, started by i2cbus::begin
  Wire1.write(wineador::kTouchFirstReg);
  if (Wire1.endTransmission(false) != 0) return false;
  uint8_t b[wineador::kTouchReadLen];
  if (Wire1.requestFrom(wineador::kTouchAddr, (size_t)wineador::kTouchReadLen) != (size_t)wineador::kTouchReadLen) {
    while (Wire1.available()) Wire1.read();
    return false;
  }
  for (int i = 0; i < wineador::kTouchReadLen; i++) b[i] = (uint8_t)Wire1.read();
  t = wineador::decodeTouch(b);
  return t.valid;
}

bool fitted() { return true; }

#else

bool read(wineador::TouchPoint& t) { t = wineador::TouchPoint{}; return false; }
bool fitted() { return false; }

#endif

}  // namespace touch
