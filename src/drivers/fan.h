#pragma once
// Plenum fan: PWM on P25 and the tach on P34 — from soak_test.cpp (spec 5.3).
// The duty to drive comes from wineador::FanController (kickstart, 1-19% refused).

#include <stdint.h>

namespace fan {

void begin();                          // BOOT ONLY. Tach input + interrupt, then ledcAttach (P25 untouched until here)
void setDuty(uint8_t pct);             // writes only when it changes
uint8_t duty();
void update(uint32_t nowMs, uint32_t windowMs);   // works out RPM once per window
uint32_t rpm();                        // last window; exactly 0 for a stopped fan
uint32_t pulsesTotal();                // accepted pulses (low >= 1 ms)
uint32_t transitionsTotal();           // every edge, chatter included

}  // namespace fan
