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
  const char* controlText = nullptr;    // temperature control (formatControlText); nullptr = left out
  const char* humidText = nullptr;      // humidifier control (formatHumidText); nullptr = left out
  const char* sdText = nullptr;         // SD logging, e.g. "sd ok 7/16" or "SD FAIL"; nullptr = left out
  uint8_t     fanDutyPct = 0;
  uint32_t    fanRpm     = 0;
  int32_t     fanPwmReadback = -1;      // 0-255 from the PWM hardware; -1 = left out
  uint32_t    fanPulses  = 0;           // last tach window: accepted pulses...
  uint32_t    fanEdges   = 0;           // ...and every edge (chatter included)
  CabinetSummary cab;
  uint32_t    loopMaxUs  = 0;
  uint32_t    heapFree   = 0;
  uint32_t    heapMin    = 0;          // lowest free heap since boot (5.2)
  bool        timeTrusted = true;       // false adds "UNTRUSTED" after the timestamp (5.4)
  bool        rtcLostTime = false;      // true adds "RTC LOST TIME" (OSF, not yet corrected by NTP)
  const char* netText    = nullptr;     // e.g. "wifi up ntp ok"; nullptr = left out
  const char* boardText  = nullptr;     // Rev Q: "12V 12.04 | psram min ... | hwdog max 9ms"; nullptr = left out
  const char* healthText = nullptr;     // excluded sensors and the 4.3 hold; nullptr = left out
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
