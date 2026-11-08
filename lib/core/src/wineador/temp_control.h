#pragma once
// Temperature control (spec 4.5, 4.6, 4.7, 5.6) — stage 4. Plain logic.
//
// Each pass it takes the time, the sensor summary (healthy sensors only) and
// what is live, and returns what it ASKS for: cooling, heating, the plenum fan
// duty, whether RH stripping is active and whether the heater is being held off
// as ineffective. The runtime guards (4.4) still decide what actually runs.
//
//   Deadband (4.5)  Control temperature = the cabinet mean, or the warmest
//                   cabinet sensor when the spread is over 2 C (4.2).
//                   Cooling asked for above setpoint + 0.5, until it is back at
//                   setpoint. Heating asked for below setpoint - 1.5, until it
//                   is back at setpoint - 1.0. Full heater duty (the PWM cap is
//                   applied by the driver side). Dosed control is off until B6.
//   Crossover       A temperature load starting when the last one to run was the
//                   other: counted and reported. (The 10-minute delay itself is
//                   a guard, 4.4.)
//   Harvest (4.6)   Starts whenever the compressor or heater stops. Ends when the
//                   chamber sensor is within 1.0 C of the cabinet mean; if that
//                   never comes, at the backstop (30 min; bench 2 min). Also ends
//                   if a temperature load starts again. Chamber unhealthy: runs to
//                   the backstop.
//   Fan (4.6)       Idle 20 %, compressor 40 %, heater and heating harvest 60 %.
//                   Cooling harvest: 100 % falling to 40 % as the chamber-cabinet
//                   gap closes — 40 % at convergence (in 5 % steps, never rising).
//                   If the idle duty is 0 (fan stopped at idle — a B6 option, or
//                   the test-build 'fan 0'), a load that is ASKED for gets its
//                   duty before it starts: otherwise the fan guard (tach zero)
//                   would hold it off for ever. Every start from 0 % kickstarts.
//   Stripping (4.7) Trigger: mean cabinet RH >= RH setpoint + 5 % for the whole
//                   persistence time (30 min; bench 30 s). Release: mean cabinet
//                   vapour pressure <= that of the RH setpoint at the temperature
//                   setpoint. Floor: any healthy cabinet sensor below setpoint
//                   - 2 C stops it. Heating wins: a heating call stops it, and it
//                   never starts while heating is asked for or running. After any
//                   stop the full persistence runs again from zero, counted only
//                   while every cabinet sensor is above the floor.
//   Ineffective     Once per run, from the chamber reading at load start:
//   (5.6)           heater must raise the chamber 0.5 C within 15 min (bench
//                   1 min), else heater off, flagged, held off for 30 min (bench
//                   2 min), then tried again. Cooling must lower it 1.0 C within
//                   15 min, else flagged only — cooling carries on. A run that
//                   ends inside the window gives no result. A flag clears on a
//                   later passing window.
//
// Every change worth logging is queued as an Event; main prints them (and
// stage 6 writes them to the SD card).

#include "wineador/types.h"
#include "wineador/settings.h"
#include "wineador/summary.h"
#include "wineador/guards.h"

namespace wineador {

struct ControlInputs {
  uint32_t nowMs = 0;
  CabinetSummary cab;          // healthy sensors only, corrected RH
  bool readingsFresh = false;  // false: the readings are too old to act on
  LoadOutputs live;            // what is live right now (last pass's result)
  // TEST BUILDS: a scenario is asking for loads directly (stage 2-3 scenarios).
  // Then this module asks for nothing itself, and stripping and the ineffective
  // checks are suspended; harvest, the fan and the crossover count still follow
  // what is live.
  bool scriptedDemand = false;
};

struct ControlOutput {
  Demand demand;                    // cool / heat (humid is stage 5)
  bool strippingActive = false;     // -> the guards: humidifier never during stripping
  bool heaterIneffectiveHold = false;   // -> the guards: heater held off until the retry
  uint8_t fanPct = 0;
};

enum class HarvestKind : uint8_t { None, Cooling, Heating };
enum class HarvestEnd : uint8_t { None, Convergence, Backstop, LoadStarted };
enum class StripStop : uint8_t { None, VapourPressure, Floor, Heating, Sensors, Disabled, Scripted };

const char* harvestEndName(HarvestEnd e);
const char* stripStopName(StripStop s);

struct ControlEvent {
  enum class Type : uint8_t {
    None,
    StripStart,        // a = mean RH %, b = mean vapour pressure kPa
    StripStop,         // reason, a = mean RH %, b = mean vapour pressure kPa
    HarvestStart,      // kind, a = chamber-cabinet gap C (or -1 if unknown)
    HarvestEnd,        // kind, harvestEnd, ms = how long it ran
    Crossover,         // a = 0 cooling->heating, 1 heating->cooling; count = total
    HeaterIneffective, // a = chamber change C over the window
    HeaterEffective,   // a = chamber change C; the flag has cleared
    CoolingIneffective,
    CoolingEffective,
  };
  Type type = Type::None;
  HarvestKind kind = HarvestKind::None;
  HarvestEnd harvestEnd = HarvestEnd::None;
  StripStop strip = StripStop::None;
  float a = 0, b = 0;
  uint32_t ms = 0;
  uint32_t count = 0;
};

// One line for the serial output, e.g.
// "RH STRIP START: mean RH 68.0% >= 67.0% for 30 s; cooling called (vapour pressure 1.62 kPa, release at 1.45)"
int formatControlEvent(char* out, size_t n, const ControlEvent& e, const Settings& s);

class TemperatureControl {
 public:
  void begin(const Settings& s, uint32_t nowMs);

  ControlOutput update(const ControlInputs& in);

  // TEST BUILDS: the 'fan <pct>' command — replaces the IDLE duty only (loads and
  // harvest keep their rules). 1-19 % (or over 100) is refused: returns false.
  // -1 = back to the setting.
  bool setIdleFanOverride(int pct);
  // A user setting changed (setpoints, enables): used from the next update.
  void setUser(const UserSettings& u) { s_.user = u; }
  int idleFanOverride() const { return idleOverride_; }

  // State, for the status line and the scenarios
  bool coolCall() const { return coolCall_; }
  bool heatCall() const { return heatCall_; }
  bool stripping() const { return stripActive_; }
  StripStop lastStripStop() const { return lastStripStop_; }
  uint32_t stripCount() const { return stripCount_; }
  bool stripPersisting() const { return persisting_; }
  uint32_t stripPersistMs(uint32_t nowMs) const { return persisting_ ? elapsedMs(nowMs, persistSinceMs_) : 0; }
  float stripTargetVpKpa() const;
  HarvestKind harvest() const { return harvest_; }
  HarvestEnd lastHarvestEnd() const { return lastHarvestEnd_; }
  uint32_t lastHarvestMs() const { return lastHarvestMs_; }
  uint32_t harvestForMs(uint32_t nowMs) const { return harvest_ != HarvestKind::None ? elapsedMs(nowMs, harvestStartMs_) : 0; }
  uint32_t crossovers() const { return crossovers_; }
  bool heaterIneffective() const { return heaterFlag_; }
  bool heaterHeld() const { return heaterHold_; }
  uint32_t heaterRetryLeftMs(uint32_t nowMs) const;
  bool coolingIneffective() const { return coolingFlag_; }
  uint8_t fanPct() const { return fanPct_; }
  // The summary the last update() acted on (the scenarios check against exactly this).
  const CabinetSummary& lastCab() const { return lastCab_; }

  // Oldest first; false when there are none.
  bool takeEvent(ControlEvent& e);

 private:
  struct Check {          // one ineffective check (5.6)
    bool armed = false;
    uint32_t startMs = 0;
    float startC = 0;
  };
  void push(const ControlEvent& e);
  void deadband(const ControlInputs& in);
  void edges(const ControlInputs& in);
  void harvest(const ControlInputs& in);
  void strip(const ControlInputs& in);
  void checks(const ControlInputs& in);
  uint8_t fan(const ControlInputs& in, const Demand& asked);
  void stopStrip(StripStop why, const CabinetSummary& c);

  Settings s_;
  bool coolCall_ = false, heatCall_ = false;
  LoadOutputs prev_;
  enum class Last : uint8_t { None, Cool, Heat } last_ = Last::None;
  uint32_t crossovers_ = 0;

  HarvestKind harvest_ = HarvestKind::None;
  HarvestEnd lastHarvestEnd_ = HarvestEnd::None;
  uint32_t harvestStartMs_ = 0, lastHarvestMs_ = 0;
  float gap0_ = -1;       // chamber-cabinet gap at harvest start (-1 = unknown yet)
  uint8_t harvestDuty_ = 0;

  bool stripActive_ = false;
  bool persisting_ = false;
  uint32_t persistSinceMs_ = 0;
  uint32_t stripCount_ = 0;
  StripStop lastStripStop_ = StripStop::None;

  Check heatCheck_, coolCheck_;
  bool heaterFlag_ = false, coolingFlag_ = false;
  bool heaterHold_ = false;
  uint32_t heaterHoldSinceMs_ = 0;

  int idleOverride_ = -1;
  uint8_t fanPct_ = 0;
  CabinetSummary lastCab_;

  static constexpr int kEvents = 12;
  ControlEvent events_[kEvents];
  int evHead_ = 0, evCount_ = 0;
};

// The control part of the status line, e.g.
// "ctl 20.12C mean idle | strip off (0) persist 12s | xover 0 | harvest cool 0:35 | INEFFECTIVE heater (retry 95s)"
int formatControlText(char* out, size_t n, const TemperatureControl& tc, const CabinetSummary& c,
                      uint32_t nowMs);

}  // namespace wineador
