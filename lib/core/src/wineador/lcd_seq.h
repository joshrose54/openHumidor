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

// SPI clock (3A.9: 15 MHz or less; the ST7796S's write cycle is at least 66 ns). The S3 makes its
// SPI clock as 80 MHz divided by a whole number and ESP-IDF picks the divider NEAREST the speed
// asked for, above or below: asking for 15 MHz gives 80 / 5 = 16 MHz. So we ask for 80 / 6.
constexpr uint32_t kSpiSourceHz = 80000000;       // the S3's APB clock, ESP-IDF's default SPI source
constexpr uint32_t kLcdSpiHz = 13333333;          // 80 MHz / 6 = 13.3 MHz
constexpr uint32_t kLcdTrialSpiHz = 20000000;     // BP2 test 29's trial only: 80 MHz / 4

// The clock the S3 really makes for a requested speed: the same arithmetic as ESP-IDF 5.5's
// spi_ll_master_cal_clock (hal/esp32s3/include/hal/spi_ll.h): above 3/4 of the source, the source
// itself; otherwise the pre x n divider (pre 1-16, n 2-64) nearest the request, ties to the
// higher n.
constexpr uint32_t s3SpiClockHz(uint32_t sourceHz, uint32_t hz) {
  if (hz > (sourceHz / 4) * 3) return sourceHz;
  int64_t bestErr = -1;
  uint32_t best = 0;
  for (uint32_t n = 2; n <= 64; n++) {
    uint32_t pre = ((sourceHz / n) + (hz / 2)) / hz;
    if (pre == 0) pre = 1;
    if (pre > 16) pre = 16;
    const uint32_t f = sourceHz / (pre * n);
    const int64_t err = f > hz ? (int64_t)f - hz : (int64_t)hz - f;
    if (bestErr < 0 || err <= bestErr) { bestErr = err; best = f; }
  }
  return best;
}
static_assert(s3SpiClockHz(kSpiSourceHz, kLcdSpiHz) <= 15000000, "the LCD's real SPI clock is above 15 MHz (spec 3A.9)");

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
constexpr uint8_t kLcdMaxArgs = 14;               // the gamma tables
struct LcdStep {
  uint8_t cmd;
  uint8_t nArgs;
  uint8_t args[kLcdMaxArgs];
  uint16_t waitMs;
};

// After the hardware reset and its wait (3A.11): Sleep Out and its 120 ms, then the maker's
// register list, then (in the driver) the orientation, the screen cleared to black, Display On.
//
// The register list is lcdwiki's ST7796_Init.txt for the MSP4031 (the module page's start-up
// file; the plain ESP32 demo, Simple_test.ino, sends the same), in its order and with its values,
// with one fix: the file sends 0xC2 (Power Control 3) and then 0xA7 as a COMMAND; 0xA7 is not a
// command, it is C2's parameter (as the maker's TFT_eSPI copy in the same package sends it).
// Left out of the list: the file's own Sleep Out and Display On at its end (ours come where
// 3A.11 puts them). Added at the end: inversion off, already the reset state, sent so the
// colours never depend on it.
constexpr LcdStep kLcdInit[] = {
  {kLcdSleepOut, 0, {0}, 120},                    // ST7796S p160: 5 ms before any command; 120 ms before Sleep In
  // ---- lcdwiki ST7796_Init.txt (MSP4031)
  {0xF0, 1, {0xC3}, 0},                           // Command Set Control: command 2 part I on
  {0xF0, 1, {0x96}, 0},                           // command 2 part II on
  {kLcdMadctl, 1, {0x48}, 0},                     // MX | BGR: portrait (the driver then sends its own orientation)
  {kLcdPixelFormat, 1, {0x05}, 0},                // 16 bits a pixel on the SPI ("control interface", p190)
  {0xB0, 1, {0x80}, 0},                           // Interface Mode: SDA in only (no MISO)
  {0xB6, 2, {0x00, 0x02}, 0},                     // Display Function Control
  {0xB5, 4, {0x02, 0x03, 0x00, 0x04}, 0},         // Blanking Porch
  {0xB1, 2, {0x80, 0x10}, 0},                     // Frame Rate
  {0xB4, 1, {0x00}, 0},                           // Display Inversion Control (the drive scheme, not the colours)
  {0xB7, 1, {0xC6}, 0},                           // Entry Mode
  {0xC5, 1, {0x1C}, 0},                           // VCOM
  {0xE4, 1, {0x31}, 0},                           // (not in the Sitronix datasheet; the maker sends it)
  {0xE8, 8, {0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33}, 0},   // Display Output Ctrl Adjust
  {0xC2, 1, {0xA7}, 0},                           // Power Control 3: 0xA7 as its parameter (the file's slip fixed)
  {0xE0, 14, {0xF0, 0x09, 0x13, 0x12, 0x12, 0x2B, 0x3C, 0x44, 0x4B, 0x1B, 0x18, 0x17, 0x1D, 0x21}, 0},   // positive gamma
  {0xE1, 14, {0xF0, 0x09, 0x13, 0x0C, 0x0D, 0x27, 0x3B, 0x44, 0x4D, 0x0B, 0x17, 0x17, 0x1D, 0x21}, 0},   // negative gamma
  {0xF0, 1, {0x3C}, 0},                           // command 2 part I off
  {0xF0, 1, {0x69}, 0},                           // command 2 part II off
  {0x13, 0, {0}, 0},                              // Normal Display Mode On
  // ---- end of the maker's list
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
