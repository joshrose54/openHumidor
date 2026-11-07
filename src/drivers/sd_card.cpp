#include "sd_card.h"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "pins.h"

namespace sdcard {

static SPIClass gSpi(VSPI);
static bool gMounted = false;

bool begin() {
  gSpi.begin(pins::kSdSck, pins::kSdMiso, pins::kSdMosi, pins::kSdCs);
  gMounted = SD.begin(pins::kSdCs, gSpi, 4000000);
  return gMounted;
}

bool mounted() { return gMounted; }

uint64_t cardMB() { return gMounted ? SD.cardSize() / (1024ULL * 1024ULL) : 0; }

}  // namespace sdcard
