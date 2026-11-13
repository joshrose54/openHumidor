#include "lcd.h"
#include <Arduino.h>
#include <SPI.h>
#include "wineador/lcd_seq.h"
#include "pins.h"

namespace lcd {

#if defined(WINEADOR_BOARD_REVQ)

using namespace wineador;

static SPIClass gSpi(FSPI);            // SPI2 (on the S3, Arduino's FSPI is SPI2); the SD card has SPI3
static uint32_t gHz = kLcdSpiHz;
static volatile bool gReady = false;
static volatile uint32_t gReleasedMs = 0;
static uint8_t gLine[kLcdWidth * 2];   // one row of pixels, internal RAM

static void cmd(uint8_t c, const uint8_t* args, uint8_t n) {
  gSpi.beginTransaction(SPISettings(gHz, MSBFIRST, SPI_MODE0));
  digitalWrite(pins::kLcdCs, LOW);
  digitalWrite(pins::kLcdDc, LOW);      // command
  gSpi.transfer(c);
  if (n) {
    digitalWrite(pins::kLcdDc, HIGH);   // its arguments
    gSpi.writeBytes(args, n);
  }
  digitalWrite(pins::kLcdCs, HIGH);
  gSpi.endTransaction();
}

static void window(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  uint8_t a[4];
  lcdWindowArgs(x, (uint16_t)(x + w - 1), a);
  cmd(kLcdColAddr, a, 4);
  lcdWindowArgs(y, (uint16_t)(y + h - 1), a);
  cmd(kLcdRowAddr, a, 4);
}

// Pixels row by row. `colourOf` gives each pixel; between rows the task lets others run.
template <typename F>
static void paint(uint16_t x, uint16_t y, uint16_t w, uint16_t h, F colourOf) {
  if (w == 0 || h == 0 || x >= kLcdWidth || y >= kLcdHeight) return;
  if (x + w > kLcdWidth) w = (uint16_t)(kLcdWidth - x);
  if (y + h > kLcdHeight) h = (uint16_t)(kLcdHeight - y);
  window(x, y, w, h);
  cmd(kLcdMemWrite, nullptr, 0);
  gSpi.beginTransaction(SPISettings(gHz, MSBFIRST, SPI_MODE0));
  digitalWrite(pins::kLcdCs, LOW);
  digitalWrite(pins::kLcdDc, HIGH);
  for (uint16_t r = 0; r < h; r++) {
    for (uint16_t c = 0; c < w; c++) {
      const uint16_t v = colourOf((uint16_t)(x + c), (uint16_t)(y + r));
      gLine[2 * c] = (uint8_t)(v >> 8);
      gLine[2 * c + 1] = (uint8_t)(v & 0xFF);
    }
    gSpi.writeBytes(gLine, (size_t)w * 2);
    if ((r & 15) == 15) {                 // every 16 rows: let the idle task and others run
      digitalWrite(pins::kLcdCs, HIGH);
      gSpi.endTransaction();
      vTaskDelay(1);
      gSpi.beginTransaction(SPISettings(gHz, MSBFIRST, SPI_MODE0));
      digitalWrite(pins::kLcdCs, LOW);
      digitalWrite(pins::kLcdDc, HIGH);
    }
  }
  digitalWrite(pins::kLcdCs, HIGH);
  gSpi.endTransaction();
}

// GPIO48 low >= 5 ms, high, >= 120 ms to Sleep Out (3A.11); then the start-up commands.
// Resets the touch controller too (one pin): its first read waits 300 ms from the release.
void resetAndInit() {
  digitalWrite(pins::kLcdTouchReset, LOW);
  vTaskDelay(pdMS_TO_TICKS(kLcdResetLowMs));
  digitalWrite(pins::kLcdTouchReset, HIGH);
  gReleasedMs = millis();
  vTaskDelay(pdMS_TO_TICKS(kLcdResetToSleepOutMs));
  for (size_t i = 0; i < kLcdInitSteps; i++) {
    cmd(kLcdInit[i].cmd, kLcdInit[i].args, kLcdInit[i].nArgs);
    if (kLcdInit[i].waitMs) vTaskDelay(pdMS_TO_TICKS(kLcdInit[i].waitMs));
  }
  paint(0, 0, kLcdWidth, kLcdHeight, [](uint16_t, uint16_t) { return kBlack; });
  cmd(kLcdDisplayOn, nullptr, 0);
}

static void bootTask(void*) {
  resetAndInit();
  gReady = true;
  vTaskDelete(nullptr);
}

void begin() {
  pinMode(pins::kLcdCs, OUTPUT);
  digitalWrite(pins::kLcdCs, HIGH);
  pinMode(pins::kLcdDc, OUTPUT);
  digitalWrite(pins::kLcdDc, HIGH);
  pinMode(pins::kLcdTouchReset, OUTPUT);
  digitalWrite(pins::kLcdTouchReset, HIGH);
  gSpi.begin(pins::kLcdSck, -1, pins::kLcdMosi, -1);   // no MISO: the screen is never read (3A.4)
  // Core 0, priority 0: shares core 0 with the idle task (which the watchdog watches) and the
  // SD task; WiFi runs above it. Runs once, then deletes itself.
  xTaskCreatePinnedToCore(bootTask, "lcd", 3072, nullptr, 0, nullptr, 0);
}

bool ready() { return gReady; }
bool fitted() { return true; }
uint32_t resetReleasedMs() { return gReleasedMs; }

void fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t rgb565) {
  paint(x, y, w, h, [rgb565](uint16_t, uint16_t) { return rgb565; });
}

void colourBars() { paint(0, 0, kLcdWidth, kLcdHeight, [](uint16_t x, uint16_t y) { return colourBarAt(x, y); }); }

void setSpiHz(uint32_t hz) { gHz = hz; }

#else   // Rev N: no LCD

void begin() {}
bool ready() { return false; }
bool fitted() { return false; }
uint32_t resetReleasedMs() { return 0; }
void resetAndInit() {}
void fill(uint16_t, uint16_t, uint16_t, uint16_t, uint16_t) {}
void colourBars() {}
void setSpiHz(uint32_t) {}

#endif

}  // namespace lcd
