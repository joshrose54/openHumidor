#pragma once
// Runtime guards (spec 4.4) — the rules that decide whether each load MAY run.
// Evaluated every pass, before anything else; nothing overrides them, test
// builds included.
//
// The guards take what is being asked for (the demand), the time, the readings,
// the fan and what is live right now, and return for each load: run or not, and
// if not, why. They never call hardware.
//
// Two kinds of guard:
//   - STOP guards turn a running load off on the pass they fail: settings problem,
//     boot hold, relay exercise cycling, sensors, fan, ceilings, max run,
//     stripping, compressor run (humidifier), user disable, heater ineffective.
//   - START guards only stop a load from starting: min-off, crossover, the other
//     temperature load running, relay exercise waiting.
// Min-on keeps a load on after its demand stops — never against a stop guard.
//
// The interlock position (heater only on the heating side, compressor only on
// the cooling side) is enforced one step later, by InterlockSequencer: a load
// signal is never asserted until K1 has settled on that load's side.
//
// Timing is measured from what is actually live (LoadOutputs), not from what was
// asked for. At boot every load counts as having JUST STOPPED (4.4, "Timing
// state at boot"): the boot hold, min off and crossover delay all run from boot.
// Real profile: compressor and heater first allowed 10 min after boot (the
// crossover delay), the humidifier after 5 (boot hold = its min off).

#include "wineador/types.h"
#include "wineador/settings.h"
#include "wineador/summary.h"

namespace wineador {

// What the control logic (or, in a test build, a scenario) asks for.
struct Demand {
  bool cool  = false;
  bool heat  = false;
  bool humid = false;
};

// Why a load is not running. None = nothing stops it.
enum class GuardBlock : uint8_t {
  None,
  SettingsProblem,   // the settings failed validation: every load off
  BootHold,          // startup delay after every boot
  RelayExercise,     // the weekly relay exercise is waiting or cycling
  Disabled,          // heater or humidifier switched off in the user settings
  Sensors,           // not enough healthy sensors, or the readings are stale
  Fan,               // tach zero with PWM asserted (or the fan not running)
  CabinetCeiling,    // heater: a cabinet sensor above setpoint + 3 C
  ChamberCeiling,    // heater: chamber above 30 C
  RhCeiling,         // humidifier: a cabinet sensor at or above 70 %
  Ineffective,       // heater: failed its ineffective check; held off until the retry (5.6)
  HumidIneffective,  // humidifier: failed its ineffective check; held off until the retry (5.6)
  Stripping,         // humidifier: never during RH stripping
  CompressorRun,     // humidifier: compressor running, "humidify during cooling" off
  MaxRun,            // ran its maximum; now rests for its minimum off
  MinOff,            // stopped too recently
  Crossover,         // the other temperature load stopped too recently
  OtherLoad,         // the other temperature load is running (or starting this pass)
};
const char* guardBlockName(GuardBlock b);   // short, e.g. "boot-hold"

struct GuardInputs {
  uint32_t nowMs = 0;
  bool settingsOk = true;
  bool bootHoldActive = true;
  bool exerciseWaiting = false;   // exercise pending: cool and heat may not START
  bool exerciseCycling = false;   // exercise running: cool and heat OFF
  bool fanOk = false;             // fan PWM asserted and tach non-zero
  bool readingsFresh = false;     // false = the sensor readings are too old to trust
  CabinetSummary cab;             // from the HEALTHY sensors only (4.3)
  bool sensorHoldTemp = false;    // 4.3 recovery hold running for compressor + heater
  bool sensorHoldHumid = false;   // ...for the humidifier
  bool strippingActive = false;   // RH stripping running (4.7): humidifier off
  bool heaterIneffectiveHold = false;   // heater failed its check: off until the retry (5.6)
  bool humidIneffectiveHold = false;    // humidifier failed its check: off until the retry (5.6)
};

struct LoadDecision {
  bool run = false;          // the load may be live this pass
  bool demanded = false;
  bool heldOnByMinOn = false;  // running only because its minimum on has not passed
  GuardBlock block = GuardBlock::None;   // the first guard that stops it
  uint32_t waitMs = 0;       // for MinOff / Crossover / BootHold: time left, if known
};

struct GuardDecisions {
  LoadDecision cool, heat, humid;
};

class RuntimeGuards {
 public:
  void begin(const Settings& s, uint32_t nowMs);

  // actual = what is live right now (the previous pass's outputs).
  GuardDecisions evaluate(const GuardInputs& in, const Demand& demand, const LoadOutputs& actual);

  // TEST BUILDS ONLY call this (B3 test 21): "humidify during cooling runs" at run
  // time. The cabinet build only ever uses the settings value.
  void setHumidifyDuringCooling(bool on) { s_.tuning.humidifyDuringCooling = on; }
  // A user setting changed (serial 'set ...', 5.5): the guards use the new values.
  void setUser(const UserSettings& u) { s_.user = u; }
  bool humidifyDuringCooling() const { return s_.tuning.humidifyDuringCooling; }

 private:
  struct Timer {
    bool on = false;
    uint32_t onSinceMs = 0;
    uint32_t offSinceMs = 0;
  };
  static void track(Timer& t, bool liveNow, uint32_t nowMs);

  Settings s_;
  Timer cool_, heat_, humid_;
};

}  // namespace wineador
