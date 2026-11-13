#pragma once
// Rev Q's LCD (ST7796S, 320 x 480) on SPI2 / FSPI: GPIO10 CS, GPIO11 MOSI, GPIO12 SCK, GPIO9 D/C,
// GPIO48 reset — shared with the touch controller (spec 3A.9, 3A.11). The timings and commands
// are in lib/core/lcd_seq.h.
//
// begin() starts a small task on core 0 (priority 0, like the SD task) that resets the panel,
// sends the start-up commands, clears it to black and turns the display on. The reset waits
// and the ~165 ms of clearing happen there, never in the loop. Before the B7 screens that is
// all the firmware does with it (3A.11: cleared to black, backlight off).
//
// The drawing calls are for the BP2 test 29 sketch. They block the caller while they send
// (a full screen is ~165 ms at 15 MHz), so the firmware's loop never calls them.
// Rev N: no LCD; every call does nothing.

#include <stdint.h>

namespace lcd {

void begin();                      // BOOT: starts the reset + clear task
bool ready();                      // the boot clear has finished
bool fitted();
uint32_t resetReleasedMs();        // millis() when GPIO48 last went high (touch waits 300 ms from it)

// BP2 test 29 sketch only (they block; never from the firmware's loop):
void resetAndInit();               // GPIO48 pulse (LCD AND touch), start-up commands, black, display on
void fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t rgb565);
void colourBars();                 // the test pattern
void setSpiHz(uint32_t hz);        // 15 MHz normally; 20 MHz as test 29's trial

}  // namespace lcd
