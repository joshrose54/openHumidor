#pragma once
// SD card on VSPI, CS on P17, 4 MHz — from soak_test.cpp.
// Stage 1: mount at boot and report. Logging (spec 5.4) arrives at stage 6.
//
// begin() is SLOW and BOOT ONLY: mounting talks to the card and can take hundreds
// of ms (longer with no card while it retries). The time it took is shown in the
// boot banner. It must never be called from the loop. Stage 6 writes go through a
// queue, flushed in short steps (spec 5.2).

#include <stdint.h>

namespace sdcard {

bool begin();          // BOOT ONLY, slow. Mount; false if no card or it will not mount
bool mounted();
uint64_t cardMB();     // card size from the card's stored details, 0 if not mounted

}  // namespace sdcard
