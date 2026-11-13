// =====================================================================
// BP2 test 18 — each output alone (Rev Q). BP test 12 on the Rev Q pins, plus the ion generator.
//
// Loads connected before you start (power off while wiring):
//   J10 pins 1-2 : SSR control input, pin 1 -> SSR +A1, pin 2 -> SSR -A2 (NO mains on the SSR)
//   J10 pins 3-4 : heater pad (KSD9700 in series)
//   J11          : humidifier fan
//   J16          : ion generator (TFB-YD12102) - metered unpowered and fitted BEFORE starting;
//                  it stays off until part (e)
// Nothing switches until g. Each n applies the next step. x = everything off.
// The heater switches itself off after 5 minutes on.
// =====================================================================
#include <Arduino.h>
#include "bp2/common/bp2_loads.h"

namespace {

using bp2::LoadStep;
//                title, check,                                                   K1     heat   Hz  SSR    humid  ion    blw fan
const LoadStep kSteps[] = {
  // ---- (a) the relay alone
  {"a1. All off, relay de-energised",
   "Meter J10 pin 1 and pin 3 to GND (TP7): pin 1 ~12V, pin 3 ~0V. Heater pad off, fan off",
   false, false, 0, false, false, false, -1, -1},
  {"a2. Relay ENERGISED (nothing else on)",
   "K1 CLICKS. J10 pin 1 ~0V, pin 3 ~12V. Pad stays off (Q3/Q11 off)",
   true, false, 0, false, false, false, -1, -1},
  {"a3. Relay de-energised again",
   "K1 clicks back. J10 pin 1 ~12V, pin 3 ~0V",
   false, false, 0, false, false, false, -1, -1},
  // ---- (b) the heater
  {"b1. Relay energised, heater signal OFF",
   "Meter across the pad (J10 pin 3 to pin 4): ~0V",
   true, false, 0, false, false, false, -1, -1},
  {"b2. Heater PWM 1Hz",
   "Across the pad: ~12V and ~0V, changing once a second",
   true, true, 1, false, false, false, -1, -1},
  {"b3. Heater PWM 5Hz",
   "Across the pad: a reading between 0 and 12V that flickers (5 Hz)",
   true, true, 5, false, false, false, -1, -1},
  {"b4. Heater PWM 10Hz",
   "Across the pad: a steady part-voltage (10 Hz is faster than the meter)",
   true, true, 10, false, false, false, -1, -1},
  {"b5. Heater steady ON",
   "Across the pad: ~12V; the pad warms",
   true, true, 0, false, false, false, -1, -1},
  {"b6. Relay DE-ENERGISED with the heater signal still ON",
   "K1 clicks. Across the pad: ~0V - no heater feed with the relay off (the interlock)",
   false, true, 0, false, false, false, -1, -1},
  {"b7. Heater signal off",
   "Across the pad: ~0V",
   false, false, 0, false, false, false, -1, -1},
  // ---- (c) the SSR input
  {"c1. Relay de-energised, SSR signal OFF",
   "Red on +A1 (J10 pin 1), black on -A2 (J10 pin 2): ~0V",
   false, false, 0, false, false, false, -1, -1},
  {"c2. SSR signal ON",
   "+A1 to -A2: ~12V (the SSR's LED lights, if it has one)",
   false, false, 0, true, false, false, -1, -1},
  {"c3. Relay ENERGISED with the SSR signal still ON",
   "K1 clicks. +A1 to -A2: ~0V - no SSR feed on the heating side (the interlock)",
   true, false, 0, true, false, false, -1, -1},
  {"c4. SSR signal off, relay de-energised",
   "+A1 to -A2: ~0V",
   false, false, 0, false, false, false, -1, -1},
  // ---- (d) the humidifier fan
  {"d1. Humidifier fan ON",
   "Fan starts cleanly. Across the fan (J11 pin 1 to pin 2): 11.8V or more",
   false, false, 0, false, true, false, -1, -1},
  {"d2. Humidifier fan OFF",
   "Fan stops. Across the fan: ~0V",
   false, false, 0, false, false, false, -1, -1},
  {"d3. Humidifier fan ON again",
   "Starts cleanly again",
   false, false, 0, false, true, false, -1, -1},
  {"d4. Humidifier fan OFF",
   "Fan stops. Across the fan: ~0V",
   false, false, 0, false, false, false, -1, -1},
  // ---- (e) the ion generator
  {"e1. Ion generator OFF",
   "Across J16 (pin 1 to pin 2): ~0V. Nothing from the generator",
   false, false, 0, false, false, false, -1, -1},
  {"e2. Ion generator ON",
   "Across J16: ~12V. The generator runs (a faint hiss; ozone smell close to the needles - don't touch them)",
   false, false, 0, false, false, true, -1, -1},
  {"e3. Ion generator OFF",
   "Across J16: ~0V. It stops",
   false, false, 0, false, false, false, -1, -1},
};

const char* const kBanner[] = {
  "Loads (power off while wiring): SSR input on J10 1-2 (NO mains), heater pad + KSD on J10 3-4,",
  "humidifier fan on J11. Blowers may be on J12-J15 (they run at full speed throughout).",
  "The ion generator on J16 - BEFORE starting, with NO power: meter its black lead to its case and",
  "to its needles - both must be OPEN. Record that. Then fit it to J16; it stays OFF until part (e).",
  "Steps a-d are BP test 12's; meter to GND (TP7) on the power side.",
  nullptr,
};

const bp2::StepSketch kSketch = {
  "BP2 TEST 18: EACH OUTPUT ALONE", kBanner, kSteps, sizeof(kSteps) / sizeof(kSteps[0]),
  "pass/fail for a, b, c, d, e; J10 pin 1 and 3 volts in a1 and a2; +A1 to -A2 volts in c1-c3; volts across "
  "the fan in d1; the ion generator's black lead to case and needles (open?)",
  nullptr, nullptr, nullptr, nullptr,
};

}  // namespace

void setup() { bp2::stepSketchSetup(kSketch); }
void loop() { bp2::stepSketchLoop(); }
