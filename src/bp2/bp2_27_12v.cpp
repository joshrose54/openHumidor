// =====================================================================
// BP2 test 27 — the 12V sense (Rev Q), using the firmware's own driver (GPIO8, ADC1, 12 dB,
// a reading every 100 ms, 1 s averages).
//
// Every second it prints the pin's millivolts, the 1 s average in volts with the scale in use,
// and the scale. With the meter on 12V (TP6), type  cal <volts>  (what the meter reads):
// the scale factor is worked out, used from then on, and printed for include/board.h and the
// build log. Then the PSU at 11.0V: the reading must stay within 0.1V of the meter.
// Switches nothing.
// =====================================================================
#include <Arduino.h>
#include <stdlib.h>
#include "bp2/common/bp2_ui.h"
#include "wineador/supply.h"
#include "wineador/bp2_calc.h"
#include "drivers/supply_sense.h"
#include "board.h"

namespace {

const char* const kBanner[] = {
  "Bench PSU into J1 through the 4A fuse. Meter 12V (TP6) to GND (TP7).",
  "1. PSU at 12.0V. When the readings settle, type  cal <meter volts>  e.g.  cal 12.03",
  "   RECORD the scale factor it prints (build log; it then goes into include/board.h).",
  "2. PSU at 11.0V. The 'volts' reading must be within 0.1V of the meter. Then back to 12.0V: the same.",
  "Switches nothing.",
  nullptr,
};

float gScale = board::k12vScale;
uint32_t gLastMs = 0;
float gAvgPinMv = 0;   // the last 1 s average, as pin millivolts

}  // namespace

void setup() {
  bp2::begin("BP2 TEST 27: 12V SENSE", kBanner);
  supplysense::begin();
  Serial.printf("  Scale in use: %.4f (%s)\n", (double)gScale, gScale == 115.0f / 15.0f ? "nominal, the 100k/15k divider" : "from board.h");
}

void loop() {
  const uint32_t now = millis();
  supplysense::update(now);
  auto& s = supplysense::logic();
  if (s.haveAverage()) gAvgPinMv = s.lastAverage() / board::k12vScale * 1000.0f;   // back to pin mV
  if (now - gLastMs >= 1000 && s.haveAverage()) {
    gLastMs = now;
    const float v = gAvgPinMv / 1000.0f * gScale;
    Serial.printf("  pin %4lu mV (1 s average %.0f mV) | volts %.2f V with scale %.4f\n", (unsigned long)supplysense::lastPinMv(),
                  (double)gAvgPinMv, (double)v, (double)gScale);
  }
  char w[32];
  if (!bp2::readLine(w, sizeof(w))) return;
  if (strncmp(w, "cal ", 4) == 0) {
    const float meter = strtof(w + 4, nullptr);
    const float sc = wineador::scaleFromMeter(meter, (uint32_t)(gAvgPinMv + 0.5f));
    if (meter < 5.0f || meter > 30.0f || sc <= 0.0f) {
      Serial.println("  cal <volts>: the meter's reading on 12V (TP6), e.g. cal 12.03. Nothing changed.");
      return;
    }
    gScale = sc;
    Serial.println("  ---------------------------------------------------------------------");
    Serial.printf ("  SCALE FACTOR: %.4f  (meter %.2f V, pin %.0f mV). RECORD it (build log).\n", (double)sc, (double)meter,
                   (double)gAvgPinMv);
    Serial.printf ("  For include/board.h:  constexpr float k12vScale = %.4ff;\n", (double)sc);
    Serial.println("  Used from now on. Now the PSU at 11.0V: 'volts' within 0.1V of the meter.");
    Serial.println("  ---------------------------------------------------------------------");
  } else {
    Serial.printf("  '%s' - type  cal <meter volts>.\n", w);
  }
}
