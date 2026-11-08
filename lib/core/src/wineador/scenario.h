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
// Scenarios:
//   run 1   guard timing: min-on, min-off, max run — compressor, heater, humidifier
//   run 2   heater guards: cabinet ceiling, chamber ceiling, sensors (chamber implausible),
//           fan (unplug J2 when told). The heater shows lit for 5 s before each trip.
//   run 3   interlock: both loads asked for at once; meter J10 pins 1 and 3
//   run 4   sensor failure: unplug sensors when told; thresholds and the recovery hold
//   run 41  test 4c: heat asked for, chamber in; two cabinet sensors out -> heater off at the second
//   run 42  test 4d: cooling asked for; chamber out -> cooling off (meter J10 pin 1 to pin 2)
//   run 5   divergence: one cabinet sensor scripted 5.5 C high for over 10 min, then back
//   run 6   fan failure: unplug J2 when told; stall kickstarts; replug; loads resume
//   run 13  relay exercise: three cycles, both loads off, ends on the cooling side
//   run 15  I2C bus fault: short a sensor's SDA to GND when told; recovery; remove it
// `stop` ends a scenario early; the demand goes back to none.

#if defined(WINEADOR_TEST_BUILD)

#include "wineador/types.h"
#include "wineador/settings.h"
#include "wineador/controller.h"
#include "wineador/cabinet_model.h"
#include "wineador/sensor_health.h"
#include "wineador/fan_monitor.h"

namespace wineador {

// What the scenario sees each pass, after this pass's outputs have been applied.
struct ScenarioView {
  uint32_t nowMs = 0;
  LoadOutputs live;
  bool fanOk = false;
  bool sweepDone = false;                 // a sensor sweep finished this pass (health just updated)
  const SensorHealth* health = nullptr;
  const FanMonitor* fan = nullptr;
  uint32_t busStuck = 0;                  // stuck-bus recoveries since boot
  bool rtcOk = true;
  bool pullupsOn = false;                 // the ESP32's internal I2C pull-ups (must stay off, 5.3)
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

  // Text to print, oldest first; nullptr when there is none.
  const char* takeLine();

  static bool exists(int n) {
    return n == 1 || n == 2 || n == 3 || n == 4 || n == 41 || n == 42 || n == 5 || n == 6 || n == 13 || n == 15;
  }
  static const char* list() { return "run 1, 2, 3, 4, 41 (4c), 42 (4d), 5, 6, 13, 15"; }

 private:
  void say(uint32_t nowMs, const char* fmt, ...);
  void finish(uint32_t nowMs, CabinetModel& model, Result r, const char* why);
  void phase(int p, uint32_t nowMs) { phase_ = p; phaseAtMs_ = nowMs; passesInPhase_ = 0; }
  void edges(const ScenarioView& v);

  void run1(const ScenarioView& v, Controller& ctl, CabinetModel& model);
  void run2(const ScenarioView& v, Controller& ctl, CabinetModel& model);
  void run3(const ScenarioView& v, Controller& ctl, CabinetModel& model);
  void run4(const ScenarioView& v, Controller& ctl, CabinetModel& model);
  void run41(const ScenarioView& v, Controller& ctl, CabinetModel& model);
  void run42(const ScenarioView& v, Controller& ctl, CabinetModel& model);
  void run5(const ScenarioView& v, Controller& ctl, CabinetModel& model);
  void run6(const ScenarioView& v, Controller& ctl, CabinetModel& model);
  void run13(const ScenarioView& v, Controller& ctl, CabinetModel& model);
  void run15(const ScenarioView& v, Controller& ctl, CabinetModel& model);

  Settings s_;
  int n_ = 0;
  Result result_ = Result::None;
  Demand demand_;
  uint32_t startMs_ = 0;
  int phase_ = 0;
  uint32_t phaseAtMs_ = 0;
  uint32_t passesInPhase_ = 0;
  uint32_t stuckAtStart_ = 0;
  uint32_t kicksSeen_ = 0;
  int target_ = -1;          // the sensor a step is about
  ModelRates savedRates_;   // run 1 freezes the model; restored at the end

  // Observed edges: when each load (0 cool, 1 heat, 2 humid) last went on / off.
  LoadOutputs prev_;
  bool ranSinceStart_[3] = {};
  uint32_t onAtMs_[3] = {}, offAtMs_[3] = {};
  bool wentOn_[3] = {}, wentOff_[3] = {};   // this pass
  bool relayRose_ = false, relayFell_ = false;
  uint32_t relayRoseAtMs_ = 0, relayFellAtMs_ = 0;

  int item_ = 0;            // which load / which guard / which cycle
  uint32_t expectMs_ = 0;   // a time a step is waiting on (0 = not yet)
  uint32_t humidMetMs_ = 0, tempMetMs_ = 0;    // run 4: when each 4.3 requirement was met again
  bool humidBack_ = false, heatBack_ = false;  // run 4: resumed after the hold
  uint32_t deadlineMs_ = 0;

  static constexpr int kLines = 12;
  static constexpr int kLineLen = 240;
  char lines_[kLines][kLineLen] = {};
  int lineHead_ = 0, lineCount_ = 0;
  char taken_[kLineLen] = {};
};

}  // namespace wineador

#endif  // WINEADOR_TEST_BUILD
