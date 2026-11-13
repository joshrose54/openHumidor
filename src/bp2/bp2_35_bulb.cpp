// =====================================================================
// BP2 test 35 — the bulb surge (Rev Q). USB OUT; the J23 monitor may stay on.
//
// The 21 W 12V bulb on J10 pins 3-4 (the KSD9700 in series is optional for the bulb). After g: K1 energised, then the bulb ON 2 s, OFF 3 s,
// 50 times. BP2 runs it TWICE: first with the hardware watchdog paused (the default), so a
// freeze stays a freeze and shows in the log (the log just stops); then after 'hwdog on', as
// the firmware runs (GPIO1 toggled every pass), when a freeze ends in a U5 reset instead.
// Every boot (with its reset reason) and every switch-on is written to the SD card,
// /bp2/bulb.csv, so the record survives a reset; check it on the Mac with
//     python3 tools/check_logs.py /Volumes/WINEADOR --bulb
// After a reset the board waits for g again and carries on from the cycle it had reached.
// x = everything off.
// =====================================================================
#include <Arduino.h>
#include <SD.h>
#include <stdlib.h>
#include "bp2/common/bp2_loads.h"
#include "drivers/sd_card.h"
#include "drivers/hw_watchdog.h"

namespace {

constexpr int kCycles = 50;
constexpr uint32_t kOnMs = 2000, kOffMs = 3000;
const char* kLog = "/bp2/bulb.csv";

const char* const kBanner[] = {
  "USB-C OUT (the native USB unplugged). The J23 monitor cable may stay on (GND, TX, RX only).",
  "The 21W 12V bulb on J10 pins 3-4 (KSD in series optional for the bulb). An SD card in J22 for the log.",
  "After g: K1 energised; the bulb ON 2 s, OFF 3 s, 50 times. Watch for a freeze or a reset.",
  "RUN IT TWICE (BP2 test 35):",
  "  Run 1: as it starts - hardware watchdog PAUSED (GPIO1 an input), so a freeze shows as a freeze. Type g.",
  "  Run 2: afterwards, type  hwdog on  then  g  - the watchdog toggling every pass, as the firmware runs.",
  "  (A reset during run 2 restarts with the watchdog paused: type  hwdog on  again, then  g .)",
  "A freeze: the count stops and nothing answers - switch the 12V off and on, then check the card.",
  "If the board resets, this banner prints again: type g and it carries on from the cycle it reached.",
  "Afterwards, the card on the Mac:  python3 tools/check_logs.py /Volumes/WINEADOR --bulb",
  nullptr,
};

const bp2::LoadStep kRelay = {"K1 energised, bulb off", "", true, false, 0, false, false, false, -1, -1};
const bp2::LoadStep kOn    = {"K1 energised, bulb ON", "", true, true, 0, false, false, false, -1, -1};

bool gSd = false, gRunning = false, gOnNow = false, gDog = false;
int gCycle = 0;                 // cycles completed
uint32_t gEdgeMs = 0;

void logLine(const char* kind, int n) {
  Serial.printf("  %s %d at uptime %lu ms\n", kind, n, (unsigned long)millis());
  if (!gSd) return;
  File f = SD.open(kLog, FILE_APPEND);
  if (!f) return;
  f.printf("%lu,%s,%d,%s\n", (unsigned long)millis(), kind, n, kind[0] == 'b' ? bp2::resetReason() : "");
  f.close();
}

// The last cycle completed, from the log (so a reset doesn't start the count again).
int lastCycle() {
  if (!gSd) return 0;
  File f = SD.open(kLog, FILE_READ);
  if (!f) return 0;
  int last = 0;
  char line[64];
  while (f.available()) {
    const size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
    line[n] = 0;
    const char* k = strchr(line, ',');
    if (k && strncmp(k + 1, "off,", 4) == 0) last = atoi(k + 5);
    if (k && strncmp(k + 1, "done,", 5) == 0) last = 0;       // a finished run: a new run starts at 0
  }
  f.close();
  return last;
}

void stopAll(const char* why) {
  bp2::loadsAllOff();
  gRunning = false;
  gOnNow = false;
  logLine(why, gCycle);
}

}  // namespace

void setup() {
  bp2::loadsBegin();                    // every load off from the first instruction
  hwdog::begin();                       // GPIO1 an input until the loop runs
  bp2::begin("BP2 TEST 35: BULB SURGE", kBanner);
  gSd = sdcard::begin();
  if (gSd && !SD.exists("/bp2")) SD.mkdir("/bp2");
  gCycle = lastCycle();
  logLine("boot", gCycle);
  Serial.printf("  SD log: %s. Cycles done so far: %d of %d. Type  g  to %s.\n", gSd ? kLog : "NO CARD (serial only)", gCycle,
                kCycles, gCycle ? "carry on" : "start");
}

void loop() {
  const uint32_t now = millis();
  hwdog::kick(now);                     // only once 'hwdog on' has started it: every pass, as the firmware
  bp2::loadsUpdate(now);
  if (gRunning) {
    if (!gOnNow && now - gEdgeMs >= kOffMs) {
      if (gCycle >= kCycles) {
        stopAll("done");
        bp2::printFinished("did it freeze or reset (how many times, at which cycle)? Then run check_logs.py --bulb on the card");
        return;
      }
      bp2::loadsApply(kOn, now);
      gOnNow = true;
      gEdgeMs = now;
      logLine("on", gCycle + 1);
    } else if (gOnNow && now - gEdgeMs >= kOnMs) {
      bp2::loadsApply(kRelay, now);
      gOnNow = false;
      gEdgeMs = now;
      gCycle++;
      logLine("off", gCycle);
    }
  }
  char w[32];
  if (!bp2::readLine(w, sizeof(w))) return;
  if (strcmp(w, "g") == 0 && !gRunning) {
    if (gCycle >= kCycles) gCycle = 0;
    bp2::loadsApply(kRelay, now);       // K1 first; the bulb's first ON comes 3 s later
    gRunning = true;
    gEdgeMs = now;
    bp2::Live l = bp2::loadsLive();
    bp2::printLive(l);
    Serial.printf("  Running: cycle %d of %d next; hardware watchdog %s. x = stop.\n", gCycle + 1, kCycles,
                  gDog ? "ON" : "OFF");
  } else if (strcmp(w, "hwdog on") == 0) {
    hwdog::start(now);
    hwdog::resume(now);
    gDog = true;
    logLine("hwdog_on", gCycle);
    Serial.println("  Hardware watchdog ON: GPIO1 toggled every pass (a freeze now ends in a U5 reset).");
  } else if (strcmp(w, "hwdog off") == 0) {
    hwdog::pause();
    gDog = false;
    logLine("hwdog_off", gCycle);
    Serial.println("  Hardware watchdog OFF: GPIO1 an input (a freeze stays a freeze).");
  } else if (strcmp(w, "x") == 0) {
    stopAll("abort");
    bp2::printAborted();
  } else {
    Serial.printf("  '%s' - type %s.\n", w, gRunning ? "x to stop" : "g (or hwdog on / hwdog off)");
  }
}
