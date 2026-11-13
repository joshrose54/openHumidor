// =====================================================================
// BP2 test 22 — blower voltage (Rev Q). Four blowers on J12-J15.
//
// The blowers run at full speed from power-up (GPIO15 pulled up). After g the duty steps
// 100% -> 0% in 10% steps, one per n; at each you meter 12V (TP6) to BLW- (TP12) and listen.
// That table goes in the build log (spec 3A.11). x = back to full speed.
// =====================================================================
#include <Arduino.h>
#include "bp2/common/bp2_loads.h"

namespace {

using bp2::LoadStep;
#define BLW(p, title, check) {title, check, false, false, 0, false, false, false, p, -1}
const char* kMeter = "Meter 12V (TP6) to BLW- (TP12) and RECORD it with the duty. Listen: no ticking or whine";
const LoadStep kSteps[] = {
  BLW(100, "Blowers 100%", "12V (TP6) to BLW- (TP12): 11.5V or more. Record it. Listen: no ticking or whine"),
  BLW(90, "Blowers 90%", kMeter),
  BLW(80, "Blowers 80%", kMeter),
  BLW(70, "Blowers 70%", kMeter),
  BLW(60, "Blowers 60%", kMeter),
  BLW(50, "Blowers 50%", "12V (TP6) to BLW- (TP12): about 6V. Record it. Listen: no ticking or whine"),
  BLW(40, "Blowers 40%", kMeter),
  BLW(30, "Blowers 30%", kMeter),
  BLW(20, "Blowers 20%", kMeter),
  BLW(10, "Blowers 10%", kMeter),
  BLW(0, "Blowers 0% (stopped)", "12V (TP6) to BLW- (TP12): ~0V; the blowers stop. Record it"),
};
#undef BLW

const char* const kBanner[] = {
  "Four blowers on J12-J15 (all four: the table is calibrated with all four fitted, 3A.11).",
  "Each step sets the blower duty (PWM ~100 kHz on GPIO15); meter 12V (TP6) to BLW- (TP12).",
  "Below about 70 mA total the voltage reads higher than duty x 12V (3A.6) - expected.",
  nullptr,
};

const bp2::StepSketch kSketch = {
  "BP2 TEST 22: BLOWER VOLTAGE", kBanner, kSteps, sizeof(kSteps) / sizeof(kSteps[0]),
  "the duty-to-voltage table (100% ... 0%) and any step with ticking or whine",
  nullptr, nullptr, nullptr, nullptr,
};

}  // namespace

void setup() { bp2::stepSketchSetup(kSketch); }
void loop() { bp2::stepSketchLoop(); }
