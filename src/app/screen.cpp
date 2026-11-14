#include "screen.h"
#include <Arduino.h>
#include <stdio.h>
#include "board.h"
#include "drivers/lcd.h"
#include "drivers/touch.h"
#include "drivers/backlight.h"

#include "esp_heap_caps.h"
#include "lcdgfx/screens.h"

namespace screen {

using namespace wineador;

// board::kLcdSendBytes is tuned at BP2: each of its two buffers must hold at least one full-width
// row, and one piece must go out well inside 3A.11's 50 ms.
static_assert(board::kLcdSendBytes / 2 >= (size_t)kScreenW * 2, "LCD send buffers: each must hold one 480-pixel row (960 bytes)");
static_assert((board::kLcdSendBytes / 2) * 8ull * 1000ull / kLcdSpiHz < 50, "LCD send buffers: one piece over 50 ms on the wire");

static constexpr uint32_t kTouchEveryMs = 50;
static constexpr uint32_t kLogicEveryMs = 100;
static constexpr uint32_t kLongStepUs = 50000;   // 3A.11: 50 ms

static uint16_t* gFrame = nullptr;
static lcdgfx::Canvas* gCanvas = nullptr;
static lcdgfx::Painter gPainter;
static LcdUi gUi;
static bool gRunning = false;
static const char* gProblem = "not started";
static uint32_t gLastTouchMs = 0, gLastLogicMs = 0;
static bool gTouched = false;
static BootSplash gSplash;              // 5.5: the boot splash, before the walk-past screen
static UiView gSplashView;
static uint8_t gBacklight = 255;
static bool gFrameBusy = false;
static uint32_t gFrameStartMs = 0;
static bool gStress = false;
static uint32_t gLastStressMs = 0;
static Stats gStats;
static bool gLong = false;               // an over-50 ms pass waiting to be logged
static uint32_t gLongUs = 0, gLongCount = 0, gLongLoggedMs = 0;
static bool gLongEverLogged = false;
static const char* gLongWhat = "";

static bool gLogic = false;

bool begin(uint32_t nowMs, const char* version) {
  gSplashView = splashView(version);
  // The screen's logic first: the dashboard shows it even if the LCD itself fails (5.5).
  gUi.begin(nowMs, board::kBacklightDimPct);
  gLogic = true;
  // The backlight was started (off) early in setup(); it stays off until the first screen is sent (3A.11).
  // BOOT ONLY: the picture in PSRAM, once.
  gFrame = (uint16_t*)heap_caps_malloc((size_t)kScreenW * kScreenH * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!gFrame) { gProblem = "no PSRAM for the picture"; lcd::begin(board::kLcdOrientation, board::kLcdSendBytes); return false; }
  static lcdgfx::Canvas canvas(gFrame, kScreenW, kScreenH);
  gCanvas = &canvas;
  canvas.fill(Rect(0, 0, kScreenW, kScreenH), 0);
  if (!lcd::begin(board::kLcdOrientation, board::kLcdSendBytes)) { gProblem = "LCD send buffers or SPI bus not had"; return false; }
  lcd::setFrame(gFrame, kScreenW, kScreenH);
  gPainter.invalidate();
  gRunning = true;
  gProblem = nullptr;
  return true;
}

bool running() { return gRunning; }
const char* problem() { return gProblem; }
bool logicRunning() { return gLogic; }
const LcdUi& ui() { return gUi; }

static void maxInto(uint32_t& m, uint32_t v) { if (v > m) m = v; }

void update(const UiInputs& in) {
  if (!gLogic) return;
  const uint32_t now = in.nowMs;
  if (!gRunning || !lcd::ready()) {
    // No LCD to draw on: the logic still runs (the dashboard shows its modes and alerts).
    if (now - gLastLogicMs >= kLogicEveryMs) { gLastLogicMs = now; gUi.update(in); }
    return;
  }
  uint32_t t0 = micros(), touchUs = 0, logicUs = 0, drawUs = 0, sendUs = 0;

  // 1. Touch, every 50 ms (I2C1, from the loop as the RTC is). No read until 300 ms after a reset.
  //    Not while the boot splash is up (5.5): nothing on it to touch.
  if (gSplash.touches() && now - gLastTouchMs >= kTouchEveryMs) {
    gLastTouchMs = now;
    TouchPoint t;
    if (touch::read(t)) {
      int x = 0, y = 0;
      if (t.count > 0) touchToScreen(lcd::orientation(), t.x, t.y, &x, &y);
      gUi.touch(t.count > 0, x, y, now);
      if (t.count > 0) gTouched = true;
    } else {
      gStats.touchFails++;
    }
    touchUs = micros() - t0;
  }

  // 2. The screen logic, every 100 ms or straight after a touch.
  uint32_t t1 = micros();
  if (gTouched || now - gLastLogicMs >= kLogicEveryMs) {
    gTouched = false;
    gLastLogicMs = now;
    gUi.update(in);
    logicUs = micros() - t1;
  }

  // 3. One drawing step into the picture: the splash's while it is going up, the UI's after.
  //    What it changed is queued to send - except while the splash is held on the screen, when
  //    the walk-past screen is drawn behind it unsent (BootSplash).
  const UiView& view = gSplash.paintsSplash() ? gSplashView : gUi.view();
  if (gStress && gSplash.phase() == BootSplash::Phase::Done && now - gLastStressMs >= 1000) {
    gLastStressMs = now;
    gPainter.invalidate();
  }
  uint32_t t2 = micros();
  Rect painted;
  if (gPainter.step(*gCanvas, view, &painted)) {
    if (gSplash.sends()) lcd::queue(painted);
    drawUs = micros() - t2;
    if (gSplash.sends() && !gFrameBusy) { gFrameBusy = true; gFrameStartMs = now; }
  }
  gSplash.update(gPainter.idle(view), lcd::idle(), now);
  if (gSplash.takeRelease()) {                      // the walk-past screen replaces the splash, whole
    lcd::queue(Rect(0, 0, kScreenW, kScreenH));
    if (!gFrameBusy) { gFrameBusy = true; gFrameStartMs = now; }
  }

  // 4. One sending step (DMA runs on by itself).
  uint32_t t3 = micros();
  lcd::step();
  sendUs = micros() - t3;

  // A frame is done when nothing is left to draw or send.
  if (gFrameBusy && lcd::idle() && gPainter.idle(view)) {    // the view being painted (asking about another would restart it)
    gFrameBusy = false;
    gStats.lastFrameMs = now - gFrameStartMs;
    maxInto(gStats.maxFrameMs, gStats.lastFrameMs);
    gStats.frames++;
  }

  // 5. Backlight (3A.11): off until the boot splash is all on the screen; then 5.5's level.
  const uint8_t bl = gSplash.backlightOn() ? gUi.backlightPct(now) : 0;
  if (bl != gBacklight) { backlight::set(bl); gBacklight = bl; }

  const uint32_t passUs = micros() - t0;
  maxInto(gStats.touchMaxUs, touchUs);
  maxInto(gStats.logicMaxUs, logicUs);
  maxInto(gStats.drawMaxUs, drawUs);
  maxInto(gStats.sendMaxUs, sendUs);
  maxInto(gStats.passMaxUs, passUs);
  if (passUs > kLongStepUs) {
    gLongCount++;
    if (!gLong || passUs > gLongUs) {          // keep the worst one waiting to be logged
      gLong = true;
      gLongUs = passUs;
      const uint32_t m = touchUs > logicUs ? touchUs : logicUs;
      gLongWhat = drawUs >= sendUs && drawUs >= m ? "drawing" : sendUs >= m ? "sending" : touchUs >= logicUs ? "touch read" : "screen logic";
    }
  }
}

bool takeSave(float* t, float* rh) { return gUi.takeSave(t, rh); }
bool takeWindowOpened() { return gUi.takeWindowOpened(); }
UpdateWindow& window() { return gUi.window(); }

bool takeLongStep(uint32_t* us, const char** what, uint32_t* count) {
  // At most one event a minute: the worst pass since the last one, and how many there were.
  const uint32_t now = millis();
  if (!gLong || (gLongEverLogged && now - gLongLoggedMs < 60000)) return false;
  gLong = false;
  gLongEverLogged = true;
  gLongLoggedMs = now;
  if (us) *us = gLongUs;
  if (what) *what = gLongWhat;
  if (count) *count = gLongCount;
  gLongUs = 0;
  gLongCount = 0;
  return true;
}

void stress(bool on) { gStress = on; gLastStressMs = 0; }
bool stressing() { return gStress; }
Stats stats() { return gStats; }

void resetMax() {
  gStats.touchMaxUs = gStats.logicMaxUs = gStats.drawMaxUs = gStats.sendMaxUs = gStats.passMaxUs = 0;
  gStats.maxFrameMs = 0;
}

void formatStatus(char* out, size_t n) {
  if (!gRunning) { snprintf(out, n, "lcd OFF (%s)", gProblem ? gProblem : "?"); return; }
  snprintf(out, n, "lcd draw %.1f send %.1f touch %.1f ms max%s | frame %lu ms (max %lu)%s",
           gStats.drawMaxUs / 1000.0, gStats.sendMaxUs / 1000.0, gStats.touchMaxUs / 1000.0,
           gStats.passMaxUs > kLongStepUs ? " OVER 50 ms" : "", (unsigned long)gStats.lastFrameMs,
           (unsigned long)gStats.maxFrameMs, gStress ? " | STRESS" : "");
}

}  // namespace screen
