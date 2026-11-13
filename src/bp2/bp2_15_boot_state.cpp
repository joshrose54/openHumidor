// =====================================================================
// BP2 tests 15, 16, 17 — first flash, boot state, loads held in reset (Rev Q)
//
// This sketch NEVER drives a pin. The load pins (GPIO4-7, GPIO17), the blower pin (GPIO15) and
// the hardware watchdog pin (GPIO1) are read as plain inputs, no pulls, so the board's own
// pull-ups and pull-downs alone decide them. It switches nothing, so there is no "g".
// Every 2 seconds it prints what each pin reads and what it should read.
//
//   Test 15: it flashed, and this banner appeared on J23.
//   Test 16: U9 fitted; meter while it runs (below).
//   Test 17: hold RESET (SW1) and meter again; the printing stops while it is held.
// =====================================================================
#include <Arduino.h>
#include "esp_chip_info.h"
#include "bp2/common/bp2_ui.h"
#include "pins.h"
#include "drivers/heap.h"

namespace {

struct PinCheck { int pin; const char* name; int expect; };   // expect: 0 low, 1 high
const PinCheck kPins[] = {
  {pins_revq::kCoolingSsr, "GPIO4  cooling (SSR)", 0},
  {pins_revq::kHeater,     "GPIO5  heater", 0},
  {pins_revq::kRelayK1,    "GPIO6  relay K1", 0},
  {pins_revq::kHumidifier, "GPIO7  humidifier", 0},
  {pins_revq::kIon,        "GPIO17 ion generator", 0},
  {pins_revq::kBlowerPwm,  "GPIO15 blowers (pull-UP: full)", 1},
  {pins_revq::kWatchdog,   "GPIO1  hw watchdog (undriven = off)", -1},
};
uint32_t gLastMs = 0;

const char* const kBanner[] = {
  "Drives NOTHING: every load pin is left to the board's pull-ups and pull-downs.",
  "Test 15: this banner on J23 = flashed and running.",
  "Test 16 (power off first: U9 in its socket, pin 1 to the notch; four blowers on J12-J15):",
  "  meter GPIO4-7 and GPIO17 at their pull-down pads to GND (TP3): all ~0V;",
  "  each gate Q2-Q6 and Q11 to GND (TP3): ~0V; Q7 (blowers) high;",
  "  J10 pin 1 ~12V, pin 3 ~0V (K1 de-energised);",
  "  12V (TP6) to BLW- (TP12): 11.5V or more (blowers at full speed); ion generator off.",
  "Test 17: HOLD RESET (SW1): each gate Q2-Q6, Q11 to GND (TP3) under 0.1V;",
  "  12V (TP6) to BLW- (TP12) still 11.5V or more. Release: this banner prints again.",
  nullptr,
};

}  // namespace

void setup() {
  for (const PinCheck& p : kPins) pinMode(p.pin, INPUT);   // input, no pulls: nothing is driven
  bp2::begin("BP2 TESTS 15-17: BOOT STATE (nothing driven)", kBanner);
  esp_chip_info_t c;
  esp_chip_info(&c);
  Serial.printf("  Chip: ESP32-S3 rev %d, %d cores; flash %lu MB; PSRAM %lu KB found%s\n", (int)c.revision, (int)c.cores,
                (unsigned long)(ESP.getFlashChipSize() / (1024 * 1024)), (unsigned long)(heap::psramTotal() / 1024),
                heap::psramTotal() ? "" : "  <-- CHECK: none found (expected 2048 KB)");
  Serial.println("  Record: the reset reason above, and pass/fail for each meter reading.");
}

void loop() {
  const uint32_t now = millis();
  if (now - gLastMs < 2000) return;
  gLastMs = now;
  bool ok = true;
  Serial.printf("\n  uptime %lu s - pins as read (nothing driven):\n", (unsigned long)(now / 1000));
  for (const PinCheck& p : kPins) {
    const int v = digitalRead(p.pin);
    const bool good = p.expect < 0 || v == p.expect;
    ok = ok && good;
    Serial.printf("    %-36s %-4s %s%s\n", p.name, v ? "HIGH" : "LOW",
                  p.expect < 0 ? "(either: undriven)" : p.expect ? "(expect HIGH)" : "(expect LOW)", good ? "" : "  <-- CHECK");
  }
  Serial.println(ok ? "  ALL AS EXPECTED" : "  SOMETHING NOT AS EXPECTED");
}
