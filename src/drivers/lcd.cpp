#include "lcd.h"
#include <Arduino.h>
#include "pins.h"

#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_spi.h"

namespace lcd {

using wineador::LcdOrientation;
using wineador::Rect;

using namespace wineador;

static esp_lcd_panel_io_handle_t gIo = nullptr;
static uint32_t gHz = kLcdSpiHz;
static LcdOrientation gOrient = LcdOrientation::Portrait;
static volatile bool gReady = false;
static volatile uint32_t gReleasedMs = 0;
static volatile uint32_t gResets = 0;      // GPIO48 pulses so far (the touch driver sets the chip up after each)
static bool gBusUp = false;

// Two DMA buffers in internal RAM (3A.11: the internal-heap floor; Josh: about 8 KB in all).
static uint8_t* gBuf[2] = {nullptr, nullptr};
static size_t gBufBytes = 0;
static volatile bool gWireDone = true;      // set by the DMA-done callback
static int gOnWire = -1;                    // buffer being sent, or -1
static int gReadyBuf = -1;                  // buffer filled with the next piece, or -1
static Rect gReadyWin;                      // that piece's window
static uint32_t gPieces = 0;

// The picture and the queue of rectangles to send.
static const uint16_t* gFrame = nullptr;
static uint16_t gFrameW = 0, gFrameH = 0;
static constexpr int kQueue = 24;
static Rect gQ[kQueue];
static int gQHead = 0, gQLen = 0;
static Rect gCur;                           // the rectangle being sent...
static int16_t gCurRow = 0;                 // ...and its next row
static bool gHaveCur = false;

static bool IRAM_ATTR onDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
  gWireDone = true;
  return false;
}

static bool makeIo() {
  esp_lcd_panel_io_spi_config_t cfg = {};
  cfg.cs_gpio_num = pins::kLcdCs;
  cfg.dc_gpio_num = pins::kLcdDc;
  cfg.spi_mode = 0;
  cfg.pclk_hz = gHz;
  cfg.trans_queue_depth = 4;
  cfg.on_color_trans_done = onDone;
  cfg.lcd_cmd_bits = 8;
  cfg.lcd_param_bits = 8;
  return esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &cfg, &gIo) == ESP_OK;
}

static void cmd(uint8_t c, const uint8_t* args, uint8_t n) { esp_lcd_panel_io_tx_param(gIo, c, args, n); }

static void window(const Rect& r) {
  uint8_t a[4];
  lcdWindowArgs((uint16_t)r.x, (uint16_t)(r.x + r.w - 1), a);
  cmd(kLcdColAddr, a, 4);
  lcdWindowArgs((uint16_t)r.y, (uint16_t)(r.y + r.h - 1), a);
  cmd(kLcdRowAddr, a, 4);
}

static void startPiece(int b, const Rect& win) {
  window(win);
  gWireDone = false;
  gOnWire = b;
  esp_lcd_panel_io_tx_color(gIo, kLcdMemWrite, gBuf[b], (size_t)win.w * win.h * 2);
  gPieces++;
}

// Blocking: wait for the piece on the wire (sketch and boot task only).
static void waitWire() {
  while (gOnWire >= 0 && !gWireDone) vTaskDelay(1);
  gOnWire = -1;
}

// Blocking piece-by-piece paint from a colour function (boot clear, sketch drawing).
template <typename F>
static void paintBlocking(Rect r, F colourOf) {
  const uint16_t w = lcdWidth(gOrient), h = lcdHeight(gOrient);
  if (r.empty() || r.x >= w || r.y >= h) return;
  if (r.x + r.w > w) r.w = (int16_t)(w - r.x);
  if (r.y + r.h > h) r.h = (int16_t)(h - r.y);
  const int16_t rows = (int16_t)(gBufBytes / ((size_t)r.w * 2));
  int b = 0;
  for (int16_t y = r.y; y < r.y + r.h; y = (int16_t)(y + rows)) {
    const int16_t n = (int16_t)((y + rows <= r.y + r.h) ? rows : (r.y + r.h - y));
    uint8_t* p = gBuf[b];
    for (int16_t yy = 0; yy < n; yy++)
      for (int16_t x = 0; x < r.w; x++) {
        const uint16_t v = colourOf((uint16_t)(r.x + x), (uint16_t)(y + yy));
        *p++ = (uint8_t)(v >> 8);
        *p++ = (uint8_t)(v & 0xFF);
      }
    waitWire();
    startPiece(b, Rect(r.x, y, r.w, n));
    b ^= 1;
  }
  waitWire();
}

// GPIO48 low >= 5 ms, high, >= 120 ms to Sleep Out (3A.11); then the start-up commands and the
// orientation. Resets the touch controller too (one pin): its first read waits 300 ms.
void resetAndInit() {
  if (!gIo) return;
  waitWire();
  digitalWrite(pins::kLcdTouchReset, LOW);
  vTaskDelay(pdMS_TO_TICKS(kLcdResetLowMs));
  digitalWrite(pins::kLcdTouchReset, HIGH);
  gReleasedMs = millis();
  gResets = gResets + 1;
  vTaskDelay(pdMS_TO_TICKS(kLcdResetToSleepOutMs));
  for (size_t i = 0; i < kLcdInitSteps; i++) {
    cmd(kLcdInit[i].cmd, kLcdInit[i].args, kLcdInit[i].nArgs);
    if (kLcdInit[i].waitMs) vTaskDelay(pdMS_TO_TICKS(kLcdInit[i].waitMs));
  }
  const uint8_t m = lcdMadctl(gOrient);
  cmd(kLcdMadctl, &m, 1);
  paintBlocking(Rect(0, 0, (int16_t)lcdWidth(gOrient), (int16_t)lcdHeight(gOrient)), [](uint16_t, uint16_t) { return kBlack; });
  cmd(kLcdDisplayOn, nullptr, 0);
}

static void bootTask(void*) {
  resetAndInit();
  gReady = true;
  vTaskDelete(nullptr);
}

bool begin(LcdOrientation o, size_t sendBytes) {
  gOrient = o;
  pinMode(pins::kLcdTouchReset, OUTPUT);
  digitalWrite(pins::kLcdTouchReset, HIGH);
  // BOOT ONLY: the send buffers, internal DMA-capable RAM (an ordinary malloc over 4 KB would go
  // to PSRAM), once; nothing is allocated after this.
  gBufBytes = sendBytes / 2;
  gBufBytes -= gBufBytes % 4;
  if (gBufBytes < (size_t)lcdHeight(LcdOrientation::Portrait) * 2) return false;   // under one 480-pixel row: refuse
  for (int i = 0; i < 2; i++) gBuf[i] = (uint8_t*)heap_caps_malloc(gBufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (!gBuf[0] || !gBuf[1]) return false;
  spi_bus_config_t bus = {};
  bus.mosi_io_num = pins::kLcdMosi;
  bus.miso_io_num = -1;                    // no MISO: the screen is never read (3A.4)
  bus.sclk_io_num = pins::kLcdSck;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = (int)gBufBytes + 16;
  if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;
  gBusUp = true;
  if (!makeIo()) return false;
  // Core 0, priority 0: shares core 0 with the idle task (which the watchdog watches) and the
  // SD task; WiFi runs above it. Its waits are vTaskDelay, so others run. Then it deletes itself.
  xTaskCreatePinnedToCore(bootTask, "lcd", 3072, nullptr, 0, nullptr, 0);
  return true;
}

bool ready() { return gReady; }
bool fitted() { return true; }
uint32_t resetReleasedMs() { return gReleasedMs; }
uint32_t resetCount() { return gResets; }
LcdOrientation orientation() { return gOrient; }
size_t pieceBytes() { return gBufBytes; }
uint32_t piecesSent() { return gPieces; }

void setFrame(const uint16_t* frame, uint16_t w, uint16_t h) {
  gFrame = frame;
  gFrameW = w;
  gFrameH = h;
}

void queue(Rect r) {
  if (r.empty()) return;
  if (gQLen == kQueue) {                   // full: send the whole screen once instead
    gQHead = 0;
    gQLen = 1;
    gQ[0] = Rect(0, 0, (int16_t)gFrameW, (int16_t)gFrameH);
    return;
  }
  gQ[(gQHead + gQLen) % kQueue] = r;
  gQLen++;
}

// The next piece of the current rectangle, into buffer b. False if nothing is left.
static bool fillNext(int b) {
  if (!gHaveCur) {
    if (gQLen == 0) return false;
    gCur = gQ[gQHead];
    gQHead = (gQHead + 1) % kQueue;
    gQLen--;
    gCurRow = gCur.y;
    gHaveCur = true;
  }
  const int16_t rowsPer = (int16_t)(gBufBytes / ((size_t)gCur.w * 2));
  const int16_t end = (int16_t)(gCur.y + gCur.h);
  const int16_t n = (int16_t)(gCurRow + rowsPer <= end ? rowsPer : end - gCurRow);
  uint16_t* out = (uint16_t*)gBuf[b];
  for (int16_t y = 0; y < n; y++) {
    const uint16_t* src = gFrame + (int32_t)(gCurRow + y) * gFrameW + gCur.x;
    for (int16_t x = 0; x < gCur.w; x++) *out++ = __builtin_bswap16(src[x]);   // the panel wants high byte first
  }
  gReadyWin = Rect(gCur.x, gCurRow, gCur.w, n);
  gReadyBuf = b;
  gCurRow = (int16_t)(gCurRow + n);
  if (gCurRow >= end) gHaveCur = false;
  return true;
}

bool step() {
  if (!gReady || !gIo || !gFrame) return false;
  if (gOnWire >= 0 && gWireDone) gOnWire = -1;
  if (gReadyBuf < 0) fillNext(gOnWire == 0 ? 1 : 0);
  if (gOnWire < 0 && gReadyBuf >= 0) {
    const int b = gReadyBuf;
    gReadyBuf = -1;
    startPiece(b, gReadyWin);               // nothing on the wire: the window commands don't wait
    fillNext(b ^ 1);                        // the next piece ready while this one goes
  }
  return !idle();
}

bool idle() { return gQLen == 0 && !gHaveCur && gReadyBuf < 0 && (gOnWire < 0 || gWireDone); }

void setOrientation(LcdOrientation o) {
  gOrient = o;
  if (!gIo) return;
  waitWire();
  const uint8_t m = lcdMadctl(o);
  cmd(kLcdMadctl, &m, 1);
}

void fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t rgb565) {
  if (!gIo) return;
  paintBlocking(Rect((int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h), [rgb565](uint16_t, uint16_t) { return rgb565; });
}

void colourBars() {
  if (!gIo) return;
  paintBlocking(Rect(0, 0, (int16_t)lcdWidth(gOrient), (int16_t)lcdHeight(gOrient)),
                [](uint16_t x, uint16_t y) { return colourBarAt(x, y); });
}

void pushFrame() {
  if (!gIo || !gFrame) return;
  queue(Rect(0, 0, (int16_t)gFrameW, (int16_t)gFrameH));
  const bool was = gReady;
  gReady = true;
  while (step()) vTaskDelay(1);
  gReady = was;
}

void setSpiHz(uint32_t hz) {
  if (!gIo) { gHz = hz; return; }
  waitWire();
  esp_lcd_panel_io_del(gIo);
  gIo = nullptr;
  gHz = hz;
  makeIo();
}

}  // namespace lcd
