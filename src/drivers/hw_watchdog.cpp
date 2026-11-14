#include "hw_watchdog.h"
#include <Arduino.h>
#include "driver/gpio.h"
#include "wineador/wdi_toggle.h"
#include "pins.h"

namespace hwdog {

static wineador::WdiToggle gT;

// Input, both internal pulls off, no pad hold: an undriven WDI disables U5's watchdog (3A.5).
static void toInput() {
  gpio_hold_dis((gpio_num_t)pins::kWatchdog);
  pinMode(pins::kWatchdog, INPUT);
  gpio_pullup_dis((gpio_num_t)pins::kWatchdog);
  gpio_pulldown_dis((gpio_num_t)pins::kWatchdog);
}

static void drive() {
  digitalWrite(pins::kWatchdog, gT.level() ? HIGH : LOW);   // level first, then the pin to an output
  pinMode(pins::kWatchdog, OUTPUT);
  digitalWrite(pins::kWatchdog, gT.level() ? HIGH : LOW);
}

void begin() { toInput(); }

void start(uint32_t nowMs) {
  if (gT.running()) return;
  gT.start(nowMs);
  drive();
}

void kick(uint32_t nowMs) {
  if (!gT.running()) return;
  digitalWrite(pins::kWatchdog, gT.pass(nowMs) ? HIGH : LOW);
}

void pause() {
  gT.pause();
  toInput();
}

void resume(uint32_t nowMs) {
  if (gT.running()) return;
  gT.resume(nowMs);
  drive();
}

bool fitted() { return true; }
bool running() { return gT.running(); }
uint32_t longestGapMs() { return gT.longestGapMs(); }
bool takeLongGap(uint32_t* gapMs) { return gT.takeLongGap(gapMs); }

}  // namespace hwdog
