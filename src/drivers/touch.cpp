#include "touch.h"
#include <Arduino.h>
#include <Wire.h>
#include "wineador/lcd_seq.h"
#include "lcd.h"

namespace touch {

#if defined(WINEADOR_BOARD_REVQ)

static wineador::TouchSetup gSetup;

// One register write: one short I2C1 transaction.
static bool writeReg(uint8_t reg, uint8_t val) {
  Wire1.beginTransmission(wineador::kTouchAddr);
  Wire1.write(reg);
  Wire1.write(val);
  return Wire1.endTransmission() == 0;
}

static bool readRegs(uint8_t first, uint8_t* out, size_t n) {
  Wire1.beginTransmission(wineador::kTouchAddr);       // I2C1, started by i2cbus::begin
  Wire1.write(first);
  if (Wire1.endTransmission(false) != 0) return false;
  if (Wire1.requestFrom(wineador::kTouchAddr, n) != n) {
    while (Wire1.available()) Wire1.read();
    return false;
  }
  for (size_t i = 0; i < n; i++) out[i] = (uint8_t)Wire1.read();
  return true;
}

bool read(wineador::TouchPoint& t) {
  t = wineador::TouchPoint{};
  const uint32_t resets = lcd::resetCount();
  using N = wineador::TouchSetup::Next;
  const N next = gSetup.next(resets, lcd::resetReleasedMs(), millis());   // 3A.11: 300 ms after a reset
  if (next == N::Wait) return false;
  if (next == N::WriteCtrl)                            // after each reset: monitor mode off (3A.11)
    gSetup.written(resets, writeReg(wineador::kTouchRegCtrl, wineador::kTouchCtrlStayActive));
  uint8_t b[wineador::kTouchReadLen];
  if (!readRegs(wineador::kTouchFirstReg, b, wineador::kTouchReadLen)) return false;
  t = wineador::decodeTouch(b);
  return t.valid;
}

bool readReg(uint8_t reg, uint8_t& val) {
  if (gSetup.next(lcd::resetCount(), lcd::resetReleasedMs(), millis()) == wineador::TouchSetup::Next::Wait) return false;
  return readRegs(reg, &val, 1);
}

bool setupDone() { return gSetup.done(lcd::resetCount()); }
bool takeSetupFailure() { return gSetup.takeFailure(); }
bool fitted() { return true; }

#else

bool read(wineador::TouchPoint& t) { t = wineador::TouchPoint{}; return false; }
bool readReg(uint8_t, uint8_t&) { return false; }
bool setupDone() { return false; }
bool takeSetupFailure() { return false; }
bool fitted() { return false; }

#endif

}  // namespace touch
