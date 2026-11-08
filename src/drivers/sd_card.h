#pragma once
// SD card on VSPI, CS on P17, 4 MHz — from soak_test.cpp.
// Mount at boot and report. From stage 6 the SD logging task (sd_log) owns the card
// after boot: nothing else may call the SD library.
//
// begin() is SLOW and BOOT ONLY: mounting talks to the card and can take hundreds
// of ms (longer with no card while it retries). The time it took is shown in the
// boot banner. It must never be called from the loop. Stage 6 writes go through a
// queue, flushed in short steps (spec 5.2).

#include <stdint.h>
#include <SPI.h>

namespace sdcard {

bool begin();          // BOOT ONLY, slow. Mount; false if no card or it will not mount
bool mounted();
uint64_t cardMB();     // card size found at boot (cached), 0 if not mounted

SPIClass& spi();       // the bus the card is on (for the logging task's remounts)

}  // namespace sdcard
