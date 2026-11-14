#pragma once
// Rev Q's 12V sense on GPIO8, ADC1 at 12 dB (spec 3A.3, 3A.11). A reading every 100 ms; the
// averaging, the readings-row figures and the dip events are in lib/core/supply.h.

#include <stdint.h>
#include "wineador/supply.h"

namespace supplysense {

void begin();                      // BOOT: attenuation set; the first reading 100 ms later
void update(uint32_t nowMs);       // every loop pass: one ADC read when 100 ms have passed (~tens of us)
bool fitted();

wineador::SupplySense& logic();    // the 1 s averages, the interval figures, the dip events
uint32_t lastPinMv();              // the last raw pin reading (for 'sensors' and BP2 test 27)

}  // namespace supplysense
