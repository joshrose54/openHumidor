// =====================================================================
// BP2 tests 24 and 25 — the hardware watchdog (Rev Q), using the firmware's own driver.
//
// At boot GPIO1 is an input with both pulls off: the watchdog (U5) is disabled.
//   g       GPIO1 starts changing level every pass of this loop (as the firmware does).
//   stall   the toggling stops; this sketch keeps printing the ms since 'stall' every 100 ms,
//           so the last line before the reset shows how long U5 took (1.12-2.4 s). Use a
//           stopwatch too. After the reset GPIO1 is an input again until g.
//   pause   GPIO1 back to an input, both pulls off (the watchdog disabled). Then 'stall':
//           NO reset; after 10 s it says so.
//   resume  toggling again.
// Test 25: with it toggling (after g), flash this same sketch over USB-C, three times.
// =====================================================================
#include <Arduino.h>
#include "bp2/common/bp2_ui.h"
#include "drivers/hw_watchdog.h"

namespace {

const char* const kBanner[] = {
  "R26 (0R) fitted. Nothing else needed. GPIO1 is an INPUT now: the hardware watchdog is OFF.",
  "Test 24: g, then  stall  with a stopwatch: ONE reset 1.12-2.4 s later, then this banner again.",
  "         Then g,  pause , then  stall : NO reset (it says so after 10 s).",
  "Test 25: g (toggling), then flash this sketch over USB-C:  pio run -e bp2_24_watchdog -t upload",
  "         Three times; each must complete. (In download mode GPIO1 is undriven: the watchdog is off.)",
  "Commands: g, stall, pause, resume.",
  nullptr,
};

bool gStalled = false;
uint32_t gStallMs = 0, gLastPrintMs = 0, gLastStatusMs = 0;

const char* state() { return !hwdog::running() ? "INPUT, both pulls off (U5 disabled)" : gStalled ? "STOPPED (stalled)" : "TOGGLING every pass"; }

void showState() {
  bp2::Live l;
  l.showLoads = false;
  l.hwdog = state();
  bp2::printLive(l);
}

}  // namespace

void setup() {
  hwdog::begin();                         // input, both pulls off: disabled
  bp2::begin("BP2 TESTS 24-25: HARDWARE WATCHDOG", kBanner);
  showState();
  Serial.println("  Type  g  to start toggling GPIO1.");
}

void loop() {
  const uint32_t now = millis();
  if (!gStalled) hwdog::kick(now);        // every pass, from the loop (does nothing until started)
  if (gStalled && now - gLastPrintMs >= 100) {
    gLastPrintMs = now;
    const uint32_t ms = now - gStallMs;
    Serial.printf("  stalled %lu ms%s\n", (unsigned long)ms, hwdog::running() ? "" : " (watchdog paused: expect NO reset)");
    if (!hwdog::running() && ms >= 10000) {
      Serial.println("  10 s with GPIO1 an input and no reset: PASS (the paused watchdog can't reset the board).");
      Serial.println("  Stall ended. 'resume' to toggle again.");
      gStalled = false;
    }
  }
  if (!gStalled && hwdog::running() && now - gLastStatusMs >= 5000) {
    gLastStatusMs = now;
    Serial.printf("  toggling; longest gap between changes %lu ms (limit 900)\n", (unsigned long)hwdog::longestGapMs());
  }
  char w[32];
  if (!bp2::readLine(w, sizeof(w))) return;
  if (strcmp(w, "g") == 0) {
    hwdog::start(now);                    // the first time (start works once only, as in the firmware)
    hwdog::resume(now);                   // after a pause
    gStalled = false;
    showState();
  }
  else if (strcmp(w, "stall") == 0) {
    gStalled = true;
    gStallMs = now;
    gLastPrintMs = now;
    Serial.printf("  STALL: toggling stopped at uptime %lu ms. %s\n", (unsigned long)now,
                  hwdog::running() ? "Stopwatch: expect a reset in 1.12-2.4 s." : "The watchdog is paused: expect no reset.");
  } else if (strcmp(w, "pause") == 0) { hwdog::pause(); showState(); }
  else if (strcmp(w, "resume") == 0) { hwdog::resume(now); gStalled = false; showState(); }
  else Serial.printf("  '%s' - type g, stall, pause or resume.\n", w);
}
