#pragma once
// The board's settings: Rev Q, the cabinet board (ESP32-S3-WROOM-1-N8R2, 2 MB PSRAM), as
// board_revq, picked by -DWINEADOR_BOARD_REVQ (platformio.ini) as pins.h is. Rev N, the bench board,
// is retired (git tag rev-n-last). The drivers use it as plain `board::`. lib/core never includes
// this file.
//
// Outage buffers (spec 5.4): how much the SD logging keeps in RAM while the card is out.
// Lines are stored by their real length, not in fixed slots: each kind has a line count
// limit AND a byte budget; when either is reached the OLDEST line is dropped and counted.
// The readings buffer is also the normal 16-row write batch.

#include <stddef.h>
#include <stdint.h>
#include "wineador/lcd_seq.h"   // LcdOrientation (Rev Q's LCD)

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
// boot: an event, and the no-PSRAM fallback below in internal RAM.
constexpr bool     kOutageInPsram  = true;
constexpr uint16_t kSdEventsKept   = 1000;
constexpr size_t   kSdEventsBytes  = 200000;  // ~200 characters an event
constexpr uint16_t kSdRowsKept     = 1440;    // readings rows: 24 h at the steady 60 s rate (~6 h at 15 s)
constexpr size_t   kSdRowsBytes    = 700000;  // test-build rows up to ~490 characters (cabinet ~280)
constexpr uint16_t kSdHourlyKept   = 168;     // a week
constexpr size_t   kSdHourlyBytes  = 64000;   // ~360 characters a row

// The no-PSRAM fallback (spec 5.4): 60 events, 30 readings rows, 24 hourly rows in internal RAM -
// the sizes Rev N ran, proven on it against the 30 KB heap floor (B4 SD proof run, 2026-11-11).
constexpr uint16_t kNoPsramEventsKept = 60;
constexpr size_t   kNoPsramEventsBytes = 12000;   // ~200 characters an event
constexpr uint16_t kNoPsramRowsKept   = 30;      // readings rows: 30 min at the steady 60 s rate
constexpr size_t   kNoPsramRowsBytes  = 14000;   // test-build rows are up to ~460 characters (cabinet ~260)
constexpr uint16_t kNoPsramHourlyKept = 24;      // pending hourly rows: a day
constexpr size_t   kNoPsramHourlyBytes = 9000;   // ~360 characters a row

// 12V sense, GPIO8 (3A.3, 3A.11): volts = pin mV / 1000 x this. The divider's nominal
// (100k + 15k) / 15k until BP2 test 27 measures it (then the build log's value goes here).
constexpr bool     kHasSupplySense = true;
constexpr float    k12vScale       = 115.0f / 15.0f;

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
#if defined(WINEADOR_BOARD_REVQ)
namespace board = board_revq;
#elif !defined(WINEADOR_BOARD_ANY)
#error "No board set: add -DWINEADOR_BOARD_REVQ to the environment in platformio.ini"
#endif
