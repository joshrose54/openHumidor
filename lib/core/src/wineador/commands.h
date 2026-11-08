#pragma once
// The small serial command reader: "boot", "help", "exercise" (every build),
// and "stall", "run <n>", "stop", "resets [clear]" (test builds only). The full serial interface
// arrives at stage 6.
// Characters are fed one at a time, so reading never waits.

#include "wineador/types.h"

namespace wineador {

enum class Command : uint8_t {
  None,              // nothing complete yet, or an empty line
  Boot,              // reprint the banner and boot reason
  Help,
  Stall,             // TEST BUILDS ONLY: lock the main loop so the watchdog fires (B3 test 11)
  StallNotAllowed,   // "stall" typed in a cabinet build
  Exercise,          // start the relay exercise now (every build; loads held off)
  Run,               // TEST BUILDS ONLY: "run <n>" plays B3 test n's scenario
  Stop,              // TEST BUILDS ONLY: end the scenario early
  Resets,            // TEST BUILDS ONLY: print the reset record
  ResetsClear,       // TEST BUILDS ONLY: "resets clear" empties it
  TestOnly,          // "run" or "stop" typed in a cabinet build
  Unknown,
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

// Trims spaces, ignores case. For Run, *arg gets the test number (-1 if missing
// or not a number).
Command parseCommand(const char* line, bool testBuild, int* arg = nullptr);

}  // namespace wineador
