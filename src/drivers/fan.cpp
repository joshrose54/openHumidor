#include "fan.h"
#include <Arduino.h>
#include "soc/soc.h"
#include "soc/gpio_reg.h"
#include "pins.h"

namespace fan {

constexpr uint32_t kPwmHz = 25000;
constexpr uint8_t kPwmBits = 8;
constexpr uint32_t kMinLowUs = 1000;   // a pulse counts only if the line stayed low >= 1 ms

// Written only by the interrupt handler; read and cleared with interrupts paused.
static volatile uint32_t vPulses = 0;
static volatile uint32_t vEdges = 0;
static volatile uint32_t vLowStartUs = 0;
static volatile bool vWasLow = false;

// GPIO34 level straight from the register — digitalRead is not guaranteed ISR-safe.
static inline bool IRAM_ATTR tachIsLow() {
  return ((REG_READ(GPIO_IN1_REG) >> (pins::kFanTach - 32)) & 1) == 0;
}

static void IRAM_ATTR onTach() {
  uint32_t now = micros();
  vEdges = vEdges + 1;
  bool low = tachIsLow();
  if (low && !vWasLow) {
    vLowStartUs = now;
    vWasLow = true;
  } else if (!low && vWasLow) {
    if (now - vLowStartUs >= kMinLowUs) vPulses = vPulses + 1;
    vWasLow = false;
  }
}

static uint8_t gDuty = 0;
static bool gDutyWritten = false;
static uint32_t gRpm = 0;
static uint32_t gWindowStartMs = 0;
static uint32_t gPulsesTotal = 0, gEdgesTotal = 0;
static uint32_t gLastPulses = 0, gLastEdges = 0;

void begin() {
  pinMode(pins::kFanTach, INPUT);            // mandatory: GPIO34's input buffer is off by default
  vWasLow = tachIsLow();                     // starting state from the pin, before attaching
  attachInterrupt(digitalPinToInterrupt(pins::kFanTach), onTach, CHANGE);
  ledcAttach(pins::kFanPwm, kPwmHz, kPwmBits);
  gWindowStartMs = millis();
}

void setDuty(uint8_t pct) {
  if (pct > 100) pct = 100;
  if (gDutyWritten && pct == gDuty) return;
  ledcWrite(pins::kFanPwm, (uint32_t)pct * 255 / 100);
  gDuty = pct;
  gDutyWritten = true;
}

uint8_t duty() { return gDuty; }

bool update(uint32_t nowMs, uint32_t windowMs) {
  uint32_t dt = nowMs - gWindowStartMs;
  if (dt < windowMs) return false;
  noInterrupts();
  uint32_t p = vPulses, e = vEdges;
  vPulses = 0;
  vEdges = 0;
  interrupts();
  gWindowStartMs = nowMs;
  gPulsesTotal += p;
  gEdgesTotal += e;
  gLastPulses = p;
  gLastEdges = e;
  gRpm = (uint32_t)((uint64_t)p * 60000ULL / 2 / dt);   // two pulses per revolution
  return true;
}

uint32_t rpm() { return gRpm; }
uint32_t pulsesTotal() { return gPulsesTotal; }
uint32_t transitionsTotal() { return gEdgesTotal; }
uint32_t pwmReadback() { return ledcRead(pins::kFanPwm); }
uint32_t lastPulses() { return gLastPulses; }
uint32_t lastTransitions() { return gLastEdges; }

}  // namespace fan
