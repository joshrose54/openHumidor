#pragma once
// TEST BUILDS ONLY — the scenario runner (spec B3, "How the tests are run").
// `run <n>` plays a B3 test's scenario: it sets the demand, scripts readings
// through the cabinet model, watches what is actually live each pass, and
// prints PASS or FAIL itself. It never touches the outputs: everything goes
// through the runtime guards and the interlock sequence, like the real thing
// (no test mode bypasses the guards, 4.4).
//
// Compiled out of the cabinet build entirely (like cabinet_model.h).
//
// Stage 2 scenarios:
//   run 1   guard timing: min-on, min-off, max run — compressor, heater, humidifier
//   run 2   heater guards: cabinet ceiling, chamber ceiling, fan (unplug J2 when told)
//   run 3   interlock: both loads asked for at once; meter J10 pins 1 and 3
//   run 13  relay exercise: three cycles, both loads off, ends on the cooling side
// `stop` ends a scenario early; the demand goes back to none.

#if defined(WINEADOR_TEST_BUILD)

#include "wineador/types.h"
#include "wineador/settings.h"
#include "wineador/controller.h"
#include "wineador/cabinet_model.h"

namespace wineador {

// What the scenario sees each pass, after this pass's outputs have been applied.
struct ScenarioView {
  uint32_t nowMs = 0;
  LoadOutputs live;
  bool fanOk = false;
};

class ScenarioRunner {
 public:
  enum class Result : uint8_t { None, Running, Pass, Fail, Stopped };

  void begin(const Settings& s) { s_ = s; }

  // false = no such scenario (nothing changes).
  bool start(int n, uint32_t nowMs, CabinetModel& model);
  void stop(uint32_t nowMs, CabinetModel& model);

  // Every pass, after the outputs have been applied.
  void update(const ScenarioView& v, Controller& ctl, CabinetModel& model);

  bool running() const { return result_ == Result::Running; }
  int number() const { return n_; }
  Result result() const { return result_; }
  const Demand& demand() const { return demand_; }

  // Test scaffolding for run 2: kick the plenum fan (100% for 1 s) so it
  // restarts after J2 is plugged back in. Stage 3 does this by itself.
  bool takeFanKick() { bool k = fanKick_; fanKick_ = false; return k; }

  // Text to print, oldest first; nullptr when there is none.
  const char* takeLine();

  static bool exists(int n) { return n == 1 || n == 2 || n == 3 || n == 13; }

 private:
  void say(uint32_t nowMs, const char* fmt, ...);
  void finish(uint32_t nowMs, CabinetModel& model, Result r, const char* why);
  void phase(int p, uint32_t nowMs) { phase_ = p; phaseAtMs_ = nowMs; passesInPhase_ = 0; }
  void edges(const ScenarioView& v);

  void run1(const ScenarioView& v, Controller& ctl, CabinetModel& model);
  void run2(const ScenarioView& v, Controller& ctl, CabinetModel& model);
  void run3(const ScenarioView& v, Controller& ctl, CabinetModel& model);
  void run13(const ScenarioView& v, Controller& ctl, CabinetModel& model);

  Settings s_;
  int n_ = 0;
  Result result_ = Result::None;
  Demand demand_;
  uint32_t startMs_ = 0;
  int phase_ = 0;
  uint32_t phaseAtMs_ = 0;
  uint32_t passesInPhase_ = 0;
  bool fanKick_ = false;
  uint32_t lastKickMs_ = 0;
  ModelRates savedRates_;   // run 1 freezes the model; restored at the end

  // Observed edges: when each load (0 cool, 1 heat, 2 humid) last went on / off.
  LoadOutputs prev_;
  bool ranSinceStart_[3] = {};
  uint32_t onAtMs_[3] = {}, offAtMs_[3] = {};
  bool wentOn_[3] = {}, wentOff_[3] = {};   // this pass
  bool relayRose_ = false, relayFell_ = false;
  uint32_t relayRoseAtMs_ = 0, relayFellAtMs_ = 0;

  int item_ = 0;            // which load / which guard / which cycle
  uint32_t deadlineMs_ = 0;

  static constexpr int kLines = 12;
  static constexpr int kLineLen = 240;
  char lines_[kLines][kLineLen] = {};
  int lineHead_ = 0, lineCount_ = 0;
  char taken_[kLineLen] = {};
};

}  // namespace wineador

#endif  // WINEADOR_TEST_BUILD
