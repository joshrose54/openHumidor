#pragma once
// WiFi and NTP in the background (spec 5.2, 5.4). Nothing here waits:
//   - WiFi, per board (include/board.h). Rev N: the radio is on only for the NTP
//     sync — at boot and every 24 h, off again once it has synced or after 2
//     minutes (lib/core/ntp_window.h). The next board: always connected, and it
//     reconnects if it drops. Control never depends on it.
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
void update(uint32_t nowMs);           // every loop pass: opens and closes the NTP-only radio window
bool wifiUp();
bool radioOn();                        // the radio is on (always, unless WiFi is only for NTP)
bool takeNote(char* out, size_t n);    // a radio window opened or closed: text for an "ntp" event
void syncNow();                        // TEST BUILDS: open a radio window now (Rev N)
bool takeSync();                       // true once after each NTP sync
uint32_t syncCount();

}  // namespace timesync
