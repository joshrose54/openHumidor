#pragma once
// Rev Q's hardware watchdog on GPIO1 (spec 3A.5, 3A.11). The rules are in
// lib/core/wdi_toggle.h.
//
//   begin()   FIRST THING AT BOOT: GPIO1 an input with both internal pulls off — disabled.
//   start()   once the control loop is running (called from the first loop pass).
//   kick()    every loop pass, from the loop: changes the level. Never from a timer.
//   pause()   back to an input with both pulls off (never pad hold); resume() drives it again.

#include <stdint.h>

namespace hwdog {

void begin();
void start(uint32_t nowMs);
void kick(uint32_t nowMs);
void pause();
void resume(uint32_t nowMs);

bool fitted();                 // always true (Rev Q has one)
bool running();
uint32_t longestGapMs();       // the longest time between two changes since start
bool takeLongGap(uint32_t* gapMs);   // once per gap over 0.9 s (an event)

}  // namespace hwdog
