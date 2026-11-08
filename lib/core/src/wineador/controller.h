#pragma once
// The minimum job, every pass (spec 5.2): boot hold, relay exercise, runtime
// guards, then the interlock sequence -> the four load outputs to drive.
//
// It depends on nothing that can wait: the readings it is given are whatever
// the last sensor sweep produced (and a flag saying whether they are fresh).
// Turning a load off never waits for a sensor read, a log write or the network.
//
// Plain C++, so the native tests run exactly this code with a fake clock.

#include "wineador/types.h"
#include "wineador/settings.h"
#include "wineador/boot_hold.h"
#include "wineador/guards.h"
#include "wineador/interlock.h"
#include "wineador/relay_exercise.h"

namespace wineador {

struct PassInputs {
  uint32_t nowMs = 0;
  bool settingsOk = true;
  bool fanOk = false;
  bool readingsFresh = false;
  CabinetSummary cab;            // healthy sensors only
  bool sensorHoldTemp = false;   // 4.3 recovery hold (RequirementGate)
  bool sensorHoldHumid = false;
  LoadOutputs live;              // what is being driven right now (last pass's result)
  bool strippingActive = false;  // stage 4
};

class Controller {
 public:
  void begin(const Settings& s, uint32_t nowMs);

  // One pass. Returns the outputs to drive now.
  LoadOutputs pass(const PassInputs& in, const Demand& demand);

  // The 'exercise' command. False if one is already under way.
  bool requestExercise(uint32_t nowMs) { return exercise_.request(nowMs); }

  const BootHold& hold() const { return hold_; }
  RelayExercise& exercise() { return exercise_; }
  const RelayExercise& exercise() const { return exercise_; }
  const GuardDecisions& decisions() const { return decisions_; }
  const Demand& demand() const { return demand_; }
  const InterlockSequencer& interlock() const { return seq_; }

 private:
  BootHold hold_;
  RuntimeGuards guards_;
  InterlockSequencer seq_;
  RelayExercise exercise_;
  GuardDecisions decisions_;
  Demand demand_;
};

// "heat+humid", "none" ...
const char* demandText(char* out, size_t n, const Demand& d);

// The guard part of the status line: what is demanded and held, and why.
// e.g. "demand heat+humid | held: heat boot-hold 12s, humid boot-hold 12s"
int formatGuardText(char* out, size_t n, const Controller& c);

}  // namespace wineador
