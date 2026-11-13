// =====================================================================
// BP2 test 30 — the RTC cell (Rev Q), using the firmware's own I2C and WiFi/NTP drivers.
//
// Every 10 s it prints the DS3231's time, its oscillator-stopped flag (OSF) and EOSC, and - once
// NTP has synced over WiFi - how far the DS3231 is from NTP's time. 'sync' sets the DS3231 from
// NTP and clears OSF. Every build clears EOSC at boot if it finds it set (3A.11); this sketch
// just reports it. Switches nothing.
// =====================================================================
#include <Arduino.h>
#include <sys/time.h>
#include <time.h>
#include "bp2/common/bp2_ui.h"
#include "wineador/decode.h"
#include "drivers/i2c_bus.h"
#include "drivers/time_sync.h"

namespace {

const char* const kBanner[] = {
  "Day 1: when 'NTP synced' shows, type  sync  (the DS3231 set from NTP, OSF cleared).",
  "Leave the board powered 48 hours (the cell charges from 3V3Q).",
  "After 48 h: meter BT1's + tab to GND (TP3): 2.8-3.2V. RECORD it with the date and 3V3Q (TP8).",
  "  (above 3.2V: 1M across C49, 3A.8 - tell Claude)",
  "Then 12V off overnight. Next morning, 12V on: this sketch prints OSF (must be CLEAR) and,",
  "once NTP syncs, the DS3231's difference from NTP (time kept: within a few seconds).",
  "Switches nothing. WiFi: the home network, for NTP only.",
  nullptr,
};

uint32_t gLastPrintMs = 0, gLastSweepMs = 0;
bool gSynced = false, gFirst = true;

void print(uint32_t now) {
  wineador::DateTime dt;
  const bool ok = i2cbus::rtcOk() && i2cbus::rtcDateTime(dt);
  char t[32] = "--";
  if (ok) snprintf(t, sizeof(t), "%04d-%02d-%02d %02d:%02d:%02d", dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
  Serial.printf("  up %lu s | DS3231 %s UTC%s | OSF %s | EOSC %s | WiFi %s, NTP %s", (unsigned long)(now / 1000), t,
                ok ? "" : " (NOT READ)", !i2cbus::rtcStatusKnown() ? "unknown" : i2cbus::rtcOsf() ? "SET (time was lost)" : "clear",
                (i2cbus::rtcControl() & wineador::kDs3231Eosc) ? "SET" : "clear", timesync::wifiUp() ? "up" : "down",
                gSynced ? "synced" : "waiting");
  if (ok && gSynced) {
    // How far the DS3231 is from NTP, allowing for the time since the RTC was read.
    const int64_t rtc = wineador::toUnixSeconds(dt) + (int64_t)((now - i2cbus::rtcReadAtMs()) / 1000);
    Serial.printf(" | DS3231 minus NTP %+lld s", (long long)(rtc - (int64_t)time(nullptr)));
  }
  Serial.println();
}

}  // namespace

void setup() {
  bp2::begin("BP2 TEST 30: RTC CELL", kBanner);
  setenv("TZ", "UTC0", 1);
  tzset();
  i2cbus::begin(millis());
  timesync::begin();                      // Rev Q: WiFi always on; SNTP started by the NTP window
}

void loop() {
  const uint32_t now = millis();
  i2cbus::update(now);
  timesync::update(now);
  if (i2cbus::idle() && now - gLastSweepMs >= 2000) { gLastSweepMs = now; i2cbus::startSweep(); }
  if (timesync::takeSync()) { gSynced = true; Serial.println("  NTP synced (the system clock now has NTP's time)."); }
  char note[120];
  if (timesync::takeNote(note, sizeof(note))) Serial.printf("  %s\n", note);
  const int wr = i2cbus::takeRtcWriteResult();
  if (wr) Serial.printf("  sync: DS3231 written from NTP%s\n", wr > 0 ? ", OSF cleared - OK" : " - FAILED");
  if ((gFirst && i2cbus::rtcStatusKnown()) || now - gLastPrintMs >= 10000) {
    if (gFirst) Serial.println("  At this boot:");
    gFirst = false;
    gLastPrintMs = now;
    print(now);
  }
  char w[32];
  if (!bp2::readLine(w, sizeof(w))) return;
  if (strcmp(w, "sync") == 0) {
    if (!gSynced) { Serial.println("  Not yet: wait for 'NTP synced'."); return; }
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    i2cbus::requestRtcWrite(wineador::fromUnixSeconds((int64_t)tv.tv_sec), true);   // and clear OSF
    Serial.println("  sync: writing the DS3231 from NTP (the result follows).");
  } else {
    Serial.printf("  '%s' - type  sync  (after 'NTP synced').\n", w);
  }
}
