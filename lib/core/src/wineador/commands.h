#pragma once
// The serial interface (spec B3-B5 "serial only", 5.5): commands typed at the
// serial monitor. Characters are fed one at a time, so reading never waits.
//
// Every build:
//   boot · help · exercise
//   status · sensors · events [n] · log · settings
//   set temp <C> · set rh <%> · set heater on|off · set humid on|off · set defaults
//   exclude chN on|off              (user settings and manual excludes, saved to NVS)
// Test builds only:
//   stall · run <n> (or "run soak") · stop · resets [clear] · fan <pct>|auto ·
//   humidcool on|off · crc chN · timeout chN · sdstress <kHz> retry|noretry <writes> | stop · ntpsync

#include "wineador/types.h"

namespace wineador {

enum class Command : uint8_t {
  None,              // nothing complete yet, or an empty line
  Boot,              // reprint the banner and boot reason
  Help,
  Stall,             // TEST BUILDS ONLY: lock the main loop so the watchdog fires (B3 test 11)
  StallNotAllowed,   // "stall" typed in a cabinet build
  Exercise,          // start the relay exercise now (every build; loads held off)
  Run,               // TEST BUILDS ONLY: "run <n>" plays B3 test n's scenario ("run soak" = kSoakScenario)
  Stop,              // TEST BUILDS ONLY: end the scenario early
  Resets,            // TEST BUILDS ONLY: print the reset record
  ResetsClear,       // TEST BUILDS ONLY: "resets clear" empties it
  Fan,               // TEST BUILDS ONLY: "fan <pct>" sets the IDLE fan duty (0 or 20-100; 1-19 refused),
                     //   "fan auto" back to the setting. arg = pct, -1 = auto, -2 = not a number
  HumidCool,         // TEST BUILDS ONLY: "humidcool on|off" — humidify during cooling runs (B3 test 21).
                     //   arg = 1 on, 0 off, -2 neither
  // ---- stage 6: the serial interface
  Status,            // the dashboard's view (5.5), as text
  Sensors,           // per-channel health and counts
  Events,            // the last n events (arg = n, -1 = default, -2 = not a number)
  Log,               // the SD card and logging state
  Settings,          // all three tiers
  SetTemp,           // "set temp 19.5": f = value (arg -2 = not a number)
  SetRh,             // "set rh 63":     f = value (arg -2 = not a number)
  SetHeater,         // "set heater on|off": arg 1 / 0, -2 neither
  SetHumid,          // "set humid on|off":  arg 1 / 0, -2 neither
  SetDefaults,       // "set defaults": every user setting back to its default, excludes cleared
  Exclude,           // "exclude <channel> on|off": arg = channel 0..5 (-2 = bad), arg2 = 1 on / 0 off.
                     //   <channel> = 0-5, ch0-ch5, a position name or a probe label (channelWord)
  InjectCrc,         // TEST BUILDS ONLY: "crc <channel>" — its next read fails its CRC (B3 test 24)
  InjectTimeout,     // TEST BUILDS ONLY: "timeout <channel>" — its next read times out
  SdStress,          // TEST BUILDS ONLY: "sdstress <kHz> retry|noretry <writes>"; "sdstress stop" (arg -1).
                     //   arg = kHz (400-9999; -2 = bad), arg2 = 1 retry / 0 noretry, arg3 = writes
  NtpSync,           // TEST BUILDS ONLY: "ntpsync" — open the NTP-only radio window now (Rev N)
  TestOnly,          // a test-build command typed in a cabinet build
  Unknown,
};

constexpr int kSoakScenario = 100;   // "run soak" (B4 step 1)

struct CommandArgs {
  int arg = -1;
  int arg2 = -1;
  int arg3 = -1;
  float f = 0;
};

class LineReader {
 public:
  // Feed one character. Returns true when a line has just been completed.
  bool feed(char c);
  const char* line() const { return line_; }

 private:
  static constexpr size_t kMax = 32;
  char buf_[kMax + 1] = {};
  char line_[kMax + 1] = {};
  size_t len_ = 0;
  bool overflow_ = false;
};

// Trims spaces, ignores case.
Command parseCommand(const char* line, bool testBuild, CommandArgs& a);
// The short form (the integer argument only).
Command parseCommand(const char* line, bool testBuild, int* arg = nullptr);

// A sensor channel as typed (spec 5.5, serial console): the number 0-5 ("3" or "ch3"), the
// position name ("mid_rear") or the probe label ("a1c5"), any case. Returns 0-5, or -2.
int channelWord(const char* word);
// What a channel command acted on, as spec 5.5 shows it: "ch3 = mid_rear (probe A1C5): excluded".
int formatChannelEcho(char* out, size_t n, int channel, const char* what);

}  // namespace wineador
