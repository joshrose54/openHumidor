#pragma once
// TEST BUILDS ONLY — the reset record (diagnostic). See lib/core/reset_log.h.
//
//   - The list of boots lives in flash (NVS), so it survives brownouts, resets
//     and power cuts. Written once per boot (and on 'resets clear').
//   - The heartbeat (uptime and loads, once a second) lives in RTC memory that
//     is not cleared by a software, watchdog or brownout reset. Whether it
//     survives an EN reset is not guaranteed: if it doesn't, the record says
//     "heartbeat lost". It never survives a power cut.

#include <stdint.h>
#include "wineador/types.h"

namespace resetrec {

void begin(int espResetReason);   // BOOT ONLY (writes flash). Records this boot
void heartbeat(uint32_t uptimeS, const wineador::LoadOutputs& live);   // every pass; cheap
void print();                     // the list, oldest first
void clear();                     // SLOW (flash write, a few ms): empties the list

}  // namespace resetrec
