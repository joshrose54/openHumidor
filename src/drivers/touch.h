#pragma once
// Rev Q's touch controller, FT6336U at 0x38 on I2C1 with the RTC (spec 3A.7, 3A.9). One short
// I2C1 transaction (~0.6 ms at 100 kHz); the decoding is lib/core/touch_decode.h. Its reset is
// GPIO48, shared with the LCD (lcd.h): no read until 300 ms after the reset is released.
// The firmware doesn't poll it before the B7 screens; this is for the BP2 test 29 sketch.

#include <stdint.h>
#include "wineador/touch_decode.h"

namespace touch {

// After every GPIO48 reset (boot, and test 29's pulse), the first read also writes 0x00 to
// register 0x86 - monitor mode off, so the chip keeps answering while idle (3A.11); the order is
// lib/core's TouchSetup. A failed write is tried again at the next read.
// False if not yet 300 ms since the reset, or the controller didn't answer.
bool read(wineador::TouchPoint& t);
bool readReg(uint8_t reg, uint8_t& val);   // one register (test 29: the ID and 0x86); false as read()
bool setupDone();                          // 0x86 written since the last reset
bool takeSetupFailure();                   // true once if the 0x86 write failed after a reset: log it
bool fitted();

}  // namespace touch
