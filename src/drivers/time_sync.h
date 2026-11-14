#pragma once
// WiFi and NTP in the background (spec 5.2, 5.4). Nothing here waits:
//   - WiFi: always connected, and it reconnects if it drops (5.4). The NTP window
//     (lib/core/ntp_window.h: at boot and every 24 h, closed once it has synced or
//     after 2 minutes) starts and stops SNTP while the radio stays on.
//     Control never depends on it.
//   - SNTP (pool.ntp.org, time.google.com) runs in the ESP32's own background
//     task: it syncs once WiFi is up, then every 24 h, and sets the system clock.
//   - Each sync is reported once through takeSync(); main then checks the DS3231
//     against it (lib/core/clock_state.h).
//
// The WiFi name and password come from include/wifi_time.h. They are never
// printed, logged or copied anywhere else.

#include <stdint.h>
#include <stddef.h>

namespace timesync {

void begin();                          // BOOT ONLY: starts WiFi and SNTP, returns at once
void update(uint32_t nowMs);           // every loop pass: opens and closes the NTP window; watches WiFi
bool wifiUp();
int rssi();                            // the WiFi signal now, dBm (0 while not connected)
bool takeNote(char* out, size_t n);    // the NTP window opened or closed: text for an "ntp" event
bool takeSync();                       // true once after each NTP sync
// -1 once when WiFi drops, 1 once when it is back (with how long it was gone), 0 none.
// Connecting at boot is neither (lib/core/wifi_watch.h).
int takeWifiEvent(uint32_t* outageMs);
uint32_t syncCount();

}  // namespace timesync
