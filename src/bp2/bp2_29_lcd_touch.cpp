// =====================================================================
// BP2 test 29 — the LCD and touch (Rev Q), using the firmware's own LCD, touch, backlight and
// I2C drivers. The LCD on J20 (test 10 passed).
//
// At boot the firmware's LCD start-up runs (reset, cleared to black) and the backlight is off.
// g and each n:  1 colour bars at 13.3 MHz, backlight 100%  ·  2 the bars at 20 MHz (a trial)
//   3 touch: coordinates print while a finger is down  ·  4 backlight 100% -> 10%
//   5 GPIO48 pulsed: the LCD and touch both reset; touch reads fail, then work 300 ms on;
//     the bars are drawn again  ·  6 I2C1 scanned (expect 0x38 touch, 0x68 RTC), the touch
//     chip's ID registers read, RTC read; everything idle so you can meter I2C1
//   7 the idle check: the screen left untouched for over 40 s with the failed touch reads
//     counted (monitor mode, 3A.11), then touched  ·  8 the landscape check (spec 5.5, BP2 test 29):
//     an UP arrow and four corner targets, drawn by the firmware's own screen code; each touch
//     is reported by the corner it landed in. a / b switch between the two landscape ways, so
//     Josh can choose the one that is up in the enclosure (board.h kLcdOrientation; build log).
// x = backlight off, screen black.
// =====================================================================
#include <Arduino.h>
#include <Wire.h>
#include "bp2/common/bp2_ui.h"
#include "wineador/bp2_steps.h"
#include "wineador/lcd_seq.h"
#include "wineador/decode.h"
#include "drivers/lcd.h"
#include "drivers/touch.h"
#include "drivers/backlight.h"
#include "drivers/i2c_bus.h"
#include <string.h>
#include "board.h"
#include "lcdgfx/screens.h"
#include "esp_heap_caps.h"
#include "pins.h"

namespace {

const char* const kBanner[] = {
  "The LCD on J20 with its 30 cm ribbon (test 10 passed). Nothing else needed.",
  "At boot the LCD is cleared to black with the backlight OFF (as the firmware does at boot).",
  "Steps: bars at 13.3 MHz; bars at 20 MHz (trial); touch; backlight dimming; GPIO48 reset; I2C1 scan",
  "and the touch chip's ID; 40 s untouched, then a touch; the landscape check (UP arrow, four corners;",
  "a / b switch the landscape way).",
  nullptr,
};

const char* kTitles[] = {
  "1. Colour bars at 13.3 MHz, backlight 100%",
  "2. Colour bars at 20 MHz (a TRIAL only: the cabinet runs 13.3 MHz)",
  "3. Touch: coordinates while a finger is down",
  "4. Backlight 100% -> 10%",
  "5. GPIO48 pulsed: LCD and touch reset together",
  "6. I2C1 scan, the touch chip's ID and the RTC read; then everything idle",
  "7. Idle check: 40 s untouched, failed touch reads counted, then a touch",
  "8. Landscape check: UP arrow, a touch in each corner (a / b = the two landscape ways)",
};
const char* kChecks[] = {
  "Eight bars, left to right: white, yellow, cyan, green, magenta, red, blue, black. No tearing, no noise. "
  "Note if the order is mirrored or the colours inverted (Claude adjusts the start-up commands)",
  "The same bars, redrawn at 20 MHz: clean, or noise / wrong colours? Record it (trial only)",
  "Touch the screen: x,y print while touched. Top-left, top-right, bottom-left, bottom-right corners: record "
  "roughly what each prints. The numbers follow the finger",
  "The backlight steps down every second from 100% to 10% and stays there: it visibly dims",
  "Above: with GPIO48 held low the touch controller does NOT answer (it is in reset); the screen goes black "
  "and the bars come back; then 'touch answers again'. Touch the screen: coordinates print",
  "The scan finds 0x38 (touch) and 0x68 (RTC). The touch chip's ID prints: 0xA8 = 0x11, 0x9F = 0x26, 0xA3 = 0x64 "
  "and 'reads as an FT6336'. Record the three values. The RTC time prints. Now idle: meter I2C1 SDA and SCL at "
  "R69 and R70 to GND (TP3): each about 3.3V - NOT 5V",
  "DON'T touch the screen. Register 0x86 prints as 0x00 (monitor mode off). Every 5 s the count of touch reads "
  "and FAILED reads prints; after 40 s untouched it says so. Failed reads stay 0 all the way. Then touch the "
  "screen: 'coordinates came back' prints with x,y. Record the failed count at 40 s and the coordinates. (A touch "
  "before 40 s restarts the count)",
  "Landscape A first. The arrow points to the top of the screen and the word UP is above it. Touch each "
  "corner target in turn: each prints 'corner: top left' etc. for the corner you touched, and its target "
  "turns green. Type b for landscape B (turned 180 degrees), a for A again; do the four corners in each. "
  "CHOOSE the way that is up as the LCD sits in the enclosure, and RECORD it (A or B) for the build log",
};

wineador::StepSequencer gSeq;
uint32_t gLastTouchMs = 0, gDimMs = 0, gLastSweepMs = 0;
int gDim = 100;
bool gTouchOn = false, gTouchDown = false;
bool gResetWatch = false, gScanPending = false;
uint32_t gResetAtMs = 0;
constexpr int kSteps = 8;
constexpr int kIdleStep = 6, kLandscapeStep = 7;

// Step 7: the picture (PSRAM) and the firmware's own drawing code.
uint16_t* gFrame = nullptr;
lcdgfx::Canvas* gCanvas = nullptr;
wineador::LcdOrientation gWay = wineador::LcdOrientation::LandscapeA;
uint8_t gCorners = 0;
bool gLandscapeOn = false;

// Step 7, the idle check (3A.11: monitor mode off, so the chip answers while idle).
constexpr uint32_t kIdleMs = 40000, kIdleReportMs = 5000;
bool gIdleOn = false, gIdlePassed = false, gIdleFinished = false;
uint32_t gIdleStartMs = 0, gIdleReportMs = 0, gIdleReads = 0, gIdleFails = 0;
uint32_t gSetupReported = 0;             // the reset whose 0x86 write was last reported

void drawCheck() {
  if (!gCanvas) { Serial.println("  NO PSRAM for the picture: the landscape check can't be drawn - CHECK"); return; }
  lcd::setOrientation(gWay);
  const char* name = gWay == wineador::LcdOrientation::LandscapeA ? "Landscape A" : "Landscape B";
  lcdgfx::paintLandscapeCheck(*gCanvas, name, gCorners);
  lcd::pushFrame();
}

void startWay(wineador::LcdOrientation w) {
  gWay = w;
  gCorners = 0;
  drawCheck();
  Serial.printf("  NOW: %s (MADCTL 0x%02X). Touch each corner.\n", wineador::lcdOrientationName(w), wineador::lcdMadctl(w));
}

void live(int i) {
  bp2::Live l;
  l.showLoads = false;
  l.backlightPct = backlight::pct();
  l.extra = i == 1 ? "LCD SPI ......... 20 MHz (trial)" : i == kLandscapeStep ? "LCD ............. landscape, 13.3 MHz"
          : i == kIdleStep ? "Touch ........... read every 50 ms, failed reads counted" : "LCD SPI ......... 13.3 MHz";
  bp2::printStep(i, kSteps, kTitles[i], l, kChecks[i], i + 1 < kSteps ? kTitles[i + 1] : nullptr);
}

void scan() {
  Serial.print("  I2C1 scan (GPIO21 SDA / GPIO14 SCL): found");
  int n = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire1.beginTransmission(a);
    if (Wire1.endTransmission() == 0) { Serial.printf(" 0x%02X", a); n++; }
  }
  Serial.printf("%s  (expect 0x38 touch and 0x68 RTC)\n", n ? "" : " NOTHING");
  uint8_t v = 0, m = 0, h = 0;
  if (touch::readReg(wineador::kTouchRegVendorId, v) && touch::readReg(wineador::kTouchRegChipMid, m) &&
      touch::readReg(wineador::kTouchRegChipHigh, h))
    Serial.printf("  Touch chip ID: 0xA8 = 0x%02X, 0x9F = 0x%02X, 0xA3 = 0x%02X - %s\n", v, m, h,
                  wineador::isFt6336(v, m, h) ? "reads as an FT6336 (0x11, 0x26, 0x64) - correct"
                                              : "NOT what an FT6336 reads (0x11, 0x26, 0x64) - CHECK");
  else Serial.println("  Touch chip ID: NOT READ (no answer at 0x38) - CHECK");
  wineador::DateTime dt;
  if (i2cbus::rtcOk() && i2cbus::rtcDateTime(dt))
    Serial.printf("  RTC on I2C1: %04d-%02d-%02d %02d:%02d:%02d UTC - read OK\n", dt.year, dt.month, dt.day, dt.hour, dt.minute,
                  dt.second);
  else Serial.println("  RTC on I2C1: NOT READ - check");
}

void startIdle(uint32_t now) {
  gIdleStartMs = gIdleReportMs = now;
  gIdleReads = gIdleFails = 0;
  gIdlePassed = gIdleFinished = false;
}

void apply(int i, uint32_t now) {
  gTouchOn = false;
  gLandscapeOn = false;
  gIdleOn = false;
  switch (i) {
    case 0: lcd::setSpiHz(wineador::kLcdSpiHz); backlight::set(100); lcd::colourBars(); break;
    case 1: lcd::setSpiHz(wineador::kLcdTrialSpiHz); lcd::colourBars(); lcd::setSpiHz(wineador::kLcdSpiHz); break;
    case 2: gTouchOn = true; break;
    case 3: gDim = 100; gDimMs = now; backlight::set(100); break;
    case 4: {
      backlight::set(100);
      // Show the touch controller really is reset by GPIO48: held low, it must not answer.
      digitalWrite(pins_revq::kLcdTouchReset, LOW);
      Wire1.beginTransmission(wineador::kTouchAddr);
      const bool answered = Wire1.endTransmission() == 0;
      Serial.printf("  GPIO48 held LOW: touch controller at 0x38 %s\n",
                    answered ? "STILL ANSWERS - CHECK (it should be held in reset)" : "does not answer - held in reset (correct)");
      lcd::resetAndInit();                // GPIO48 low 10 ms, released; LCD start-up (~0.5 s); black
      lcd::colourBars();
      gResetWatch = true;
      gResetAtMs = lcd::resetReleasedMs();
      gTouchOn = true;
      break;
    }
    case 5: gScanPending = true; break;   // done in loop() as soon as I2C is free
    case kIdleStep: {
      live(i);
      uint8_t c = 0xFF;
      if (touch::readReg(wineador::kTouchRegCtrl, c))
        Serial.printf("  Touch register 0x86 = 0x%02X - %s\n", c,
                      c == wineador::kTouchCtrlStayActive ? "monitor mode off (correct)" : "monitor mode NOT off - CHECK");
      else Serial.println("  Touch register 0x86: NOT READ - CHECK");
      Serial.println("  Don't touch the screen for 40 s.");
      gTouchOn = true;
      gIdleOn = true;
      startIdle(now);
      return;
    }
    case kLandscapeStep:
      live(i);
      gTouchOn = true;
      gLandscapeOn = true;
      startWay(wineador::LcdOrientation::LandscapeA);
      return;
  }
  live(i);
}

}  // namespace

void setup() {
  backlight::begin();                     // 5 kHz PWM, OFF
  bp2::begin("BP2 TEST 29: LCD AND TOUCH", kBanner);
  i2cbus::begin(millis());                // I2C0 (mux) and I2C1 (RTC, touch), as the firmware
  // The firmware's start-up (reset, cleared to black), portrait for steps 1-6.
  lcd::begin(wineador::LcdOrientation::Portrait, board::kLcdSendBytes);
  gFrame = (uint16_t*)heap_caps_malloc((size_t)wineador::kScreenW * wineador::kScreenH * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (gFrame) {
    static lcdgfx::Canvas canvas(gFrame, wineador::kScreenW, wineador::kScreenH);
    gCanvas = &canvas;
    lcd::setFrame(gFrame, wineador::kScreenW, wineador::kScreenH);
  }
  gSeq.begin(kSteps);
  Serial.println("  Type  g  for step 1 (after the LCD start-up has finished, ~0.5 s).");
}

void loop() {
  const uint32_t now = millis();
  i2cbus::update(now);
  if (gScanPending && i2cbus::idle()) { gScanPending = false; scan(); }
  if (i2cbus::idle() && now - gLastSweepMs >= 2000) { gLastSweepMs = now; i2cbus::startSweep(); }   // keeps the RTC read
  if (gSeq.running() && gSeq.step() == 3 && gDim > 10 && now - gDimMs >= 1000) {
    gDimMs = now;
    gDim -= 10;
    backlight::set((uint8_t)gDim);
    Serial.printf("  backlight %d%%\n", gDim);
  }
  if (gTouchOn && now - gLastTouchMs >= 50 && i2cbus::idle()) {
    gLastTouchMs = now;
    wineador::TouchPoint t;
    const bool ok = touch::read(t);
    // After each GPIO48 reset the driver writes 0x00 to 0x86 (monitor mode off, 3A.11): say when it has.
    if (touch::takeSetupFailure())
      Serial.println("  Touch setup: writing 0x00 to register 0x86 (monitor mode off) FAILED after the reset - CHECK "
                     "(tried again at each read)");
    if (touch::setupDone() && lcd::resetCount() != gSetupReported) {
      gSetupReported = lcd::resetCount();
      Serial.println("  Touch setup: monitor mode off (0x00 written to register 0x86) after the reset");
    }
    const bool idleCounting = gIdleOn && !gIdleFinished;
    if (idleCounting) {
      gIdleReads++;
      if (!ok) gIdleFails++;
      const uint32_t s = (now - gIdleStartMs) / 1000;
      if (ok && t.count > 0) {
        if (!gIdlePassed) {
          // Too soon: the 40 s start again once the finger is lifted.
          if (!gTouchDown)
            Serial.printf("  Touched after only %lu s - too soon. Leave it alone: counting again from 0.\n", (unsigned long)s);
          gTouchDown = true;
          startIdle(now);
        } else {
          Serial.printf("  Coordinates came back: touch x %u y %u (failed reads while untouched: %lu of %lu%s)\n",
                        (unsigned)t.x, (unsigned)t.y, (unsigned long)gIdleFails, (unsigned long)gIdleReads,
                        gIdleFails ? " - CHECK: expect 0" : ", as expected");
          gIdleFinished = true;
          gTouchDown = true;
        }
      } else if (ok && gTouchDown) {
        gTouchDown = false;                // the too-soon finger lifted: the count starts here
        startIdle(now);
      } else if (!gIdlePassed && now - gIdleStartMs >= kIdleMs) {
        gIdlePassed = true;
        Serial.printf("  40 s untouched (the chip's monitor mode would have started at 30 s): %lu reads, %lu FAILED%s.\n"
                      "  NOW touch the screen: the coordinates should print.\n",
                      (unsigned long)gIdleReads, (unsigned long)gIdleFails, gIdleFails ? " - CHECK: expect 0" : " - correct");
      } else if (!gIdlePassed && now - gIdleReportMs >= kIdleReportMs) {
        gIdleReportMs = now;
        Serial.printf("  untouched %2lu s: touch reads %lu, failed %lu\n", (unsigned long)s, (unsigned long)gIdleReads,
                      (unsigned long)gIdleFails);
      }
    }
    if (gResetWatch) {
      if (ok) {
        Serial.printf("  %lu ms after GPIO48 was released: touch answers again\n", (unsigned long)(now - gResetAtMs));
        gResetWatch = false;
      }
    }
    if (idleCounting) {
      // step 7's own lines above
    } else if (gLandscapeOn) {
      // Step 8: each new touch reported by the corner it landed in (screen coordinates).
      if (ok && t.count > 0 && !gTouchDown) {
        int x, y;
        wineador::touchToScreen(gWay, t.x, t.y, &x, &y);
        const int c = lcdgfx::cornerOf(x, y);
        static const char* const names[4] = {"top left", "top right", "bottom left", "bottom right"};
        Serial.printf("  corner: %s   (screen x %d y %d; panel x %u y %u; %s)\n", c < 0 ? "none - not near a corner" : names[c],
                      x, y, (unsigned)t.x, (unsigned)t.y, wineador::lcdOrientationName(gWay));
        if (c >= 0 && !(gCorners & (1u << c))) {
          gCorners |= (uint8_t)(1u << c);
          drawCheck();
          if (gCorners == 0x0F) Serial.printf("  All four corners touched in %s.\n", wineador::lcdOrientationName(gWay));
        }
      }
      if (ok) gTouchDown = t.count > 0;
    } else if (ok && t.count > 0) { Serial.printf("  touch x %u y %u\n", (unsigned)t.x, (unsigned)t.y); gTouchDown = true; }
    else if (ok && gTouchDown) { Serial.println("  (finger up)"); gTouchDown = false; }
  }
  char w[32];
  if (!bp2::readLine(w, sizeof(w))) return;
  if (gLandscapeOn && (strcmp(w, "a") == 0 || strcmp(w, "b") == 0)) {
    startWay(w[0] == 'a' ? wineador::LcdOrientation::LandscapeA : wineador::LcdOrientation::LandscapeB);
    return;
  }
  using E = wineador::StepSequencer::Event;
  switch (gSeq.handle(w)) {
    case E::Start:
      if (!lcd::ready()) { Serial.println("  The LCD start-up hasn't finished - type g again."); gSeq.begin(kSteps); break; }
      apply(0, now);
      break;
    case E::Step: apply(gSeq.step(), now); break;
    case E::Finish:
    case E::Abort:
      gTouchOn = false;
      gLandscapeOn = false;
      gIdleOn = false;
      backlight::set(0);
      lcd::fill(0, 0, wineador::lcdWidth(lcd::orientation()), wineador::lcdHeight(lcd::orientation()), wineador::kBlack);
      if (gSeq.waiting() && w[0] == 'x') bp2::printAborted();
      else bp2::printFinished("pattern OK at 13.3 MHz; 20 MHz result; the four corner coordinates; dimming OK; reset "
                              "OK; the scan result and the touch chip's three ID values; SDA and SCL volts at R69/R70; the "
                              "failed touch reads after 40 s untouched and the coordinates after; the landscape way chosen "
                              "(A or B) and that every corner was reported at its corner in it");
      break;
    case E::Unknown: Serial.printf("  '%s' - type %s (x = stop).\n", w, gSeq.waiting() ? "g" : "n"); break;
    case E::None: break;
  }
}
