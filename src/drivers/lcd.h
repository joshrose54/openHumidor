#pragma once
// Rev Q's LCD (ST7796S, 320 x 480 panel, shown landscape 480 x 320) on SPI2: GPIO10 CS, GPIO11
// MOSI, GPIO12 SCK, GPIO9 D/C, GPIO48 reset - shared with the touch controller (spec 3A.9,
// 3A.11). The timings and commands are in lib/core/lcd_seq.h. Sent through ESP-IDF's esp_lcd
// SPI interface, by DMA.
//
// begin() starts a small task on core 0 (priority 0, like the SD task) that resets the panel,
// sends the start-up commands, clears it to black and turns the display on. The reset waits
// and the clearing happen there, never in the loop.
//
// After that the firmware's loop sends the screen in pieces (3A.11: no step holds the loop
// over 50 ms): queue() the rectangles the drawing changed, then step() once a pass. A step
// never waits: when the piece on the wire has gone it copies the next rows of the picture
// (in PSRAM) into a free internal-RAM buffer, swapping the bytes for the panel, and starts it
// by DMA, which runs on while the loop carries on. Two buffers of kSendBytes / 2 each.
//
// The blocking calls (fill, colourBars, pushFrame, setSpiHz, setOrientation) are for the BP2
// test 29 sketch; the firmware's loop never calls them.

#include <stdint.h>
#include <stddef.h>
#include "wineador/lcd_seq.h"
#include "wineador/lcd_layout.h"

namespace lcd {

// BOOT: allocates the two send buffers (internal DMA RAM, sendBytes in all), starts the reset +
// clear task. False if the buffers or the SPI bus couldn't be had (the LCD is then left dark).
bool begin(wineador::LcdOrientation o, size_t sendBytes);
bool ready();                      // the boot clear has finished
bool fitted();
uint32_t resetReleasedMs();        // millis() when GPIO48 last went high (touch waits 300 ms from it)
uint32_t resetCount();             // GPIO48 pulses so far (0 = none yet); the touch driver sets the chip up after each
wineador::LcdOrientation orientation();
size_t pieceBytes();               // one buffer's size

// ---- the firmware's loop
// The picture: w x h pixels, RGB565 in the CPU's byte order (lib/lcdgfx's Canvas).
void setFrame(const uint16_t* frame, uint16_t w, uint16_t h);
void queue(wineador::Rect r);      // to be sent; a full queue becomes one whole-screen rectangle
bool step();                       // one step (never waits); true while anything is left to send
bool idle();                       // nothing queued and nothing on the wire
uint32_t piecesSent();

// ---- BP2 test 29 sketch only (they block; never from the firmware's loop)
void resetAndInit();               // GPIO48 pulse (LCD AND touch), start-up commands, black, display on
void setOrientation(wineador::LcdOrientation o);   // MADCTL; the next drawing uses its size
void fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t rgb565);
void colourBars();                 // the test pattern (portrait)
void pushFrame();                  // the whole picture from setFrame(), now
void setSpiHz(uint32_t hz);        // kLcdSpiHz (13.3 MHz) normally; kLcdTrialSpiHz (20 MHz) as test 29's trial

}  // namespace lcd
