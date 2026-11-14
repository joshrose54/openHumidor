#pragma once
// The four load outputs: cooling (SSR), heater, relay K1, humidifier (pins.h, per board).
// Rev Q also: the ion generator, GPIO17, driven LOW (off) at boot; then set every pass by its
// rule (4.6c, lib/core ion_schedule.h).
//
// begin() drives all four LOW from the first instruction. After that the only
// way to change them is apply(), with what the Controller decided this pass
// (runtime guards + interlock sequence, lib/core/controller.h).
//
// apply() writes in the order spec 3.2 needs: load signals going OFF first,
// then K1, then load signals going ON. (The sequencer never turns a temperature
// load on in the same pass K1 moves; the order is a second line of defence.)

#include "wineador/types.h"

namespace outputs {

void begin();                          // BOOT ONLY, first instruction. All four OUTPUT, LOW
void apply(const wineador::LoadOutputs& want);
// Heater PWM cap (heater_pwm.h): while the heater is ON, gates its pin. Never
// raises the pin when the heater is off, so it cannot break the interlock.
void heaterGate(bool pinOn);
void allOff();                         // load signals low first, then the relay (Rev Q: and the ion output)
void ionOff();                         // the ion generator off
// Rev Q: the ion generator on or off. The firmware's loop sets it every pass from its rule (4.6c),
// off in the boot hold, an update and a Fault; the BP2 sketches (tests 18, 20) switch it by hand.
void setIon(bool on);
bool ionOn();
wineador::LoadOutputs current();       // what is being driven right now

}  // namespace outputs
