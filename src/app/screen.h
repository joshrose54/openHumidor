#pragma once
// The LCD screens in the firmware (spec 5.5), Rev Q. Ties together the plain logic
// (lib/core lcd_ui: what to show, what a touch does), the drawing (lib/lcdgfx: the picture, in
// PSRAM) and the drivers (lcd: sent by DMA in pieces; touch; backlight).
//
// update() runs once a pass from the loop and never waits (3A.11). Each pass it does at most:
//   - one touch read (every 50 ms; I2C1, ~0.6 ms), from the loop like the RTC reads;
//   - the screen logic (every 100 ms, or at once after a touch);
//   - one drawing step: one changed element into the picture (at most kMaxStepPixels);
//   - one sending step: the next piece by DMA (lcd.h), which then runs on by itself.
// Each part is timed; the worst are in the status line, and a pass whose screen work took over
// 50 ms is an event. The backlight stays off until the first walk-past screen has been sent,
// then full, dimmed after 60 s without a touch (3A.11, 5.5).
// Rev N: no LCD; everything does nothing.

#include <stdint.h>
#include <stddef.h>
#include "wineador/lcd_ui.h"

namespace screen {

// BOOT ONLY: the picture (PSRAM, 307,200 bytes) and the LCD's send buffers (internal RAM,
// board::kLcdSendBytes), before the heap baseline. False (and the LCD stays dark) if either
// can't be had; control is unaffected.
bool begin(uint32_t nowMs);
bool running();
const char* problem();                 // why it isn't running, or nullptr

void update(const wineador::UiInputs& in);

bool takeSave(float* targetC, float* rhPct);
bool takeWindowOpened();
wineador::UpdateWindow& window();      // Q5's OTA asks it (updateAllowed)

// Passes whose screen work took over 50 ms (3A.11): at most once a minute, the worst one's time,
// the part that took longest, and how many there were since the last time.
bool takeLongStep(uint32_t* us, const char** what, uint32_t* count);

void stress(bool on);                  // TEST BUILDS: redraw the whole screen every second
bool stressing();

struct Stats {
  uint32_t touchMaxUs = 0, logicMaxUs = 0, drawMaxUs = 0, sendMaxUs = 0, passMaxUs = 0;
  uint32_t lastFrameMs = 0, maxFrameMs = 0, frames = 0;
  uint32_t touchFails = 0;
};
Stats stats();
void resetMax();                       // worst figures since the last status line
// "lcd draw 2.1 send 0.2 touch 0.7 ms max | frame 190 ms" (empty without an LCD)
void formatStatus(char* out, size_t n);

}  // namespace screen
