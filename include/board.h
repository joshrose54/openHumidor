#pragma once
// Per-board settings, for both boards, picked by the same flag as pins.h.
//   board_revn  Rev N, the bench board (ESP32-WROOM-32, no PSRAM). No longer available, build only.
//   board_revq  Rev Q, the cabinet board (ESP32-S3-WROOM-1-N8R2, 2 MB PSRAM).
// The drivers use it as plain `board::`. lib/core never includes this file.
//
// Outage buffers (spec 5.4): how much the SD logging keeps in RAM while the card is out.
// Lines are stored by their real length, not in fixed slots: each kind has a line count
// limit AND a byte budget; when either is reached the OLDEST line is dropped and counted.
// The readings buffer is also the normal 16-row write batch.

#include <stddef.h>
#include <stdint.h>

// ======================================================================== Rev N
namespace board_revn {

constexpr const char* kName = "Rev N";
// The "# board" line in every log header (spec 5.4).
constexpr const char* kLogBoard = "rev-n (channel n = mux n+2)";

// Sensor channel n (0-5, spec 2.3) is wired to this mux channel. Used only by drivers/i2c_bus.
constexpr uint8_t kSensorMuxChannel[6] = {2, 3, 4, 5, 6, 7};

constexpr uint16_t kSdEventsKept   = 60;      // events. Cut from 100 for heap margin over the 30 KB floor
                                              // (lowest 32,060 bytes with 100, B4 SD proof run 2026-11-11)
constexpr size_t   kSdEventsBytes  = 12000;   // ~200 characters an event

constexpr uint16_t kSdRowsKept     = 30;      // readings rows: 30 min at the steady 60 s rate. Cut from 60 for
                                              // the 30 KB heap floor: with WiFi on (no card writes) the lowest
                                              // free heap was 24,192 bytes (2026-11-11)
constexpr size_t   kSdRowsBytes    = 14000;   // test-build rows are up to ~460 characters (cabinet ~260)

constexpr uint16_t kSdHourlyKept   = 24;      // pending hourly rows: a day
constexpr size_t   kSdHourlyBytes  = 9000;    // ~360 characters a row

// WiFi (spec 5.4, per board). Rev N: on only for the NTP sync, then off (its current
// bursts reset the SD card and took most of the heap; B4 SD investigation).
constexpr bool     kWifiOnlyForNtp = true;
// Rev N: no card writes while the radio is on (spec 5.4). Rows and events wait in the
// outage buffers above and are written once WiFi is off, so WiFi bursts and card
// writes never overlap (heap and SD supply).
constexpr bool     kSdPauseWhileWifi = true;

}  // namespace board_revn

// ======================================================================== Rev Q
namespace board_revq {

constexpr const char* kName = "Rev Q";
constexpr const char* kLogBoard = "rev-q (channel n = mux n)";

// Rev Q wires sensor channel n to mux channel n (spec 3A.7): no mapping.
constexpr uint8_t kSensorMuxChannel[6] = {0, 1, 2, 3, 4, 5};

// PLACEHOLDERS (stage Q1): Rev N's sizes. Spec 3A.11: Rev Q's outage buffers go in PSRAM
// and are larger; their sizes are proposed at stage Q2 with the PSRAM buffers.
constexpr uint16_t kSdEventsKept   = 60;
constexpr size_t   kSdEventsBytes  = 12000;
constexpr uint16_t kSdRowsKept     = 30;
constexpr size_t   kSdRowsBytes    = 14000;
constexpr uint16_t kSdHourlyKept   = 24;
constexpr size_t   kSdHourlyBytes  = 9000;

// Spec 5.4 / 3A.11: WiFi stays connected and reconnects by itself; card writes are not paused for it.
// (The always-connected WiFi itself is stage Q2.)
constexpr bool     kWifiOnlyForNtp = false;
constexpr bool     kSdPauseWhileWifi = false;

}  // namespace board_revq

// ======================================================================== which board
#if defined(WINEADOR_BOARD_REVN) && defined(WINEADOR_BOARD_REVQ)
#error "Both WINEADOR_BOARD_REVN and WINEADOR_BOARD_REVQ are set: pick one board in platformio.ini"
#elif defined(WINEADOR_BOARD_REVQ)
namespace board = board_revq;
#elif defined(WINEADOR_BOARD_REVN)
namespace board = board_revn;
#elif !defined(WINEADOR_BOARD_ANY)
#error "No board set: add -DWINEADOR_BOARD_REVN or -DWINEADOR_BOARD_REVQ to the environment in platformio.ini"
#endif
