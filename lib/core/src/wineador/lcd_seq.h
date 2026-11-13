#pragma once
// Rev Q's LCD, lcdwiki MSP4031 (ST7796S, 320 x 480, 16-bit colour) on SPI2 (spec 3A.9, 3A.11):
// the plain-logic half — the timings, the start-up commands and the test pattern, as data, so
// the Mac tests can check them. The driver (src/drivers/lcd) sends them.
//
// GPIO48 resets the LCD AND the touch controller together (3A.11): low at least 5 ms, then at
// least 120 ms before Sleep Out and at least 300 ms before the first touch read.
// Before the B7 screens (3A.11): initialised at boot, cleared to black, backlight off.

#include <stdint.h>
#include <stddef.h>

namespace wineador {

constexpr uint32_t kLcdSpiHz = 15000000;          // 3A.9: 15 MHz or less (20 and 40 MHz: bench trials only)
static_assert(kLcdSpiHz <= 15000000, "LCD SPI above 15 MHz (spec 3A.9)");

constexpr uint16_t kLcdWidth = 320, kLcdHeight = 480;

// Reset timing (3A.11 minimums, with margin)
constexpr uint32_t kLcdResetLowMs = 10;           // >= 5 ms low
constexpr uint32_t kLcdResetToSleepOutMs = 150;   // >= 120 ms from release to Sleep Out
constexpr uint32_t kLcdResetToTouchMs = 350;      // >= 300 ms from release to the first touch read

// ST7796S commands used
constexpr uint8_t kLcdSleepOut = 0x11, kLcdDisplayOn = 0x29, kLcdColAddr = 0x2A, kLcdRowAddr = 0x2B,
                  kLcdMemWrite = 0x2C, kLcdMadctl = 0x36, kLcdPixelFormat = 0x3A, kLcdInvOff = 0x20,
                  kLcdInvOn = 0x21;

// One start-up step: a command, its argument bytes, and the wait after it.
struct LcdStep {
  uint8_t cmd;
  uint8_t nArgs;
  uint8_t args[4];
  uint16_t waitMs;
};

// After the hardware reset and its wait: Sleep Out (120 ms), portrait with BGR order,
// 16 bits a pixel. Display On comes after the screen is cleared (so nothing shows first).
constexpr LcdStep kLcdInit[] = {
  {kLcdSleepOut, 0, {0}, 120},                    // ST7796S: 120 ms after Sleep Out before Sleep In; 5 ms before commands
  {kLcdMadctl, 1, {0x48}, 0},                     // MX | BGR: portrait (the driver then sends its own orientation)
  {kLcdPixelFormat, 1, {0x55}, 0},                // 16 bits a pixel (RGB565)
  {kLcdInvOff, 0, {0}, 0},                        // colour inversion off (test 29 checks the colours)
};
constexpr size_t kLcdInitSteps = sizeof(kLcdInit) / sizeof(kLcdInit[0]);

// Orientation (spec 5.5: landscape 480 x 320, fixed). The panel is 320 x 480 portrait; MADCTL
// (0x36) turns it. The firmware runs one of the two landscape ways (180 degrees apart), chosen
// at BP2 test 29 to suit the enclosure and fixed in board.h (build log). Portrait is the BP2
// test 29 sketch's own (its colour bars and coordinates steps).
//   value: MY 0x80, MX 0x40, MV 0x20 (rows and columns exchanged), BGR 0x08.
enum class LcdOrientation : uint8_t { Portrait, LandscapeA, LandscapeB };
constexpr uint8_t lcdMadctl(LcdOrientation o) {
  return o == LcdOrientation::Portrait ? 0x48 : o == LcdOrientation::LandscapeA ? 0x28 : 0xE8;
}
constexpr uint16_t lcdWidth(LcdOrientation o) { return o == LcdOrientation::Portrait ? kLcdWidth : kLcdHeight; }
constexpr uint16_t lcdHeight(LcdOrientation o) { return o == LcdOrientation::Portrait ? kLcdHeight : kLcdWidth; }
const char* lcdOrientationName(LcdOrientation o);   // "portrait", "landscape A", "landscape B"

// RGB565 colours
constexpr uint16_t kBlack = 0x0000, kWhite = 0xFFFF, kRed = 0xF800, kGreen = 0x07E0, kBlue = 0x001F,
                   kYellow = 0xFFE0, kCyan = 0x07FF, kMagenta = 0xF81F;

// BP2 test 29's pattern: eight vertical bars, white yellow cyan green magenta red blue black.
uint16_t colourBarAt(uint16_t x, uint16_t y);

// Window bytes for CASET / RASET: start and end, big-endian.
void lcdWindowArgs(uint16_t first, uint16_t last, uint8_t out[4]);

}  // namespace wineador
