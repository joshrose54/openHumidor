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
  gNow = wineador::LoadOutputs{};
}

wineador::LoadOutputs current() { return gNow; }

}  // namespace outputs
