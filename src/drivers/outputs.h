#pragma once
// The four load outputs: P26 cooling (SSR), P27 heater, P32 relay K1, P33 humidifier.
//
// STAGE 1: there is no way to switch a load on. begin() drives all four LOW from
// the first instruction, and allOff() is called on every loop pass. Switching
// loads on — through the interlock sequence (3.2) and the runtime guards — arrives
// at stage 2.

#include "wineador/types.h"

namespace outputs {

void begin();                         // all four OUTPUT, LOW
void allOff();                        // load signals low first, then the relay
wineador::LoadOutputs current();      // what is being driven right now

}  // namespace outputs
