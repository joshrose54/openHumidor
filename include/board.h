#pragma once
// Per-board sizes (spec 5.4): how much the SD logging keeps in RAM while the card
// is out. Rev N (ESP32-WROOM-32, no PSRAM): sized to keep free heap comfortable.
// The next board (ESP32-S3 with PSRAM) can raise these.
//
// Lines are stored by their real length, not in fixed slots: each kind has a line
// count limit AND a byte budget; when either is reached the OLDEST line is dropped
// and counted. The readings buffer is also the normal 16-row write batch.

#include <stddef.h>
#include <stdint.h>

namespace board {

constexpr uint16_t kSdEventsKept   = 100;     // events (spec 5.4: about 100)
constexpr size_t   kSdEventsBytes  = 20000;   // ~200 characters an event

constexpr uint16_t kSdRowsKept     = 60;      // readings rows: an hour at the steady 60 s rate
constexpr size_t   kSdRowsBytes    = 28000;   // test-build rows are up to ~460 characters (cabinet ~260)

constexpr uint16_t kSdHourlyKept   = 24;      // pending hourly rows: a day
constexpr size_t   kSdHourlyBytes  = 9000;    // ~360 characters a row

// WiFi (spec 5.4, per board). Rev N: on only for the NTP sync, then off (its current
// bursts reset the SD card and took most of the heap; B4 SD investigation). The next
// board: always connected, for the dashboard and OTA.
constexpr bool     kWifiOnlyForNtp = true;

}  // namespace board
