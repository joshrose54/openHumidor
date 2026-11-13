#pragma once
// What every BP2 sketch prints and reads (CLAUDE.md, test sketches; the bp12_outputs_test.cpp
// format Josh found clear):
//   - a banner: the test, what must be connected, the power order, the test-point labels;
//   - nothing switches until "g"; each "n" applies the next step; "x" aborts (all off);
//   - after every step, NOW LIVE (what every output is doing right now), then CHECK NOW
//     (what to meter and record, by the board's printed labels and TP numbers, 3A.12).
// The console is UART0 on J23 (115200). Nothing here waits: lines are read as they arrive.

#include <stdint.h>
#include <stddef.h>

namespace bp2 {

// What is live right now. -1 / nullptr = not part of this test (left out of the block).
struct Live {
  bool k1 = false, heat = false, ssr = false, humid = false, ion = false;
  bool showLoads = true;          // the four U9 loads and the ion output
  float heaterHz = 0;             // heater PWM rate while on (0 = steady)
  int blowerPct = -1;
  int fanPct = -1;
  int backlightPct = -1;
  const char* sdPower = nullptr;  // "ON" / "OFF"
  const char* hwdog = nullptr;    // "TOGGLING" / "INPUT (paused)"
  const char* wifi = nullptr;
  const char* extra = nullptr;    // one more line, if any
};

// Serial on J23, then the banner: `title`, then each line of `lines` (nullptr-terminated),
// then the power order and the test-point labels.
void begin(const char* title, const char* const* lines);
// A typed line, trimmed and lower case (true once per line). Never waits.
bool readLine(char* out, size_t n);

void printLive(const Live& l);
// One step's block: "STEP i of n - NOW ACTIVE", the title, NOW LIVE, CHECK NOW, and the next prompt.
void printStep(int i, int n, const char* title, const Live& l, const char* check, const char* nextTitle);
void printFinished(const char* sendClaude);   // everything off; what to send Claude
void printAborted();
void printTestPoints();                       // printed label = TP number, 3A.12

const char* resetReason();                    // this boot's reset reason, as a word

}  // namespace bp2
