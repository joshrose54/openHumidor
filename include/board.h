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
#include "wineador/lcd_seq.h"   // LcdOrientation (Rev Q's LCD)

// ======================================================================== Rev N
namespace board_revn {

constexpr const char* kName = "Rev N";
// The version (spec B3 "Version string"): 1.0.0 once B3 passed on Rev N; -BENCH and -TEST added.
constexpr const char* kFirmwareVersion = "1.0.0";
// The board in the OTA identity block (lib/core image_identity.h, spec 5.5).
constexpr const char* kIdentityBoard = "rev-n";
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

// What Rev N doesn't have: no 12V sense, no PSRAM (outage buffers in internal RAM), no SD
// power switch, no hardware watchdog, no blowers or ion output, no LCD.
constexpr bool     kOutageInPsram  = false;
constexpr bool     kHasSupplySense = false;
constexpr float    k12vScale       = 0.0f;

// No LCD on Rev N (the names exist so shared code builds; nothing uses them).
constexpr wineador::LcdOrientation kLcdOrientation = wineador::LcdOrientation::Portrait;
constexpr uint8_t  kBacklightDimPct = 20;
constexpr size_t   kLcdSendBytes = 0;
constexpr bool     kHasDashboard = false;            // no dashboard on Rev N (spec 5.5)
constexpr uint32_t kTransferMarginBytes = 0;

}  // namespace board_revn

// ======================================================================== Rev Q
namespace board_revq {

constexpr const char* kName = "Rev Q";
// The version (spec B3 "Version string"): Rev Q's cabinet build is 2.0.0; -BENCH and -TEST added.
// The OTA identity block carries the same string.
constexpr const char* kFirmwareVersion = "2.0.0";
constexpr const char* kIdentityBoard = "rev-q";
constexpr const char* kLogBoard = "rev-q (channel n = mux n)";

// Rev Q wires sensor channel n to mux channel n (spec 3A.7): no mapping.
constexpr uint8_t kSensorMuxChannel[6] = {0, 1, 2, 3, 4, 5};

// Outage buffers in PSRAM (spec 5.4): about 0.94 MB of the 2 MB. If PSRAM is not found at
// boot: an event, and Rev N's sizes in internal RAM (board_revn above).
constexpr bool     kOutageInPsram  = true;
constexpr uint16_t kSdEventsKept   = 1000;
constexpr size_t   kSdEventsBytes  = 200000;  // ~200 characters an event
constexpr uint16_t kSdRowsKept     = 1440;    // readings rows: 24 h at the steady 60 s rate (~6 h at 15 s)
constexpr size_t   kSdRowsBytes    = 700000;  // test-build rows up to ~490 characters (cabinet ~280)
constexpr uint16_t kSdHourlyKept   = 168;     // a week
constexpr size_t   kSdHourlyBytes  = 64000;   // ~360 characters a row

// 12V sense, GPIO8 (3A.3, 3A.11): volts = pin mV / 1000 x this. The divider's nominal
// (100k + 15k) / 15k until BP2 test 27 measures it (then the build log's value goes here).
constexpr bool     kHasSupplySense = true;
constexpr float    k12vScale       = 115.0f / 15.0f;

// Spec 5.4 / 3A.11: WiFi stays connected and reconnects by itself; card writes are not paused for it.
// (The always-connected WiFi itself is stage Q2.)
constexpr bool     kWifiOnlyForNtp = false;
constexpr bool     kSdPauseWhileWifi = false;

// The LCD screens (spec 5.5, 3A.11).
// Which landscape way is up in the enclosure: ONE FIXED CONSTANT, chosen at BP2 test 29 and
// recorded in the build log (5.5) - not a setting. Landscape A until test 29 says otherwise.
constexpr wineador::LcdOrientation kLcdOrientation = wineador::LcdOrientation::LandscapeA;
// The backlight when dimmed, 60 s after the last touch (5.5: about 20%; set at test 29).
constexpr uint8_t  kBacklightDimPct = 20;
// Internal RAM for sending the picture to the LCD by DMA: two buffers, this many bytes in all
// (Josh: about 8 KB, so WiFi and the Q5 web server keep their internal RAM; tuned at BP2).
// 4,096 bytes a piece = 4 full-width rows, about 2.5 ms on the wire at 13.3 MHz.
constexpr size_t   kLcdSendBytes = 8192;

// The web dashboard and its JSON API (spec 5.5), Rev Q only.
constexpr bool     kHasDashboard = true;
// A new long transfer (download or upload) is refused while free internal RAM is below the 30 KB
// floor plus this margin (5.5). Unset (0) until BP2 test 39 measures what a transfer takes.
constexpr uint32_t kTransferMarginBytes = 0;

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
