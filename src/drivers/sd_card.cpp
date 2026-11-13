#include "sd_card.h"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "pins.h"
#include "sd_power.h"

namespace sdcard {

#if defined(WINEADOR_BOARD_REVQ)
static SPIClass gSpi(HSPI);   // Rev Q: SPI3 (3A.10). On the S3, Arduino's HSPI is SPI3; SPI2 (FSPI) is the LCD's
#else
static SPIClass gSpi(VSPI);   // Rev N: VSPI
#endif
static bool gMounted = false;
static uint64_t gMB = 0;

// BOOT ONLY — see sd_card.h. Refuses to run a second time.
bool begin() {
  static bool ran = false;
  if (ran) return gMounted;
  ran = true;
  sdpower::begin();     // Rev Q: GPIO39 input, pull-up off - the soft-start "on" state (3A.11). Rev N: nothing
  gSpi.begin(pins::kSdSck, pins::kSdMiso, pins::kSdMosi, pins::kSdCs);
  gMounted = SD.begin(pins::kSdCs, gSpi, kSpiHz);
  gMB = gMounted ? SD.cardSize() / (1024ULL * 1024ULL) : 0;
  return gMounted;
}

bool mounted() { return gMounted; }

uint64_t cardMB() { return gMB; }

SPIClass& spi() { return gSpi; }

}  // namespace sdcard
