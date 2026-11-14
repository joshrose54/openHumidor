#pragma once
// Rev Q: the web dashboard's server (spec 5.5, 5.2, 3A.11), on ESP-IDF's own esp_http_server.
//
//   - Its own task on core 0 (the control loop runs on core 1 and never waits for it).
//   - At most 2 browser connections: the server is allowed 3 so that it sees a third at all (with
//     2 it would leave a third queued), and the firmware closes any third at once, never queued;
//     an existing connection is never closed to make room (5.5).
//   - Every reply closes its connection; no CORS header is ever sent; writes only as JSON.
//   - The requests themselves are lib/core web_app (the same code as the Mac review build). Long
//     ones (a log download, a firmware upload) run on a second task, so the server's own task stays
//     free for the other connection.
//   - It reads only: the loop's state copy and the mailbox (app/web_link), the graphs and events
//     fed by the SD task (lib/core web_feed), and the card through the SD task (sdlog::readPiece).
//   - mDNS: the board answers as wineador.local.

#include <stdint.h>
#include <stddef.h>

namespace web {

// BOOT ONLY, after WiFi has been started: PSRAM for the graphs and the work areas, the SD task's
// hooks, the server and mDNS. False (and why in problem()) if any part couldn't start; control is
// unaffected.
bool begin();
bool running();
const char* problem();

// The figures for the `web` console command and BP2 test 39: connections, requests, the third
// connections closed, transfers, the tasks' stack margins, internal RAM.
void formatStatus(char* out, size_t n);

}  // namespace web
