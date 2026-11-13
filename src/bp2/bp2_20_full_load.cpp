// =====================================================================
// BP2 test 20 — full load and the 12V rail (Rev Q).
//
// Loads: four blowers on J12-J15, heater pad on J10 3-4, SSR input on J10 1-2 (NO mains),
// humidifier fan on J11, ion generator on J16, plenum fan on J2. Loads are added one at a time;
// the heater with K1 energised, so the SSR stays off (the interlock). At "everything on" WiFi
// connects and keeps sending small packets to the router (a host on the home network only).
// Type  r <volts>  with the R21/R22 drop to have U8's input current worked out (drop / 3.4 ohm).
// Nothing switches until g. Each n applies the next step. x = everything off.
// =====================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <stdlib.h>
#include "bp2/common/bp2_loads.h"
#include "wineador/bp2_calc.h"

// The WiFi name and password (never printed). Its other contents are unused here.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "wifi_time.h"
#pragma GCC diagnostic pop

namespace {

using bp2::LoadStep;
//                title, check,                                                   K1     heat   Hz  SSR    humid  ion    blw  fan
const LoadStep kSteps[] = {
  {"1. Baseline: blowers and plenum fan stopped, every output off",
   "Record the PSU current",
   false, false, 0, false, false, false, 0, 0},
  {"2. + Plenum fan 100%",
   "Record the PSU current. The fan runs",
   false, false, 0, false, false, false, 0, 100},
  {"3. + Blowers 100% (all four)",
   "Record the PSU current",
   false, false, 0, false, false, false, 100, 100},
  {"4. + Relay K1 energised (heater still off)",
   "K1 clicks. Record the PSU current",
   true, false, 0, false, false, false, 100, 100},
  {"5. + Heater pad ON",
   "Record the PSU current",
   true, true, 0, false, false, false, 100, 100},
  {"6. + Humidifier fan ON",
   "Record the PSU current",
   true, true, 0, false, true, false, 100, 100},
  {"7. + Ion generator ON, WiFi connecting and transmitting - EVERYTHING ON",
   "Record the PSU current once WiFi shows CONNECTED. Then meter: J1's + pin to 12V (TP6) - 0.1V or less; "
   "12V (TP6) to GND (TP7) - 11.4V or more; 5V (TP1) to GND (TP7) - 4.9-5.1V; across R21/R22 - 2.0V or less "
   "(type  r <volts>); C12's + pad to GND (TP7) - above 9.5V. After 2 minutes on, touch Q1, Q3, Q11, U8, D1: "
   "warm at most",
   true, true, 0, false, true, true, 100, 100},
};

const char* const kBanner[] = {
  "Loads (power off while wiring): four blowers on J12-J15, heater pad + KSD on J10 3-4, SSR input on",
  "J10 1-2 (NO mains), humidifier fan on J11, ion generator on J16, plenum fan on J2.",
  "PSU 12.0V through the 4A fuse into J1. Record the PSU current at every step.",
  "The heater switches itself off after 5 minutes on; its 5 minutes start again at step 7, for the readings.",
  "At step 7:  r <volts>  = the R21/R22 drop -> U8's input current (drop / 3.4 ohm), pass at 2.0V or less.",
  nullptr,
};

// ---- WiFi for step 7: connected and sending ~50 KB/s of small packets to the router.
bool gWifiWanted = false;
WiFiUDP gUdp;
uint8_t gPacket[512];
uint32_t gLastSendMs = 0, gSent = 0, gOnSinceMs = 0, gLastNoteMs = 0;
char gWifiLine[96];

void onStep(int i, uint32_t nowMs) {
  if (i == 6) bp2::loadsRestartHeaterLimit(nowMs);   // step 7's readings + 2 min: a fresh 5 min of heater
  const bool want = (i == 6);
  if (want && !gWifiWanted) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_TIME_SSID, WIFI_TIME_PASSWORD);   // returns at once; connects in the background
    gOnSinceMs = nowMs;
    gLastNoteMs = nowMs;
  } else if (!want && gWifiWanted) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  gWifiWanted = want;
}

void everyPass(uint32_t nowMs) {
  if (!gWifiWanted) return;
  if (WiFi.status() == WL_CONNECTED && nowMs - gLastSendMs >= 10) {   // 512 bytes every 10 ms
    gLastSendMs = nowMs;
    if (gUdp.beginPacket(WiFi.gatewayIP(), 9)) {                      // port 9: discard
      gUdp.write(gPacket, sizeof(gPacket));
      if (gUdp.endPacket()) gSent++;
    }
  }
  if (nowMs - gLastNoteMs >= 30000) {
    gLastNoteMs = nowMs;
    Serial.printf("  everything on for %lu s; WiFi %s, %lu packets sent\n", (unsigned long)((nowMs - gOnSinceMs) / 1000),
                  WiFi.status() == WL_CONNECTED ? "CONNECTED" : "not connected yet", (unsigned long)gSent);
  }
}

const char* wifiLine() {
  if (!gWifiWanted) return "WIFI ............ off";
  snprintf(gWifiLine, sizeof(gWifiLine), "WIFI ............ %s, sending to the router (%lu packets)",
           WiFi.status() == WL_CONNECTED ? "CONNECTED" : "connecting", (unsigned long)gSent);
  return gWifiLine;
}

bool onLine(const char* line) {
  if (line[0] != 'r' || line[1] != ' ') return false;
  const float drop = strtof(line + 2, nullptr);
  Serial.printf("  R21/R22 drop %.2f V -> U8's input current %.2f A (drop / %.1f ohm): %s (pass at 2.0V or less)\n",
                (double)drop, (double)wineador::u8InputAmps(drop), (double)wineador::kInrushOhms,
                drop <= 2.0f ? "PASS" : "OVER 2.0V - CHECK");
  return true;
}

const bp2::StepSketch kSketch = {
  "BP2 TEST 20: FULL LOAD AND THE 12V RAIL", kBanner, kSteps, sizeof(kSteps) / sizeof(kSteps[0]),
  "the PSU current at each step; at step 7: J1 + to TP6, TP6 and TP1 to TP7, the R21/R22 drop and U8's "
  "input current, C12 + to TP7; the touch check",
  onStep, wifiLine, everyPass, onLine,
};

}  // namespace

void setup() {
  for (size_t i = 0; i < sizeof(gPacket); i++) gPacket[i] = (uint8_t)i;
  bp2::stepSketchSetup(kSketch);
}
void loop() { bp2::stepSketchLoop(); }
