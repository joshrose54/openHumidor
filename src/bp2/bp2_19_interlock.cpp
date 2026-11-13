// =====================================================================
// BP2 test 19 — the interlock with real loads (Rev Q). BP test 13 on the Rev Q pins.
//
// Loads: the 12V bulb on J10 pins 3-4 (the KSD9700 in series is optional for the bulb); the mains lamp in the switched
// extension socket, its SSR on J10 pins 1-2. Both load signals ON and the relay flipped both
// ways twice: K1 alone must keep the unselected load off.
// Nothing switches until g. Each n applies the next step. x = everything off.
// =====================================================================
#include <Arduino.h>
#include "bp2/common/bp2_loads.h"

namespace {

using bp2::LoadStep;
const char* kGates = "Meter the Q2 (SSR) gate, and the Q3 and Q11 (heater) gates, to GND (TP7): each 4.4-5V (signals ON). ";
//                title, check,                                                   K1     heat   Hz  SSR    humid  ion    blw fan
const LoadStep kSteps[] = {
  {"1. All off, relay off",
   "Both lamps off. Q2, Q3, Q11 gates ~0V",
   false, false, 0, false, false, false, -1, -1},
  {"2. BOTH signals ON, relay OFF (cooling side)",
   "ONLY the mains lamp lights; the 12V bulb is off. Then the gates (next line)",
   false, true, 0, true, false, false, -1, -1},
  {"3. BOTH signals ON, relay ON (heating side)",
   "K1 clicks. ONLY the 12V bulb lights; the mains lamp is off. Gates as step 2",
   true, true, 0, true, false, false, -1, -1},
  {"4. BOTH signals ON, relay OFF again",
   "ONLY the mains lamp again. Gates as step 2",
   false, true, 0, true, false, false, -1, -1},
  {"5. BOTH signals ON, relay ON again",
   "ONLY the 12V bulb again. Gates as step 2",
   true, true, 0, true, false, false, -1, -1},
  {"6. All off, relay off",
   "Both lamps off",
   false, false, 0, false, false, false, -1, -1},
};

const char* const kBanner[] = {
  "Loads (power off while wiring): the 12V bulb on J10 3-4 (KSD in series optional for the bulb);",
  "the mains lamp in the switched extension socket, with its SSR on J10 1-2 (as BP test 13;",
  "mains wiring per spec 4.10).",
  "In steps 2-5 BOTH load signals are ON: K1 alone decides which load has a feed.",
  nullptr,
};

const char* gateLine() { return kGates; }

const bp2::StepSketch kSketch = {
  "BP2 TEST 19: INTERLOCK WITH REAL LOADS", kBanner, kSteps, sizeof(kSteps) / sizeof(kSteps[0]),
  "which lamp lit in steps 2-5; the Q2, Q3, Q11 gate volts in steps 2 and 3",
  nullptr, gateLine, nullptr, nullptr,
};

}  // namespace

void setup() { bp2::stepSketchSetup(kSketch); }
void loop() { bp2::stepSketchLoop(); }
