#pragma once
// Rev Q's SD card power switch, GPIO39 (spec 3A.10, 3A.11). The order and timing are in
// lib/core/sd_power_seq.h; this does each step. On Rev N (no switch) every call does nothing.
//
//   begin()        BOOT, before the card is mounted: GPIO39 an input with its pull-up off —
//                  the soft-start "on" state (the card is already on: R91 holds Q10 on).
//   cycle()        SD TASK ONLY (it owns the card): release the bus, cut, 150 ms, soft on,
//                  10 ms, restart the bus. The caller then remounts. Blocks only the SD task
//                  (~160 ms, in vTaskDelay steps), never the loop.
//   requestOff() / requestOn() / update()   the same steps one at a time, for the BP2
//                  test 26 sketch.

#include <stdint.h>

namespace sdpower {

void begin();
bool fitted();
bool cycle();                  // true once the card is powered again and the bus restarted
uint32_t cycles();             // power cycles since boot

void requestOff(uint32_t nowMs);
void requestOn();
// Does whatever step is due. Returns true when it has just switched the card back on.
bool update(uint32_t nowMs);
bool powered();

}  // namespace sdpower
