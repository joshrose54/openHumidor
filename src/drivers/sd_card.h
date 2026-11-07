#pragma once
// SD card on VSPI, CS on P17, 4 MHz — from soak_test.cpp.
// Stage 1: mount at boot and report. Logging (spec 5.4) arrives at stage 6.

#include <stdint.h>

namespace sdcard {

bool begin();          // mount; false if no card or it will not mount
bool mounted();
uint64_t cardMB();     // card size, 0 if not mounted

}  // namespace sdcard
