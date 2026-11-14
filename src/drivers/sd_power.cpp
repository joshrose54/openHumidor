#include "sd_power.h"
#include <Arduino.h>
#include <SD.h>
#include "driver/gpio.h"
#include "wineador/sd_power_seq.h"
#include "sd_card.h"
#include "pins.h"

namespace sdpower {

using wineador::SdPowerSeq;
static SdPowerSeq gSeq;
static uint32_t gCycles = 0;

// GPIO39 to an input with its internal pull-up off: the soft start (3A.11). Never driven low.
static void softOn() {
  gpio_set_direction((gpio_num_t)pins::kSdPower, GPIO_MODE_INPUT);
  gpio_pullup_dis((gpio_num_t)pins::kSdPower);
  gpio_pulldown_dis((gpio_num_t)pins::kSdPower);
}

// High first, then an output: the pin goes straight from input (card on) to driven high (off).
static void cutPower() {
  gpio_set_level((gpio_num_t)pins::kSdPower, 1);
  gpio_set_direction((gpio_num_t)pins::kSdPower, GPIO_MODE_OUTPUT);
}

// Unmount and let go of the four SD lines, so the card isn't back-fed through them (3A.11).
static void releaseBus() {
  SD.end();
  sdcard::spi().end();
  for (int p : {pins::kSdSck, pins::kSdMosi, pins::kSdMiso, pins::kSdCs}) {
    pinMode(p, INPUT);
    gpio_pullup_dis((gpio_num_t)p);
    gpio_pulldown_dis((gpio_num_t)p);
  }
}

static void restartBus() { sdcard::spi().begin(pins::kSdSck, pins::kSdMiso, pins::kSdMosi, pins::kSdCs); }

// Does one Action. Returns true for SoftOn / Remount (the card is powered again).
static bool doAction(SdPowerSeq::Action a) {
  switch (a) {
    case SdPowerSeq::Action::ReleaseBus: releaseBus(); return false;
    case SdPowerSeq::Action::CutPower:   cutPower(); return false;
    case SdPowerSeq::Action::SoftOn:     softOn(); return true;
    case SdPowerSeq::Action::Remount:    restartBus(); return true;   // the caller mounts the card
    case SdPowerSeq::Action::None:       return false;
  }
  return false;
}

void begin() { softOn(); }
bool fitted() { return true; }
uint32_t cycles() { return gCycles; }

bool cycle() {
  if (gSeq.busy()) return false;
  gSeq.startCycle(millis());
  // ~160 ms in the SD task only: the sequence says when each step is due.
  for (int guard = 0; guard < 1000; guard++) {
    const SdPowerSeq::Action a = gSeq.next(millis());
    if (a == SdPowerSeq::Action::None) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }
    doAction(a);
    if (a == SdPowerSeq::Action::Remount) { gCycles = gCycles + 1; return true; }
  }
  return false;                       // never reached in practice (1000 x 5 ms = 5 s)
}

void requestOff(uint32_t nowMs) { gSeq.requestOff(nowMs); }
void requestOn() { gSeq.requestOn(); }
bool update(uint32_t nowMs) {
  bool on = false;
  for (int i = 0; i < 4; i++) {         // the steps due now, at most one of each
    const SdPowerSeq::Action a = gSeq.next(nowMs);
    if (a == SdPowerSeq::Action::None) break;
    if (doAction(a)) on = true;
    if (a == SdPowerSeq::Action::SoftOn) { restartBus(); break; }
  }
  return on;
}
bool powered() { return gSeq.powered(); }
uint32_t onAllowedInMs(uint32_t nowMs) { return gSeq.onAllowedInMs(nowMs); }

}  // namespace sdpower
