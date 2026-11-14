#include "supply_sense.h"
#include <Arduino.h>
#include "pins.h"
#include "board.h"

namespace supplysense {

static wineador::SupplySense gLogic;
static uint32_t gPinMv = 0;

static uint32_t gLastMs = 0;

void begin() {
  // 12 dB: the full range to about 3.1 V at the pin, ~24 V on the rail. (Arduino's name for
  // the 12 dB setting is ADC_11db.) At the next range down it clips at about 12.3 V (3A.11).
  analogSetPinAttenuation(pins::k12vSense, ADC_11db);
  gLastMs = millis();
}

void update(uint32_t nowMs) {
  if (nowMs - gLastMs < wineador::SupplySense::kSampleMs) return;
  gLastMs += wineador::SupplySense::kSampleMs;
  if (nowMs - gLastMs >= wineador::SupplySense::kSampleMs) gLastMs = nowMs;   // fell behind: no burst of reads
  gPinMv = analogReadMilliVolts(pins::k12vSense);    // ADC1 one-shot, the chip's own calibration
  gLogic.add(wineador::supplyVolts(gPinMv, board::k12vScale), nowMs);
}

bool fitted() { return true; }

wineador::SupplySense& logic() { return gLogic; }
uint32_t lastPinMv() { return gPinMv; }

}  // namespace supplysense
