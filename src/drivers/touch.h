#pragma once
// Rev Q's touch controller, FT6336U at 0x38 on I2C1 with the RTC (spec 3A.7, 3A.9). One short
// I2C1 transaction (~0.6 ms at 100 kHz); the decoding is lib/core/touch_decode.h. Its reset is
// GPIO48, shared with the LCD (lcd.h): no read until 300 ms after the reset is released.
// The firmware doesn't poll it before the B7 screens; this is for the BP2 test 29 sketch.
// Rev N: no touch.

#include <stdint.h>
#include "wineador/touch_decode.h"

namespace touch {

// False if not yet 300 ms since the reset, or the controller didn't answer.
bool read(wineador::TouchPoint& t);
bool fitted();

}  // namespace touch
