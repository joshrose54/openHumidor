#pragma once
// Rev Q's touch controller, FT6336U at 0x38 on I2C1 (spec 3A.7, 3A.9): decoding its registers.
// The driver reads 5 bytes from register 0x02: TD_STATUS, P1_XH, P1_XL, P1_YH, P1_YL
// (FocalTech FT6x36 register map; BP2 test 29 proves it on the board):
//   TD_STATUS bits 3-0: number of touch points (0, 1 or 2; anything else is not a valid read)
//   P1_XH bits 7-6: event flag; bits 3-0: X bits 11-8.   P1_XL: X bits 7-0
//   P1_YH bits 7-4: touch ID;   bits 3-0: Y bits 11-8.   P1_YL: Y bits 7-0
// Coordinates are the panel's own (320 x 480 portrait). touchToScreen() turns them into the
// screen's coordinates for each orientation (lcd_seq.h).

#include <stdint.h>
#include "wineador/lcd_seq.h"

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

// Monitor mode off (3A.11). The FT6336U drops into "monitor mode" after 30 s without a touch
// (register 0x86 = 0x01 and 0x87 = 30 s by default, lcdwiki's FT6336U register table), and its
// datasheet (section 2.3) says the serial port is closed then. Writing 0x00 to 0x86 keeps it
// active. A reset (GPIO48) puts the default back, so it is written after every reset.
constexpr uint8_t kTouchRegCtrl = 0x86;           // ID_G_CTRL
constexpr uint8_t kTouchCtrlStayActive = 0x00;

// The chip's ID registers and what an FT6336 reads (lcdwiki's FT6336 drivers check these three).
constexpr uint8_t kTouchRegVendorId = 0xA8, kTouchRegChipMid = 0x9F, kTouchRegChipHigh = 0xA3;
constexpr uint8_t kTouchVendorId = 0x11, kTouchChipMid = 0x26, kTouchChipHigh = 0x64;
constexpr bool isFt6336(uint8_t vendor, uint8_t mid, uint8_t high) {
  return vendor == kTouchVendorId && mid == kTouchChipMid && high == kTouchChipHigh;
}

// What the touch driver does at each poll, after each reset. resets counts the GPIO48 pulses
// (0 = none yet, so nothing is read); releasedMs is when the last one ended.
//   Wait: under kLcdResetToTouchMs since the reset (or no reset yet): no I2C at all.
//   WriteCtrl: write 0x00 to 0x86, then read as usual. Until a write succeeds it is tried
//     again at every poll; the FIRST failure after each reset is reported once (takeFailure).
//   Read: just read.
class TouchSetup {
 public:
  enum class Next : uint8_t { Wait, WriteCtrl, Read };
  Next next(uint32_t resets, uint32_t releasedMs, uint32_t nowMs) const;
  void written(uint32_t resets, bool ok);
  bool takeFailure();                 // true once for a failed write after a reset
  bool done(uint32_t resets) const { return resets != 0 && doneFor_ == resets; }

 private:
  uint32_t doneFor_ = 0;              // the reset the write succeeded after
  uint32_t failedFor_ = 0;            // the reset a failure was last reported for
  bool failPending_ = false;
};

// Panel coordinates -> screen coordinates for an orientation (lcd_seq.h). Assumes the touch
// panel's x,y are the portrait pixel x,y (MADCTL 0x48); BP2 test 29's corner check proves it.
//   portrait: unchanged · landscape A (MADCTL 0x28): X = y, Y = 319 - x
//   landscape B (0xE8, turned 180 degrees from A): X = 479 - y, Y = x
void touchToScreen(LcdOrientation o, uint16_t px, uint16_t py, int* sx, int* sy);

}  // namespace wineador
