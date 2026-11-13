#pragma once
// Rev Q: OTA from the dashboard, the board's half (spec 5.2, 5.5, 3A.11; stage Q5d). The rules are
// lib/core ota_update.h; this gives them ESP-IDF's flash and the board's control loop.
//
//   - the flash (OtaFlash): ESP-IDF's sequential writes (nothing erased at the start; each 4 KB
//     piece erases the one sector it enters), each piece copied in 256 bytes at a time through an
//     internal-RAM buffer here (the piece itself is in PSRAM); the image's own check at the end
//     (ESP-IDF: checksum and SHA-256); then it is set to boot.
//   - the pacing (OtaPacer): one piece per control-loop pass. The loop calls loopPass() every pass;
//     nextPass() waits for the next one (on the long-transfer task), so the hardware watchdog line
//     changes between any two flash steps.
//   - the record (NVS namespace "ota"): what the last update did, read at every boot.
//   - "started properly": Arduino would mark a new image good at startup by itself; this firmware
//     takes that over (verifyRollbackLater) and marks it good only by the 5.2 rule.
// Rev N: nothing (no dashboard).

#include <stdint.h>
#include <stddef.h>
#include "wineador/ota_update.h"

namespace ota {

// ---- the long-transfer task's side
wineador::OtaFlash& flash();
wineador::OtaPacer& pacer();

// ---- the loop's side
void loopPass();                     // every pass: the writer may take its next piece
uint32_t bytesWritten();             // for the loop's own 70 s watch
void cancel();                       // the loop ended the update: the writer stops at its next piece
void clearCancel();

// The boot facts for readOtaBoot: the running image's state, the slot the bootloader rolled back
// from (and its version).
wineador::OtaBootFacts bootFacts(const char* runningVersion);
bool loadRecord(wineador::OtaRecord* r);
bool saveRecord(const wineador::OtaRecord& r);   // a few ms (an NVS write)
bool markGood();                     // esp_ota_mark_app_valid_cancel_rollback
void rollBackNow();                  // esp_ota_mark_app_invalid_rollback_and_reboot: does not return
uint32_t slotBytes();
uint8_t nextSlot();                  // the app slot the next update is written to (0, 1)

}  // namespace ota
