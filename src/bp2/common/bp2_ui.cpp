#include "bp2_ui.h"
#include <Arduino.h>
#include <ctype.h>
#include <string.h>
#include "wineador/commands.h"
#include "drivers/reset_reason.h"

namespace bp2 {

static wineador::LineReader gIn;

void printTestPoints() {
  Serial.println("  Test points are printed with what they carry (no TP numbers on the board):");
  Serial.println("    5V = TP1    3V3RF = TP2   GND = TP3 (signal)   TACH = TP4   PWM = TP5   12V = TP6");
  Serial.println("    GND = TP7 (power)   3V3Q = TP8   3V3S = TP9   EN = TP10   IO0 = TP11   BLW- = TP12");
}

void begin(const char* title, const char* const* lines) {
  Serial.setTxBufferSize(4096);   // printing never waits unless 4 KB are queued
  Serial.begin(115200);
  Serial.println();
  Serial.println("=====================================================================");
  Serial.printf ("  %s   (Rev Q, BP2)\n", title);
  Serial.printf ("  Boot reason: %s\n", resetReason());
  if (strcmp(resetReason(), "POWERON") == 0)
    Serial.println("  (On the S3, POWERON also means EN was pulled low: U5's watchdog, RESET (SW1) or a supply dip -\n"
                   "   the chip can't tell these apart. A watchdog reset reads POWERON too.)");
  Serial.println("=====================================================================");
  for (int i = 0; lines && lines[i]; i++) Serial.printf("  %s\n", lines[i]);
  Serial.println("  Power: USB and 12V in any order; flashing and this monitor need 12V on.");
  Serial.println("  Monitor cable on J23: GND, TX, RX only - never its VCC wire.");
  printTestPoints();
  Serial.println("---------------------------------------------------------------------");
}

bool readLine(char* out, size_t n) {
  for (int i = 0; i < 64 && Serial.available(); i++) {
    if (!gIn.feed((char)Serial.read())) continue;
    const char* s = gIn.line();
    while (*s == ' ' || *s == '\t') s++;
    size_t k = 0;
    for (; *s && k + 1 < n; s++) out[k++] = (char)tolower((unsigned char)*s);
    while (k > 0 && (out[k - 1] == ' ' || out[k - 1] == '\t')) k--;
    out[k] = 0;
    return true;
  }
  return false;
}

static const char* onOff(bool b) { return b ? "ON" : "OFF"; }

void printLive(const Live& l) {
  Serial.println("  NOW LIVE:");
  if (l.showLoads) {
    Serial.printf("    RELAY K1 ........ %s\n", l.k1 ? "ON  (heating side)" : "OFF (cooling side)");
    if (l.heat && l.heaterHz > 0) Serial.printf("    HEATER signal ... PWM %.0f Hz\n", (double)l.heaterHz);
    else                          Serial.printf("    HEATER signal ... %s\n", onOff(l.heat));
    Serial.printf("    SSR signal ...... %s\n", onOff(l.ssr));
    Serial.printf("    HUMID FAN ....... %s\n", onOff(l.humid));
    Serial.printf("    ION generator ... %s\n", onOff(l.ion));
  }
  if (l.blowerPct >= 0) Serial.printf("    BLOWERS ......... %d%%%s\n", l.blowerPct, l.blowerPct == 100 ? " (full)" : "");
  if (l.fanPct >= 0) Serial.printf("    PLENUM FAN ...... %d%%\n", l.fanPct);
  if (l.backlightPct >= 0) Serial.printf("    BACKLIGHT ....... %d%%\n", l.backlightPct);
  if (l.sdPower) Serial.printf("    SD CARD POWER ... %s\n", l.sdPower);
  if (l.hwdog) Serial.printf("    HW WATCHDOG ..... %s\n", l.hwdog);
  if (l.wifi) Serial.printf("    WIFI ............ %s\n", l.wifi);
  if (l.extra) Serial.printf("    %s\n", l.extra);
}

void printStep(int i, int n, const char* title, const Live& l, const char* check, const char* nextTitle) {
  Serial.println();
  Serial.println("#####################################################################");
  Serial.printf ("  STEP %d of %d  -  NOW ACTIVE\n", i + 1, n);
  Serial.printf ("  %s\n", title);
  Serial.println("#####################################################################");
  printLive(l);
  Serial.println("  -------------------------------------------------------------------");
  Serial.printf ("  CHECK NOW:  %s\n", check);
  Serial.println("  -------------------------------------------------------------------");
  if (nextTitle) Serial.printf("  When done, type  n  for step %d: %s\n", i + 2, nextTitle);
  else           Serial.println("  When done, type  n  to switch everything off and finish.");
  Serial.println("  (x = abort: everything off)");
}

void printFinished(const char* sendClaude) {
  Serial.println();
  Serial.println("=====================================================================");
  Serial.println("  FINISHED - everything off, relay de-energised.");
  if (sendClaude && sendClaude[0]) Serial.printf("  Send Claude: %s\n", sendClaude);
  Serial.println("  Type  g  to run it again.");
  Serial.println("=====================================================================");
}

void printAborted() {
  Serial.println();
  Serial.println("*** ABORTED - everything off, relay de-energised. Type  g  to start again. ***");
}

const char* resetReason() { return resetreason::now(); }

}  // namespace bp2
