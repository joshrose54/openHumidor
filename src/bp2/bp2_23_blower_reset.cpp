// =====================================================================
// BP2 test 23 — blower overshoot on reset (Rev Q). Four blowers on J12-J15.
//
// Peak-hold across the blowers: a diode + 1uF + the meter, from 12V (TP6) to BLW- (TP12).
// g sets the blowers to 50%. Press RESET (SW1): the board restarts, the blowers come up at full
// speed (GPIO15's pull-up), and this sketch waits again - type g to go back to 50%. Ten times.
// The peak must never be more than 12V (TP6) + 0.5V. x = back to full speed.
// =====================================================================
#include <Arduino.h>
#include "bp2/common/bp2_ui.h"
#include "drivers/blowers.h"

namespace {

const char* const kBanner[] = {
  "Four blowers on J12-J15. Peak-hold meter: diode + 1uF + meter from 12V (TP6) to BLW- (TP12).",
  "Note 12V (TP6) first. Then, ten times: g (blowers to 50%), wait 5 s, press RESET (SW1).",
  "After each reset the blowers are at full speed and this banner prints again - type g again.",
  "Pass: the peak never above 12V (TP6) + 0.5V. Record the highest peak and the count of resets.",
  nullptr,
};

}  // namespace

void setup() {
  blowers::begin();                      // full speed, as undriven, until g
  bp2::begin("BP2 TEST 23: BLOWER OVERSHOOT ON RESET", kBanner);
  Serial.println("  The blowers are at FULL speed. Type  g  to set them to 50%.");
}

void loop() {
  char w[32];
  if (!bp2::readLine(w, sizeof(w))) return;
  bp2::Live l;
  l.showLoads = false;
  if (strcmp(w, "g") == 0) {
    blowers::setPct(50);
    l.blowerPct = 50;
    bp2::printLive(l);
    Serial.println("  CHECK NOW: wait 5 s, then press RESET (SW1) and read the peak-hold: no more than 12V (TP6) + 0.5V.");
    Serial.println("  (The board restarts at full speed; type g again for the next of the ten.)");
  } else if (strcmp(w, "x") == 0) {
    blowers::setPct(100);
    l.blowerPct = 100;
    bp2::printLive(l);
    Serial.println("  Stopped: blowers at full speed. Send Claude: the highest peak, 12V (TP6), and the number of resets.");
  } else {
    Serial.printf("  '%s' - type g (blowers to 50%%) or x (full speed).\n", w);
  }
}
