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
// Stage 4 — these drive the CABINET MODEL and let the real temperature control
// decide (no demand is scripted):
//   run 7   deadband: cabinet swept 17 -> 23 -> 17 C
//   run 8   crossover delay: cooling ends, heating asked for at once (and back)
//   run 9   RH stripping: sustained high RH; release follows vapour pressure
//   run 10  RH stripping temperature floor; persistence again once above the floor
//   run 17  load ineffective: heater, then cooling, with the model not responding
//   run 23  plenum fan rules: idle, compressor run + harvest, heater run + harvest, 10 % refused
// Stage 5 — the humidifier, the same way (19 and 21 also ask for it directly where the test says "demand"):
//   run 19  humidifier guards: persistence, max run and rest, release at setpoint (no min on), a quick re-ask
//   run 20  humidifier ceiling and sensors: one sensor at 70 %; cabinet sensors excluded one at a time
//   run 21  humidifier exclusivity: during a strip; during a temperature run with the setting off, then on
//   run 22  humidifier ineffective: vapour pressure held flat; off, flagged, held, retried
// Stage 6:
//   run 24    test 24 (logging): the automatic part — a chain that produces the control event
//             types (loads, interlock, harvest, guard blocks, crossover, strip, humidify,
//             ineffective, relay exercise). The physical ones are added by hand.
// Stage Q6 (Rev Q):
//   run blowers  BP2 test 41: the blower rule (4.6b) on the cabinet model - a spread above 1.0 C
//             (boost at once), then below 0.5 C (idle after the hold), then a forced Fault (the
//             chamber sensor scripted implausible: full speed), and back to idle. No load is asked
//             for. The ion generator's runs go on by their own schedule meanwhile (4.6c).
//   run soak  B4 step 1: 21 h with REAL timers (real_test build). Room value and drift set per
//             phase so every load cycles several times and reaches its real limits; every pass
//             audited (guard_audit.h); PASS with the counts at the end.
// `stop` ends a scenario early; the demand goes back to none.

#if defined(WINEADOR_TEST_BUILD)

#include "wineador/types.h"
#include "wineador/settings.h"
#include "wineador/controller.h"
#include "wineador/cabinet_model.h"
#include "wineador/sensor_health.h"
#include "wineador/fan_monitor.h"
#include "wineador/temp_control.h"
#include "wineador/humid_control.h"
#include "wineador/guard_audit.h"
#include "wineador/commands.h"
#include "wineador/blower_rule.h"

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
  uint8_t fanOut = 0;                     // plenum fan duty driven right now (100 during a kickstart)
  uint8_t fanTarget = 0;                  // the duty asked of the fan
  const HumidControl* hc = nullptr;       // stage 5
  const BlowerRule* blowers = nullptr;    // stage Q6 (nullptr: no blower checks)
};

class ScenarioRunner {
 public:
  enum class Result : uint8_t { None, Running, Pass, Fail, Stopped };

  void begin(const Settings& s) { s_ = s; }

  // false = no such scenario (nothing changes).
  bool start(int n, uint32_t nowMs, CabinetModel& model);
  void stop(uint32_t nowMs, CabinetModel& model);

  // Every pass, after the outputs have been applied.
  void update(const ScenarioView& v, Controller& ctl, CabinetModel& model, TemperatureControl& tc);

  bool running() const { return result_ == Result::Running; }
  int number() const { return n_; }
  Result result() const { return result_; }
  const Demand& demand() const { return demand_; }
  // Stage 4 scenarios: the temperature control asks, not the scenario.
  bool usesControl() const { return running() && usesControl(n_); }
  // (Their demand() is then added on top of the control's: run 21 'demands humidify'.)
  static bool usesControl(int n) {
    return n == 7 || n == 8 || n == 9 || n == 10 || n == 17 || n == 23 || n == 19 || n == 20 || n == 21 || n == 22 ||
           n == 24 || n == kSoakScenario;
  }

  // Text to print, oldest first; nullptr when there is none.
  const char* takeLine();

  static bool exists(int n) {
    return n == 1 || n == 2 || n == 3 || n == 4 || n == 41 || n == 42 || n == 5 || n == 6 || n == 13 || n == 15 ||
           n == kBlowerScenario || usesControl(n);
  }
  static const char* list() { return "run 1, 2, 3, 4, 41 (4c), 42 (4d), 5, 6, 13, 15, 7, 8, 9, 10, 17, 23, 19, 20, 21, 22, 24, soak, blowers (BP2 41)"; }
  // The soak's running audit (for the status of a long run).
  const GuardAudit& audit() const { return audit_; }
  // 21 h: Josh's run is 20:30 to 18:00 (B4 step 1, 2026-11-08); the verdict comes before he stops it.
  static constexpr uint32_t kSoakMs = 21UL * 60UL * 60UL * 1000UL;

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
  void run7(const ScenarioView& v, Controller& ctl, CabinetModel& model, TemperatureControl& tc);
  void run8(const ScenarioView& v, Controller& ctl, CabinetModel& model, TemperatureControl& tc);
  void run9(const ScenarioView& v, Controller& ctl, CabinetModel& model, TemperatureControl& tc);
  void run10(const ScenarioView& v, Controller& ctl, CabinetModel& model, TemperatureControl& tc);
  void run17(const ScenarioView& v, Controller& ctl, CabinetModel& model, TemperatureControl& tc);
  void run23(const ScenarioView& v, Controller& ctl, CabinetModel& model, TemperatureControl& tc);
  void run19(const ScenarioView& v, Controller& ctl, CabinetModel& model, TemperatureControl& tc);
  void run20(const ScenarioView& v, Controller& ctl, CabinetModel& model, TemperatureControl& tc);
  void run21(const ScenarioView& v, Controller& ctl, CabinetModel& model, TemperatureControl& tc);
  void run22(const ScenarioView& v, Controller& ctl, CabinetModel& model, TemperatureControl& tc);
  void run24(const ScenarioView& v, Controller& ctl, CabinetModel& model, TemperatureControl& tc);
  void runSoak(const ScenarioView& v, Controller& ctl, CabinetModel& model, TemperatureControl& tc);
  void runBlowers(const ScenarioView& v, CabinetModel& model);

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

  // Stage 4
  TemperatureControl* tc_ = nullptr;   // so finish() can clear the idle fan override
  Controller* ctl_ = nullptr;          // ...and put 'humidify during cooling runs' back (run 21)
  // Stage 6
  GuardAudit audit_;
  int soakPhase_ = -1;
  uint32_t soakCycle_ = 0;
  uint32_t lastReportMs_ = 0;
  float sweepC_ = 0;                   // run 7: the scripted cabinet temperature
  int dir_ = 1;
  uint32_t lastTickMs_ = 0;
  int heatStarts_ = 0, coolStarts_ = 0, heatEnds_ = 0, coolEnds_ = 0;
  uint32_t count0_ = 0;                // a counter's value at the start of a step
  bool flag_ = false;
  uint32_t markMs_ = 0;                // a time a step measures from
  uint8_t fanPrev_ = 0, fanMin_ = 0, fanMax_ = 0;
  int kicks_ = 0;
  uint32_t kickAtMs_ = 0;
  bool kicking_ = false;

  static constexpr int kLines = 12;
  static constexpr int kLineLen = 240;
  char lines_[kLines][kLineLen] = {};
  int lineHead_ = 0, lineCount_ = 0;
  char taken_[kLineLen] = {};
};

}  // namespace wineador

#endif  // WINEADOR_TEST_BUILD
