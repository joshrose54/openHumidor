#pragma once
// SD card: Rev N on VSPI (CS P17), Rev Q on SPI3 (CS GPIO42) — pins.h. From soak_test.cpp (which ran at 4 MHz; now kSpiHz).
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

// SPI clock for the card, for the boot mount and every remount. 1 MHz: two large
// (~7 KB) batch writes failed at 4 MHz during B4 night 1 while every small write
// worked; logging needs well under 1 KB a minute, so the lower clock costs nothing.
// (The card itself always starts up at 400 kHz.)
constexpr uint32_t kSpiHz = 1000000;

bool begin();          // BOOT ONLY, slow. Mount; false if no card or it will not mount
bool mounted();
uint64_t cardMB();     // card size found at boot (cached), 0 if not mounted

SPIClass& spi();       // the bus the card is on (for the logging task's remounts)

}  // namespace sdcard
