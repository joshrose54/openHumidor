#pragma once
// The Mac review build (stage Q5b): a simulated board. The cabinet model (lib/core
// cabinet_model.h) is run by the REAL control logic — sensor health, the requirement gate, the
// temperature and humidity controls, the guards and the interlock — as the scenario tests do. It
// logs through the firmware's own writers (logbook.h, hourly.h, event_text.h) into the history
// rings, event files and weekly counts, and fills the same StatusSnapshot the board will fill in
// stage Q5c. Made-up data: the room temperature follows a daily swing so the loads cycle.
//
// Review states (the /sim page) put the board into the states worth seeing: some are real (the
// sensors or the fan unplugged, a reboot), some are shown only (a forced load, the card out, the
// clock untrusted, an update running), and the review-only "press Allow update".

#include <stdint.h>
#include <string>
#include <map>
#include <vector>
#include "wineador/settings.h"
#include "wineador/controller.h"
#include "wineador/cabinet_model.h"
#include "wineador/sensor_health.h"
#include "wineador/fan_controller.h"
#include "wineador/fan_monitor.h"
#include "wineador/temp_control.h"
#include "wineador/humid_control.h"
#include "wineador/hourly.h"
#include "wineador/logbook.h"
#include "wineador/lcd_ui.h"
#include "wineador/history.h"
#include "wineador/log_read.h"
#include "wineador/web_status.h"
#include "wineador/web_lists.h"
#include "wineador/web_api.h"
#include "wineador/ota_rules.h"

namespace sim {

enum class State : uint8_t {
  Normal,         // the model and the control logic, nothing forced
  Cooling,        // shown: the compressor running
  HeatHumid,      // shown: heating and humidifying at once
  Drying,         // shown: an RH strip
  FaultSensors,   // real: ch2 and ch3 unplugged (2 of 4 cabinet sensors)
  FanStall,       // real: the plenum fan unplugged
  SdFail,         // shown: the SD card out (events from RAM, logs refused)
  ClockBad,       // shown: the clock not trusted
  JustBooted,     // real: a reboot (boot hold, empty history refilling)
  Updating,       // shown: an OTA update at 40 % and climbing
  Count
};
const char* stateKey(State s);
const char* stateLabel(State s);
bool stateFromKey(const char* k, State* out);

class Board {
 public:
  // Builds a month of history ending at `nowUtcS` (takes a few seconds), then runs live.
  void begin(int64_t nowUtcS);
  // Advances the simulation to this wall-clock time, 1 s at a time.
  void tick(int64_t wallUtcS);
  void setState(State s, int64_t wallUtcS);
  State state() const { return state_; }
  void pressAllowUpdate();

  // What the web server reads.
  void fillStatus(wineador::StatusSnapshot* s) const;
  wineador::History& history() { return hist_; }
  int64_t utcNow() const { return startUtc_ + (int64_t)(ms_ / 1000); }
  const wineador::UserSettings& user() const { return s_.user; }
  const wineador::TuningSettings& tuning() const { return s_.tuning; }
  bool updating() const { return updatePct_ >= 0; }
  bool compressorRunning() const { return live_.cool; }
  bool windowOpen() { return ui_.window().active(nowMs()); }
  uint32_t windowLeftS() { return ui_.window().remainingMs(nowMs()) / 1000; }
  bool sdOk() const { return state_ != State::SdFail; }
  wineador::BuildFacts build() const;

  // Writes (already checked by lib/core): applied as the control loop applies them.
  void applyUser(const wineador::UserSettings& u, const char* from);
  void doorTest(const char* note);
  bool startUpdate(const char* version);   // false if the window had closed
  wineador::UpdateState updateState();

  // The card: files and their contents, events kept in RAM.
  std::vector<wineador::LogFileEntry> files() const;
  const std::string* file(const char* path) const;
  const wineador::RecentEvents& recent() const { return recent_; }

 private:
  uint32_t nowMs() const { return (uint32_t)ms_; }
  void step(uint32_t dtMs, bool live);
  void sweep();
  void logRow();
  void event(const char* type, const char* detail);
  void hourRoll();
  void bootComponents();
  void drainEvents();

  wineador::Settings s_;
  wineador::Controller ctl_;
  wineador::CabinetModel model_;
  wineador::SensorHealth health_;
  wineador::RequirementGate gate_;
  wineador::FanMonitor fanMon_;
  wineador::TemperatureControl tc_;
  wineador::HumidControl hc_;
  wineador::FanController fan_{20, 1000};
  wineador::GuardBlockTracker guardTrack_;
  wineador::HourlyStats hourly_;
  wineador::LcdUi ui_;
  wineador::History hist_;
  wineador::WeekCounts week_;
  wineador::RecentEvents recent_;
  std::vector<wineador::HistPoint> rawStore_, dayStore_, hourlyStore_;
  std::vector<wineador::EventRow> recentStore_;
  std::map<std::string, std::string> files_;   // path -> contents
  wineador::LoadOutputs live_;
  uint8_t fanOut_ = 0;
  bool fanPlugged_ = true, fanSpinning_ = true;
  bool plugged_[wineador::kNumSensors] = {true, true, true, true, true, true};
  uint64_t ms_ = 0;            // since the simulation started
  int64_t startUtc_ = 0;
  uint64_t bootMs_ = 0;        // the last (simulated) boot
  uint64_t lastSweepMs_ = 0, lastModelMs_ = 0, lastWindowMs_ = 0, lastLogMs_ = 0;
  int64_t hourKey_ = -1;
  int64_t compressorOnSinceUtc_ = 0, compressorStoppedUtc_ = 0;
  bool compressorStopped_ = false;
  uint64_t weekReadyAtMs_ = 0;
  State state_ = State::Normal;
  int updatePct_ = -1;
  wineador::UpdateResult lastUpdate_ = wineador::UpdateResult::None;
  std::string lastUpdateDetail_;
  int64_t lastUpdateUtc_ = 0;
};

}  // namespace sim
