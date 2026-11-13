#include "outputs.h"
#include <Arduino.h>
#include "pins.h"

namespace outputs {

static wineador::LoadOutputs gNow;   // all false
static bool gHeaterGate = true;      // PWM cap: heater pin allowed high right now

void begin() {
  // Same order as the reference sketches: each pin set LOW as it becomes an output.
  pinMode(pins::kCoolingSsr, OUTPUT); digitalWrite(pins::kCoolingSsr, LOW);
  pinMode(pins::kHeater, OUTPUT);     digitalWrite(pins::kHeater, LOW);
  pinMode(pins::kRelayK1, OUTPUT);    digitalWrite(pins::kRelayK1, LOW);
  pinMode(pins::kHumidifier, OUTPUT); digitalWrite(pins::kHumidifier, LOW);
#if defined(WINEADOR_BOARD_REVQ)
  // The ion generator (GPIO17, Q6, no U9): high = on. Kept OFF in stages Q1-Q3 (3A.11); its
  // rules (4.6c) come after BP2.
  pinMode(pins::kIon, OUTPUT);        digitalWrite(pins::kIon, LOW);
#endif
  gNow = wineador::LoadOutputs{};
}

void apply(const wineador::LoadOutputs& w) {
  if (w == gNow) return;
  // 1. Load signals going off
  if (gNow.cool && !w.cool)   digitalWrite(pins::kCoolingSsr, LOW);
  if (gNow.heat && !w.heat)   digitalWrite(pins::kHeater, LOW);
  if (gNow.humid && !w.humid) digitalWrite(pins::kHumidifier, LOW);
  // 2. The interlock
  if (gNow.relayHeating != w.relayHeating) digitalWrite(pins::kRelayK1, w.relayHeating ? HIGH : LOW);
  // 3. Load signals going on
  if (!gNow.cool && w.cool)   digitalWrite(pins::kCoolingSsr, HIGH);
  if (!gNow.heat && w.heat)   digitalWrite(pins::kHeater, gHeaterGate ? HIGH : LOW);
  if (!gNow.humid && w.humid) digitalWrite(pins::kHumidifier, HIGH);
  gNow = w;
}

void heaterGate(bool pinOn) {
  if (pinOn == gHeaterGate) return;
  gHeaterGate = pinOn;
  if (gNow.heat) digitalWrite(pins::kHeater, pinOn ? HIGH : LOW);
}

void allOff() {
  digitalWrite(pins::kCoolingSsr, LOW);
  digitalWrite(pins::kHeater, LOW);
  digitalWrite(pins::kHumidifier, LOW);
  digitalWrite(pins::kRelayK1, LOW);
#if defined(WINEADOR_BOARD_REVQ)
  digitalWrite(pins::kIon, LOW);
#endif
  gNow = wineador::LoadOutputs{};
}

void ionOff() {
#if defined(WINEADOR_BOARD_REVQ)
  digitalWrite(pins::kIon, LOW);      // every pass: held off until its rules (4.6c) exist
#endif
}

wineador::LoadOutputs current() { return gNow; }

}  // namespace outputs
