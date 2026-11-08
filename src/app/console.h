#pragma once
// Serial text that needs the ESP32: the boot banner, the boot sensor check and help.
// The status line and live block are formatted in lib/core/status.h.

#include <stdint.h>
#include "wineador/settings.h"

namespace console {

struct BootInfo {
  const char* version = "";
  const char* bootReason = "";
  bool watchdogOk = false;
  const char* settingsProblem = nullptr;   // nullptr = settings valid
  bool pullupsOff = false;
  bool sdOk = false;
  uint64_t sdMB = 0;
  uint32_t sdMountMs = 0;   // how long the boot-time mount took
  const char* userNote = "";   // user settings: "saved settings loaded" / "none saved (defaults)"
  const char* nvsSpace = "";   // which NVS section this build keeps them in
};

void printBanner(const wineador::Settings& s, const BootInfo& b);
void printSensorCheck();                      // serials from the boot sweep
void printHelp(bool testBuild);
const char* resetReasonName(int espResetReason);

}  // namespace console
