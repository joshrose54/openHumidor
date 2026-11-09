#include "sd_card.h"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "pins.h"

namespace sdcard {

static SPIClass gSpi(VSPI);
static bool gMounted = false;
static uint64_t gMB = 0;

// BOOT ONLY — see sd_card.h. Refuses to run a second time.
bool begin() {
  static bool ran = false;
  if (ran) return gMounted;
  ran = true;
  gSpi.begin(pins::kSdSck, pins::kSdMiso, pins::kSdMosi, pins::kSdCs);
  gMounted = SD.begin(pins::kSdCs, gSpi, kSpiHz);
  gMB = gMounted ? SD.cardSize() / (1024ULL * 1024ULL) : 0;
  return gMounted;
}

bool mounted() { return gMounted; }

uint64_t cardMB() { return gMB; }

SPIClass& spi() { return gSpi; }

}  // namespace sdcard
