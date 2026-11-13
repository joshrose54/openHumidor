#pragma once
// The load-switching BP2 sketches (tests 18, 19, 20, 35): each step says which loads are on, and
// this applies it through the firmware's own outputs driver, which keeps the interlock order
// (loads going off first, then K1, then loads going on; spec 3.2). The heater PWM is a state,
// not a delay, and the heater switches itself off after 5 minutes on, whatever is typed.

#include <stdint.h>
#include "bp2_ui.h"

namespace bp2 {

struct LoadStep {
  const char* title;
  const char* check;          // what to meter and record (printed labels and TP numbers)
  bool k1;                    // relay energised = heating side
  bool heat;
  float heaterHz;             // 0 = steady
  bool ssr;
  bool humid;
  bool ion;
  int blowerPct;              // -1 = leave as it is (full speed from boot)
  int fanPct;                 // -1 = leave as it is
};

constexpr uint32_t kHeaterLimitMs = 5UL * 60UL * 1000UL;

void loadsBegin();                        // BOOT: every load off (outputs::begin), blowers full, fan untouched
void loadsApply(const LoadStep& s, uint32_t nowMs);
void loadsAllOff();                       // load signals off, then K1; ion off; blowers full; fan as it was
void loadsUpdate(uint32_t nowMs);         // every pass: heater PWM, the 5 min heater limit
Live loadsLive();                         // what is live now, for printStep()
void loadsRestartHeaterLimit(uint32_t nowMs);   // the 5 min heater limit counts again from now

// A whole step-list sketch: call from setup() and loop(). `onStep` (may be nullptr) is told each
// step as it is applied (-1 = all off), for anything extra the test needs (e.g. WiFi in test 20);
// `extraLive` (may be nullptr) adds one line to NOW LIVE.
struct StepSketch {
  const char* title;
  const char* const* banner;
  const LoadStep* steps;
  int count;
  const char* sendClaude;
  void (*onStep)(int index, uint32_t nowMs);
  const char* (*extraLive)();
  void (*everyPass)(uint32_t nowMs);
  bool (*onLine)(const char* line);   // a typed line the sketch handles itself (true = handled)
};
void stepSketchSetup(const StepSketch& s);
void stepSketchLoop();

}  // namespace bp2
