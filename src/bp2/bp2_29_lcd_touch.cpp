// =====================================================================
// BP2 test 29 — the LCD and touch (Rev Q), using the firmware's own LCD, touch, backlight and
// I2C drivers. The LCD on J20 (test 10 passed).
//
// At boot the firmware's LCD start-up runs (reset, cleared to black) and the backlight is off.
// g and each n:  1 colour bars at 15 MHz, backlight 100%  ·  2 the bars at 20 MHz (a trial)
//   3 touch: coordinates print while a finger is down  ·  4 backlight 100% -> 10%
//   5 GPIO48 pulsed: the LCD and touch both reset; touch reads fail, then work 300 ms on;
//     the bars are drawn again  ·  6 I2C1 scanned (expect 0x38 touch, 0x68 RTC), RTC read;
//     everything idle so you can meter I2C1.
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
#include "pins.h"

namespace {

const char* const kBanner[] = {
  "The LCD on J20 with its 30 cm ribbon (test 10 passed). Nothing else needed.",
  "At boot the LCD is cleared to black with the backlight OFF (as the firmware does before B7).",
  "Steps: bars at 15 MHz; bars at 20 MHz (trial); touch; backlight dimming; GPIO48 reset; I2C1 scan.",
  nullptr,
};

const char* kTitles[] = {
  "1. Colour bars at 15 MHz, backlight 100%",
  "2. Colour bars at 20 MHz (a TRIAL only: the cabinet runs 15 MHz)",
  "3. Touch: coordinates while a finger is down",
  "4. Backlight 100% -> 10%",
  "5. GPIO48 pulsed: LCD and touch reset together",
  "6. I2C1 scan and RTC read; then everything idle",
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
  "The scan finds 0x38 (touch) and 0x68 (RTC); the RTC time prints. Now idle: meter I2C1 SDA and SCL at R69 and "
  "R70 to GND (TP3): each about 3.3V - NOT 5V",
};

wineador::StepSequencer gSeq;
uint32_t gLastTouchMs = 0, gDimMs = 0, gLastSweepMs = 0;
int gDim = 100;
bool gTouchOn = false, gTouchDown = false;
bool gResetWatch = false, gScanPending = false;
uint32_t gResetAtMs = 0;

void live(int i) {
  bp2::Live l;
  l.showLoads = false;
  l.backlightPct = backlight::pct();
  l.extra = i == 1 ? "LCD SPI ......... 20 MHz (trial)" : "LCD SPI ......... 15 MHz";
  bp2::printStep(i, 6, kTitles[i], l, kChecks[i], i + 1 < 6 ? kTitles[i + 1] : nullptr);
}

void scan() {
  Serial.print("  I2C1 scan (GPIO21 SDA / GPIO14 SCL): found");
  int n = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire1.beginTransmission(a);
    if (Wire1.endTransmission() == 0) { Serial.printf(" 0x%02X", a); n++; }
  }
  Serial.printf("%s  (expect 0x38 touch and 0x68 RTC)\n", n ? "" : " NOTHING");
  wineador::DateTime dt;
  if (i2cbus::rtcOk() && i2cbus::rtcDateTime(dt))
    Serial.printf("  RTC on I2C1: %04d-%02d-%02d %02d:%02d:%02d UTC - read OK\n", dt.year, dt.month, dt.day, dt.hour, dt.minute,
                  dt.second);
  else Serial.println("  RTC on I2C1: NOT READ - check");
}

void apply(int i, uint32_t now) {
  gTouchOn = false;
  switch (i) {
    case 0: lcd::setSpiHz(wineador::kLcdSpiHz); backlight::set(100); lcd::colourBars(); break;
    case 1: lcd::setSpiHz(20000000); lcd::colourBars(); lcd::setSpiHz(wineador::kLcdSpiHz); break;
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
  }
  live(i);
}

}  // namespace

void setup() {
  backlight::begin();                     // 5 kHz PWM, OFF
  bp2::begin("BP2 TEST 29: LCD AND TOUCH", kBanner);
  i2cbus::begin(millis());                // I2C0 (mux) and I2C1 (RTC, touch), as the firmware
  lcd::begin();                           // the firmware's start-up: reset, cleared to black
  gSeq.begin(6);
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
    if (gResetWatch) {
      if (ok) {
        Serial.printf("  %lu ms after GPIO48 was released: touch answers again\n", (unsigned long)(now - gResetAtMs));
        gResetWatch = false;
      }
    }
    if (ok && t.count > 0) { Serial.printf("  touch x %u y %u\n", (unsigned)t.x, (unsigned)t.y); gTouchDown = true; }
    else if (ok && gTouchDown) { Serial.println("  (finger up)"); gTouchDown = false; }
  }
  char w[32];
  if (!bp2::readLine(w, sizeof(w))) return;
  using E = wineador::StepSequencer::Event;
  switch (gSeq.handle(w)) {
    case E::Start:
      if (!lcd::ready()) { Serial.println("  The LCD start-up hasn't finished - type g again."); gSeq.begin(6); break; }
      apply(0, now);
      break;
    case E::Step: apply(gSeq.step(), now); break;
    case E::Finish:
    case E::Abort:
      gTouchOn = false;
      backlight::set(0);
      lcd::fill(0, 0, wineador::kLcdWidth, wineador::kLcdHeight, wineador::kBlack);
      if (gSeq.waiting() && w[0] == 'x') bp2::printAborted();
      else bp2::printFinished("pattern OK at 15 MHz; 20 MHz result; the four corner coordinates; dimming OK; reset "
                              "OK; the scan result; SDA and SCL volts at R69/R70");
      break;
    case E::Unknown: Serial.printf("  '%s' - type %s (x = stop).\n", w, gSeq.waiting() ? "g" : "n"); break;
    case E::None: break;
  }
}
