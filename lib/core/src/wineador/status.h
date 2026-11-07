#pragma once
// Text for the serial output: the status line and the "live right now" block.
// Plain formatting, so the native tests can check that every status line
// carries the profile, the test-build mark, the boot reason, the uptime and the
// boot-hold state (spec B3, "Status line").

#include "wineador/types.h"
#include "wineador/summary.h"

namespace wineador {

// "1:02:03" — hours:minutes:seconds, hours unbounded.
const char* formatDuration(char* out, size_t n, uint64_t ms);

struct StatusInfo {
  const char* timestamp  = "RTC?";      // UTC from the DS3231, or a failure mark
  uint64_t    uptimeMs   = 0;
  Profile     profile    = Profile::Real;
  bool        testBuild  = false;
  bool        bootDemand = false;       // the bench_test_boot build: adds "BOOT-DEMAND"
  const char* bootReason = "?";
  bool        holdActive = true;
  uint32_t    holdRemainingMs = 0;
  uint32_t    holdLengthMs    = 0;      // the hold ended this long after boot
  LoadOutputs loads;
  const char* guardText = nullptr;      // demand and held loads (formatGuardText); nullptr = left out
  uint8_t     fanDutyPct = 0;
  uint32_t    fanRpm     = 0;
  CabinetSummary cab;
  uint32_t    loopMaxUs  = 0;
  uint32_t    heapFree   = 0;
  bool        rtcOk      = true;        // false adds "RTC FAIL"
  bool        i2cPullupsOn = false;     // true adds "I2C PULLUPS ON" — must never happen (5.3)
  uint32_t    readingsAgeMs = 0;        // time since the last sensor sweep finished
  uint32_t    staleAfterMs  = 30000;    // older than this adds "readings STALE"
};

// One status line, no newline. Returns the length written (truncated to n-1).
int formatStatusLine(char* out, size_t n, const StatusInfo& s);

// The bp12-style block printed at once whenever a load changes: what is live NOW.
int formatLiveBlock(char* out, size_t n, uint64_t uptimeMs, const char* why,
                    const LoadOutputs& loads, uint8_t fanDutyPct);

}  // namespace wineador
