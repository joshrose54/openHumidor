#pragma once
// WiFi and NTP in the background (spec 5.2, 5.4). Nothing here waits:
//   - WiFi connects by itself and reconnects if it drops (the radio stays on;
//     the dashboard will need it later). Control never depends on it.
//   - SNTP (pool.ntp.org, time.google.com) runs in the ESP32's own background
//     task: it syncs once WiFi is up, then every 24 h, and sets the system clock.
//   - Each sync is reported once through takeSync(); main then checks the DS3231
//     against it (lib/core/clock_state.h).
//
// The WiFi name and password come from include/wifi_time.h. They are never
// printed, logged or copied anywhere else.

#include <stdint.h>

namespace timesync {

void begin();                          // BOOT ONLY: starts WiFi and SNTP, returns at once
bool wifiUp();
bool takeSync();                       // true once after each NTP sync
uint32_t syncCount();

}  // namespace timesync
