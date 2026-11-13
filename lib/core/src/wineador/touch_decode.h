#pragma once
// Rev Q's touch controller, FT6336U at 0x38 on I2C1 (spec 3A.7, 3A.9): decoding its registers.
// The driver reads 5 bytes from register 0x02: TD_STATUS, P1_XH, P1_XL, P1_YH, P1_YL
// (FocalTech FT6x36 register map; BP2 test 29 proves it on the board):
//   TD_STATUS bits 3-0: number of touch points (0, 1 or 2; anything else is not a valid read)
//   P1_XH bits 7-6: event flag; bits 3-0: X bits 11-8.   P1_XL: X bits 7-0
//   P1_YH bits 7-4: touch ID;   bits 3-0: Y bits 11-8.   P1_YL: Y bits 7-0
// Coordinates are the panel's own (320 x 480 portrait); the screen's orientation is B7's.

#include <stdint.h>

namespace wineador {

constexpr uint8_t kTouchAddr = 0x38;
constexpr uint8_t kTouchFirstReg = 0x02;
constexpr int kTouchReadLen = 5;

struct TouchPoint {
  bool valid = false;     // the read made sense
  uint8_t count = 0;      // touches seen (0 = no finger)
  uint16_t x = 0, y = 0;  // the first touch
};

TouchPoint decodeTouch(const uint8_t b[kTouchReadLen]);

}  // namespace wineador
