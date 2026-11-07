#include "outputs.h"
#include <Arduino.h>
#include "pins.h"

namespace outputs {

static wineador::LoadOutputs gNow;   // all false

void begin() {
  // Same order as the reference sketches: each pin set LOW as it becomes an output.
  pinMode(pins::kCoolingSsr, OUTPUT); digitalWrite(pins::kCoolingSsr, LOW);
  pinMode(pins::kHeater, OUTPUT);     digitalWrite(pins::kHeater, LOW);
  pinMode(pins::kRelayK1, OUTPUT);    digitalWrite(pins::kRelayK1, LOW);
  pinMode(pins::kHumidifier, OUTPUT); digitalWrite(pins::kHumidifier, LOW);
  gNow = wineador::LoadOutputs{};
}

void allOff() {
  // Load signals first, then the interlock. With both loads already off the
  // relay can drop at once: there is nothing to settle for.
  digitalWrite(pins::kCoolingSsr, LOW);
  digitalWrite(pins::kHeater, LOW);
  digitalWrite(pins::kHumidifier, LOW);
  digitalWrite(pins::kRelayK1, LOW);
  gNow = wineador::LoadOutputs{};
}

wineador::LoadOutputs current() { return gNow; }

}  // namespace outputs
