#pragma once
// Clock trust and NTP correction (spec 5.4, 5.6). Plain logic.
//
//   - At boot the DS3231's oscillator-stop flag (OSF, status register 0x0F bit 7)
//     says whether the clock stopped while unpowered. If it did, timestamps are
//     UNTRUSTED until NTP has corrected the clock.
//   - Each NTP sync (at boot and every 24 h) sets the system clock; the DS3231
//     is written only if it is more than 2 s out, and the correction is logged.
//   - After the write, OSF is cleared.

#include "wineador/types.h"

namespace wineador {

constexpr int64_t kRtcMaxErrorS = 2;              // write the DS3231 if more than this out
constexpr uint32_t kNtpIntervalMs = 24UL * 60UL * 60UL * 1000UL;

struct ClockState {
  bool osfKnown = false;        // the boot read of the status register worked
  bool osfAtBoot = false;       // the DS3231 had stopped (lost time) before this boot
  bool ntpSynced = false;       // NTP has set the system clock at least once this boot
  bool corrected = false;       // the DS3231 was written (or found within 2 s) after NTP
  int64_t lastCorrectionS = 0;  // how far out it was, seconds (DS3231 minus true time)
  bool rtcWritten = false;      // the DS3231 was written (it was more than 2 s out, or OSF was set)
  uint32_t syncs = 0;           // NTP syncs this boot
  uint64_t firstSyncAtMs = 0;   // uptime of the first NTP sync (0 = none yet)
  uint64_t correctedAtMs = 0;   // uptime when the DS3231 was checked or written after NTP

  // Timestamps are trusted unless the clock lost time and hasn't been put right yet.
  bool trusted() const { return !osfAtBoot || corrected; }
};

// One line for the 'boot' command, so a boot that happened with USB unplugged can
// be read afterwards (B3 status line rule), e.g.
// "OSF set at this boot (lost time) | NTP first sync at 0:00:03, 1 sync | DS3231 was
//  -26.7 years out; written at 0:00:04, OSF cleared | timestamps trusted"
int formatClockSummary(char* out, size_t n, const ClockState& c);

// After an NTP sync: does the DS3231 need writing? `errorS` gets DS3231 minus true time.
inline bool rtcNeedsWrite(int64_t rtcUnixS, int64_t trueUnixS, int64_t* errorS) {
  const int64_t e = rtcUnixS - trueUnixS;
  if (errorS) *errorS = e;
  return e > kRtcMaxErrorS || e < -kRtcMaxErrorS;
}

}  // namespace wineador
