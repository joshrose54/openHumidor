// =====================================================================
// BP2 test 21 — 3.3V under WiFi (Rev Q). 12V only, no loads.
//
// After g: over and over, WiFi connects, sends small packets to the router (a host on the home
// network only) for 5 s, and disconnects; meanwhile the SD card is written back to back and the
// plenum fan runs at 100%. You meter 3V3RF (TP2) on min-hold for 10 minutes (step 1), then
// 3V3Q (TP8) for 10 minutes (step 2). Every 10 s it prints the cycles, the card writes, any
// failure and the lowest free internal RAM. A reset would print the banner again.
// x = everything stops.
// =====================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <SD.h>
#include "bp2/common/bp2_ui.h"
#include "wineador/bp2_steps.h"
#include "drivers/sd_card.h"
#include "drivers/fan.h"
#include "drivers/heap.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "wifi_time.h"
#pragma GCC diagnostic pop

namespace {

const char* const kBanner[] = {
  "12V only; no loads (blowers, if fitted, run at full speed). An SD card in J22.",
  "After g: WiFi connect / send 5 s / disconnect, over and over; SD writes back to back; plenum fan 100%.",
  "Step 1: 3V3RF (TP2) to GND (TP3) on MIN-HOLD for 10 minutes: 3.2V or more.",
  "Step 2: 3V3Q (TP8) to GND (TP3) on MIN-HOLD for 10 minutes: 3.2V or more.",
  "No reset in either (the banner would print again). If a scope is to hand: look at TP2 and TP8",
  "for ringing (if seen: remove C20 or C28 - a bench fix - and tell Claude).",
  nullptr,
};

const char* kTitles[] = {
  "1. 3V3RF (TP2) on min-hold, 10 minutes, everything running",
  "2. 3V3Q (TP8) on min-hold, 10 minutes, everything running",
};
const char* kChecks[] = {
  "Meter 3V3RF (TP2) to GND (TP3) on MIN-HOLD. After 10 minutes record the minimum: 3.2V or more. No reset",
  "Meter 3V3Q (TP8) to GND (TP3) on MIN-HOLD. After 10 minutes record the minimum: 3.2V or more. No reset",
};

wineador::StepSequencer gSeq;
bool gRunning = false, gSdOk = false;
enum class W : uint8_t { Off, Connecting, Sending, Pause } gW = W::Off;
uint32_t gWifiMs = 0, gCycles = 0, gConnects = 0, gSent = 0, gWrites = 0, gWriteFails = 0, gLastPrintMs = 0;
WiFiUDP gUdp;
uint8_t gBuf[4096];

void wifiStep(uint32_t now) {
  switch (gW) {
    case W::Off: break;
    case W::Connecting:
      if (WiFi.status() == WL_CONNECTED) { gW = W::Sending; gWifiMs = now; gConnects++; }
      else if (now - gWifiMs > 20000) { WiFi.disconnect(true); gW = W::Pause; gWifiMs = now; }   // no connect in 20 s: retry
      break;
    case W::Sending:
      if (gUdp.beginPacket(WiFi.gatewayIP(), 9)) {     // port 9: discard
        gUdp.write(gBuf, 256);
        if (gUdp.endPacket()) gSent++;
      }
      if (now - gWifiMs >= 5000) { WiFi.disconnect(true); gW = W::Pause; gWifiMs = now; gCycles++; }
      break;
    case W::Pause:
      if (now - gWifiMs >= 2000) { WiFi.begin(WIFI_TIME_SSID, WIFI_TIME_PASSWORD); gW = W::Connecting; gWifiMs = now; }
      break;
  }
}

void sdStep() {
  if (!gSdOk) return;
  File f = SD.open("/bp2/t21.bin", FILE_APPEND);
  if (!f) { gWriteFails++; return; }
  const bool ok = f.write(gBuf, sizeof(gBuf)) == sizeof(gBuf);
  const size_t size = f.size();
  f.close();
  if (ok) gWrites++; else gWriteFails++;
  if (size > 2000000) SD.remove("/bp2/t21.bin");     // keep it small: start again past 2 MB
}

void start(uint32_t now) {
  gRunning = true;
  fan::begin();
  fan::setDuty(100);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_TIME_SSID, WIFI_TIME_PASSWORD);
  gW = W::Connecting;
  gWifiMs = now;
}

void stopAll() {
  gRunning = false;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  gW = W::Off;
}

void show(int i) {
  bp2::Live l;
  l.showLoads = false;
  l.fanPct = 100;
  l.wifi = "connect / send 5 s / disconnect, over and over";
  l.extra = gSdOk ? "SD CARD ......... written back to back (/bp2/t21.bin)" : "SD CARD ......... NOT MOUNTED - check the card";
  bp2::printStep(i, 2, kTitles[i], l, kChecks[i], i + 1 < 2 ? kTitles[i + 1] : nullptr);
}

}  // namespace

void setup() {
  for (size_t i = 0; i < sizeof(gBuf); i++) gBuf[i] = (uint8_t)('a' + i % 26);
  bp2::begin("BP2 TEST 21: 3.3V UNDER WIFI", kBanner);
  gSdOk = sdcard::begin();
  if (gSdOk && !SD.exists("/bp2")) SD.mkdir("/bp2");
  Serial.printf("  SD card: %s. Everything idle until  g .\n", gSdOk ? "mounted" : "NOT MOUNTED (the test needs one)");
  gSeq.begin(2);
}

void loop() {
  const uint32_t now = millis();
  if (gRunning) {
    wifiStep(now);
    sdStep();
    if (now - gLastPrintMs >= 10000) {
      gLastPrintMs = now;
      Serial.printf("  up %lu s | wifi cycles %lu, connects %lu, packets %lu | SD writes %lu, failed %lu | "
                    "lowest internal RAM %lu\n", (unsigned long)(now / 1000), (unsigned long)gCycles,
                    (unsigned long)gConnects, (unsigned long)gSent, (unsigned long)gWrites, (unsigned long)gWriteFails,
                    (unsigned long)heap::lowest());
    }
  }
  char w[32];
  if (!bp2::readLine(w, sizeof(w))) return;
  using E = wineador::StepSequencer::Event;
  switch (gSeq.handle(w)) {
    case E::Start: start(now); show(0); break;
    case E::Step: show(gSeq.step()); break;
    case E::Finish:
      stopAll();
      bp2::printFinished("the minimum on TP2 and on TP8; any reset; the last status line (SD failures, lowest RAM)");
      break;
    case E::Abort: stopAll(); bp2::printAborted(); break;
    case E::Unknown: Serial.printf("  '%s' - type %s (x = stop).\n", w, gSeq.waiting() ? "g to start" : "n for step 2"); break;
    case E::None: break;
  }
}
