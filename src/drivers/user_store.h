#pragma once
// User settings saved in NVS (spec 5.5): target temperature, RH setpoint, heater
// and humidifier enable, manual sensor excludes; Rev Q's ion generator enable, runs per day and
// minutes per run (4.6c). Each kind of build keeps its own
// copy, so a value set during bench tests can never reach the cabinet build:
//   cabinet (revq) "usr_cab" · bench "usr_bench" · test builds "usr_test".
// load() is BOOT ONLY; save() only when a 'set' command is typed (an NVS write
// takes a few ms). Anything out of bounds is ignored and the default kept.

#include "wineador/settings.h"

namespace userstore {

// Fills u from NVS (keeps the defaults already in u for anything missing or out
// of bounds). Returns a short note for the boot banner: "saved settings loaded",
// "none saved (defaults)" or "NVS unavailable (defaults)".
const char* load(wineador::UserSettings& u);
bool save(const wineador::UserSettings& u);
const char* nvsNamespace();

}  // namespace userstore
