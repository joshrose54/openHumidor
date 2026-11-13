// =====================================================================
// BP2 test 26 — the SD card power switch (Rev Q), using the firmware's own driver.
//
// A card in J22. g mounts it and writes a line; n releases the SD lines (inputs) and drives
// GPIO39 high (card off); n sets GPIO39 to an input with its pull-up off (the soft start), and
// 10 ms later remounts and writes again. GPIO39 is never driven low (3A.11).
// You meter J22's VDD (C53's pad) to GND (TP3), and 3V3Q (TP8) on min-hold through the switch-on.
// x = the card switched back on.
// =====================================================================
#include <Arduino.h>
#include <SD.h>
#include "bp2/common/bp2_ui.h"
#include "wineador/bp2_steps.h"
#include "wineador/sd_power_seq.h"
#include "drivers/sd_card.h"
#include "drivers/sd_power.h"
#include "pins.h"

namespace {

const char* const kBanner[] = {
  "A card in J22. Nothing else needed. The card is powered from boot (GPIO39 an input).",
  "Before step 1: note 3V3Q (TP8) to GND (TP3) at idle - you need it for step 3.",
  "Meter the card's supply at C53's pad (J22 VDD) to GND (TP3).",
  nullptr,
};

const char* kTitles[] = {
  "1. Card ON: mounted and written",
  "2. Card OFF: SD lines released (inputs), GPIO39 driven HIGH",
  "3. Card ON again: GPIO39 an input, pull-up off (soft start); remounted and written",
};
const char* kChecks[] = {
  "C53 (J22 VDD) to GND (TP3): 3.23-3.37V. The write above says OK. Now set the meter to MIN-HOLD on 3V3Q (TP8) "
  "ready for step 3 (or keep it on C53 for step 2 first)",
  "C53 (J22 VDD) to GND (TP3): UNDER 0.1V. No reset",
  "C53 to GND (TP3): 3.23-3.37V again. (\"On within about 5 ms\" is a SCOPE check only - a meter can't "
  "see it; on the meter just check 3.23-3.37V.) Remount and write say OK above. No reset. "
  "3V3Q (TP8) min-hold: less than 0.1V below its idle reading",
};

wineador::StepSequencer gSeq;
bool gWaitRemount = false;
uint32_t gOnMs = 0;
uint32_t gWrites = 0;

bool mountAndWrite(const char* why) {
  const bool m = SD.begin(pins::kSdCs, sdcard::spi(), sdcard::kSpiHz);
  bool w = false;
  if (m) {
    if (!SD.exists("/bp2")) SD.mkdir("/bp2");
    File f = SD.open("/bp2/t26.txt", FILE_APPEND);
    if (f) {
      w = f.printf("write %lu at uptime %lu ms (%s)\n", (unsigned long)++gWrites, (unsigned long)millis(), why) > 0;
      f.close();
    }
  }
  Serial.printf("  %s: mount %s, write %s\n", why, m ? "OK" : "FAILED", w ? "OK" : "FAILED");
  return m && w;
}

void show(int i) {
  bp2::Live l;
  l.showLoads = false;
  l.sdPower = sdpower::powered() ? "ON (GPIO39 an input, pull-up off)" : "OFF (GPIO39 driven HIGH; SD lines inputs)";
  bp2::printStep(i, 3, kTitles[i], l, kChecks[i], i + 1 < 3 ? kTitles[i + 1] : nullptr);
}

}  // namespace

void setup() {
  bp2::begin("BP2 TEST 26: SD CARD POWER SWITCH", kBanner);
  const bool m = sdcard::begin();           // sets GPIO39 to the soft-start "on" state, then mounts
  Serial.printf("  Card at boot: %s. Type  g  for step 1.\n", m ? "mounted" : "NOT MOUNTED - check the card");
  gSeq.begin(3);
}

void loop() {
  const uint32_t now = millis();
  if (sdpower::update(now)) { gWaitRemount = true; gOnMs = now; }   // just switched on
  if (gWaitRemount && now - gOnMs >= wineador::SdPowerSeq::kSettleMs) {
    gWaitRemount = false;
    mountAndWrite("step 3, after the switch-on");
    show(2);
  }
  char w[32];
  if (!bp2::readLine(w, sizeof(w))) return;
  using E = wineador::StepSequencer::Event;
  switch (gSeq.handle(w)) {
    case E::Start: mountAndWrite("step 1"); show(0); break;
    case E::Step:
      if (gSeq.step() == 1) {
        sdpower::requestOff(now);
        sdpower::update(now);               // release the lines
        sdpower::update(now);               // then GPIO39 high
        show(1);
      } else {
        if (sdpower::onAllowedInMs(now)) Serial.printf("  (switch-on waits %lu ms more: at least 100 ms off)\n",
                                                       (unsigned long)sdpower::onAllowedInMs(now));
        sdpower::requestOn();               // done in loop() when allowed; step 3 prints then
      }
      break;
    case E::Finish:
      bp2::printFinished("C53 volts in steps 1, 2, 3; the TP8 idle and min-hold readings; the mount/write results");
      break;
    case E::Abort:
      sdpower::requestOn();
      bp2::printAborted();
      break;
    case E::Unknown: Serial.printf("  '%s' - type %s (x = card back on).\n", w, gSeq.waiting() ? "g" : "n"); break;
    case E::None: break;
  }
}
