#pragma once
// The reset record (diagnostic, used by TEST BUILDS only): a short list of the
// last boots, each with its reset reason and what the previous boot was doing
// when it was last seen alive. Built to find out why the ESP32 can hang after a
// supply dip instead of resetting cleanly.
//
// This file is plain data and formatting (tested on the Mac). The ESP32 side —
// keeping the list in flash and the heartbeat in RTC memory — is in
// src/drivers/reset_record.

#include "wineador/types.h"

namespace wineador {

// Loads as a small bit mask, for the heartbeat.
enum : uint8_t { kLoadCool = 1, kLoadHeat = 2, kLoadRelayHeating = 4, kLoadHumid = 8 };
uint8_t loadMask(const LoadOutputs& l);

struct ResetEntry {
  uint32_t bootNo = 0;          // counts up from the last 'resets clear'
  uint8_t  reason = 0;          // esp_reset_reason() code
  uint8_t  prevKnown = 0;       // 1 = the previous boot's heartbeat survived the reset
  uint8_t  prevLoads = 0;       // loadMask() at the previous boot's last heartbeat
  uint8_t  spare = 0;
  uint32_t prevAliveS = 0;      // previous boot's uptime at its last heartbeat, seconds
};

struct ResetLog {
  static constexpr uint32_t kMagic = 0x52534C47;   // "RSLG"
  static constexpr int kMax = 10;
  uint32_t magic = kMagic;
  uint32_t boots = 0;           // total boots recorded since the last clear
  uint8_t  count = 0;           // entries held, up to kMax
  uint8_t  head = 0;            // index of the oldest entry
  uint8_t  spare[2] = {};
  ResetEntry e[kMax];

  bool valid() const { return magic == kMagic && count <= kMax && head < kMax; }
  void clear() { *this = ResetLog{}; }
  // Adds a boot (bootNo is filled in). The oldest is dropped when full.
  void add(ResetEntry x);
  // i = 0 is the oldest held.
  const ResetEntry& at(int i) const { return e[(head + i) % kMax]; }
};

// One line, e.g.
// "boot 7  BROWNOUT    previous boot last alive at 0:02:51, live: heat humid K1-heat"
// "boot 8  POWERON     previous boot: heartbeat lost"
int formatResetEntry(char* out, size_t n, const ResetEntry& x, const char* reasonName);

}  // namespace wineador
