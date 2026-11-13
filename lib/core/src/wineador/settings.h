#pragma once
// The one settings struct (spec 5.5, B3). The control logic reads values from
// here, never from a literal.
//
// Three tiers, as the spec:
//   user    — you may change these (later, from the dashboard), within bounds
//   tuning  — starting values chosen to be safe, not good; B6 replaces them
//   safety  — fixed in firmware, never exposed
//
// Two profiles: Real (spec 5.5) and Bench (spec B3 table — short timers).
// All times in milliseconds, temperatures in °C, humidity in % RH.

#include "wineador/types.h"

namespace wineador {

constexpr uint32_t seconds(uint32_t s) { return s * 1000UL; }
constexpr uint32_t minutes(uint32_t m) { return m * 60UL * 1000UL; }
constexpr uint32_t hours(uint32_t h)   { return h * 60UL * 60UL * 1000UL; }

// ---------------------------------------------------------------- user
struct UserSettings {
  float targetTempC     = 20.0f;
  float rhSetpointPct   = 62.0f;
  bool  heaterEnable    = true;
  bool  humidifierEnable = true;
  bool  ionEnable       = true;    // Rev Q (5.5, 4.6c "Switched off"): off = no ion runs at all; the run
                                   // settings keep their values for when it is switched back on
  uint8_t manualExcludeMask = 0;   // bit i set = sensor index i force-excluded for maintenance
  uint8_t ionRunsPerDay   = 2;     // Rev Q (5.5, 4.6c): 0-4, evenly spaced in UTC slots from 00:00
  uint8_t ionMinutesPerRun = 2;    // Rev Q (5.5, 4.6c): 0-5; runs x minutes at most 10
};

// The same user settings (every field compared): no change to save or log.
bool sameUserSettings(const UserSettings& a, const UserSettings& b);

// Bounds for the user settings (spec 5.5). Out-of-range input is rejected.
constexpr float kTargetTempMinC = 15.0f;
constexpr float kTargetTempMaxC = 22.0f;
constexpr float kRhSetpointMinPct = 55.0f;
constexpr float kRhSetpointMaxPct = 70.0f;

// The setpoints' steps (spec 5.5), the same everywhere: LCD, dashboard, API, serial.
constexpr float kTargetTempStepC = 0.5f;
constexpr float kRhSetpointStepPct = 1.0f;

bool onStep(float v, float step);                // v is a whole number of steps (within float rounding)
float roundToStep(float v, float step);          // the nearest step (halves away from zero)

// False = rejected (out of bounds, off its step, or not a number); the value is unchanged.
bool setTargetTemp(UserSettings& u, float c);
bool setRhSetpoint(UserSettings& u, float pct);

// For a value read back from NVS: in bounds is enough here; one off its step is then rounded at
// boot by roundSetpointsToSteps (5.5). False = out of bounds or not a number (the value unchanged).
bool loadTargetTemp(UserSettings& u, float c);
bool loadRhSetpoint(UserSettings& u, float pct);

// A stored setpoint off its step (5.5) is rounded to the nearest step at boot. True if either
// changed; `wasTemp` / `wasRh` get the values before.
bool roundSetpointsToSteps(UserSettings& u, float* wasTemp, float* wasRh);

// The ion generator's run settings (5.5, 4.6c): runs per day 0-4, minutes per run 0-5, and
// runs x minutes at most 10 ("at most 10 minutes a day in total"; the 24-hour cap stays as a
// backstop, SafetySettings::ionCapMs).
constexpr uint8_t kIonRunsMax = 4;
constexpr uint8_t kIonMinutesMax = 5;
constexpr uint32_t kIonMinutesPerDayMax = 10;
constexpr const char* kIonTooMuchText = "at most 10 minutes a day in total";
bool ionRunsOk(uint8_t runs, uint8_t mins);   // each in bounds and the product at most 10

// ---------------------------------------------------------------- tuning
struct TuningSettings {
  // Deadband and run ends (4.5)
  float coolingHysteresisC        = 0.5f;
  float heatingDeadbandC          = 1.5f;
  float heatingStopBelowSetpointC = 1.0f;   // heating stops at setpoint - 1.0 until dosed control
  uint32_t crossoverDelayMs       = minutes(10);

  // Plenum fan duties, percent (4.6)
  uint8_t fanIdlePct              = 20;
  uint8_t fanCompressorPct        = 40;
  uint8_t fanCoolHarvestStartPct  = 100;
  uint8_t fanCoolHarvestEndPct    = 40;
  uint8_t fanHeatingPct           = 60;

  // Harvest (4.6)
  float harvestConvergenceC       = 1.0f;   // chamber within this of the cabinet mean
  uint32_t harvestBackstopMs      = minutes(30);

  // Dosed control (4.5b) — off until B6
  bool dosedControl               = false;

  // RH stripping (4.7)
  bool  rhStripEnable             = true;
  float rhStripOffsetPct          = 5.0f;
  uint32_t rhStripPersistMs       = minutes(30);
  float rhStripFloorBelowSetpointC = 2.0f;

  // Humidification (4.7b)
  float humidifyBandPct           = 2.0f;
  uint32_t humidifyPersistMs      = minutes(10);
  bool  humidifyDuringCooling     = false;

  // Heater PWM cap (5.5)
  uint8_t heaterPwmCapPct         = 100;
  uint32_t heaterPwmPeriodMs      = 1000;

  // Ineffective checks (5.6)
  float heaterIneffectiveRiseC    = 0.5f;
  uint32_t heaterIneffectiveWindowMs = minutes(15);
  uint32_t heaterIneffectiveRetryMs  = minutes(30);
  float coolingIneffectiveFallC   = 1.0f;
  uint32_t coolingIneffectiveWindowMs = minutes(15);
  float humidIneffectiveRisePct   = 0.5f;   // RH points at setpoint conditions, checked as vapour pressure (~0.012 kPa)
  uint32_t humidIneffectiveWindowMs = minutes(20);
  uint32_t humidIneffectiveRetryMs  = hours(2);

  // Disturbance detection (4.7a) — off until B6 sets its thresholds
  bool disturbanceDetection       = false;

  // Circulation blowers (4.6b, Rev Q). The idle duty is the ONE value BP2 test 22 sets: it stays
  // FULL SPEED (100) until that test chooses it (build log), confirmed in B6. Its range is test 22's
  // floor to 100%, never 0 (the blowers run 24/7); until test 22 the floor is kBlowerIdleFloorPct.
  uint8_t blowerIdlePct           = 100;
  float blowerBoostAboveC         = 1.0f;   // boost when the cabinet spread is above either
  float blowerBoostAboveRhPct     = 3.0f;
  float blowerIdleBelowC          = 0.5f;   // back to idle once below both...
  float blowerIdleBelowRhPct      = 2.0f;
  uint32_t blowerIdleHoldMs       = minutes(10);   // ...for this long (bench: 1 min)

  // Rates
  uint32_t logSteadyMs            = seconds(60);
  uint32_t logActiveMs            = seconds(15);
  uint32_t sensorReadMs           = seconds(15);
};

// ---------------------------------------------------------------- safety
struct SafetySettings {
  // Runtime guards (4.4)
  uint32_t compressorMinOffMs  = minutes(5);
  uint32_t compressorMinOnMs   = minutes(1);
  uint32_t compressorMaxRunMs  = minutes(30);
  uint32_t heaterMinOffMs      = minutes(2);
  uint32_t heaterMinOnMs       = minutes(1);
  uint32_t heaterMaxRunMs      = minutes(60);
  uint32_t humidMinOffMs       = minutes(5);
  uint32_t humidMaxRunMs       = minutes(30);
  uint32_t bootHoldMs          = minutes(5);    // all three loads, every boot (4.4)

  // Ceilings
  float cabinetCeilingAboveSetpointC = 3.0f;
  float chamberCeilingC        = 30.0f;
  float rhHardCeilingPct       = 70.0f;

  // Sensor health (4.3)
  uint8_t  crcFailLimit        = 3;     // consecutive
  uint8_t  timeoutLimit        = 3;     // consecutive
  float    plausibleMinC       = -10.0f;
  float    plausibleMaxC       = 50.0f;
  float    plausibleMinRhPct   = 0.0f;
  float    plausibleMaxRhPct   = 100.0f;
  float    divergenceC         = 5.0f;
  uint32_t divergenceMs        = minutes(10);
  uint8_t  recoveryGoodReads   = 10;
  uint8_t  minHealthyCabinet   = 3;     // of 4
  uint32_t sensorRecoveryHoldMs = minutes(5);

  // Hardware timing
  uint8_t  fanMinDutyPct       = 20;    // never command 1..19 (4.6)
  uint32_t fanKickstartMs      = 1000;  // 100% for 1 s on every start from 0%
  uint32_t fanTachWindowMs     = 5000;  // RPM measured over 5 s
  uint32_t fanStallRetryMs     = minutes(5);   // tach zero: kickstart at once, then every 5 min (5.6)
  uint32_t relaySettleMs       = 20;    // 3.2

  // Weekly relay exercise (5.2): three full cycles, both loads off, end on the cooling side
  uint32_t relayExercisePeriodMs = hours(24 * 7);
  uint32_t relayExerciseOnMs     = 1000;  // energised (heating side)
  uint32_t relayExerciseOffMs    = 1000;  // released (cooling side)
  uint8_t  relayExerciseCycles   = 3;
  uint32_t watchdogMs          = 5000;  // 5.2

  // Ion generator (4.6c, Rev Q): the hard cap, never exceeded whatever the settings, kept across
  // reboots. Runs are 24/N hours apart for runsPerDay; the bench profile runs every 30 min for
  // 1 min instead (B3 table) - ionBenchPeriodMs / ionBenchRunMs non-zero - with the cap unchanged.
  uint32_t ionCapMs            = minutes(10);
  uint32_t ionCapWindowMs      = hours(24);
  uint32_t ionBenchPeriodMs    = 0;     // 0: the real schedule (24/N hours, the run setting's minutes)
  uint32_t ionBenchRunMs       = 0;
};

// Until BP2 test 22 sets the blowers' real floor (the lowest duty at which all four start and run
// cleanly), the boot check only refuses 0 (4.6b: never 0).
constexpr uint8_t kBlowerIdleFloorPct = 1;

struct Settings {
  Profile profile = Profile::Real;
  UserSettings   user;
  TuningSettings tuning;
  SafetySettings safety;
};

// The settings for a profile. Real = spec 5.5. Bench = spec B3 table.
Settings settingsFor(Profile p);

// Checks every value is inside the spec's rules. Returns nullptr if all good,
// otherwise a short description of the first problem.
const char* validateSettings(const Settings& s);

}  // namespace wineador
