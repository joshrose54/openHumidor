#include "wineador/settings.h"
#include <math.h>

namespace wineador {

bool onStep(float v, float step) {
  const float n = v / step;
  return fabsf(n - roundf(n)) < 1e-3f;
}

float roundToStep(float v, float step) { return roundf(v / step) * step; }

bool setTargetTemp(UserSettings& u, float c) {
  if (!(c >= kTargetTempMinC && c <= kTargetTempMaxC)) return false;   // also rejects NaN
  if (!onStep(c, kTargetTempStepC)) return false;
  u.targetTempC = roundToStep(c, kTargetTempStepC);                    // exactly on the step
  return true;
}

bool setRhSetpoint(UserSettings& u, float pct) {
  if (!(pct >= kRhSetpointMinPct && pct <= kRhSetpointMaxPct)) return false;
  if (!onStep(pct, kRhSetpointStepPct)) return false;
  u.rhSetpointPct = roundToStep(pct, kRhSetpointStepPct);
  return true;
}

bool loadTargetTemp(UserSettings& u, float c) {
  if (!(c >= kTargetTempMinC && c <= kTargetTempMaxC)) return false;
  u.targetTempC = c;
  return true;
}

bool loadRhSetpoint(UserSettings& u, float pct) {
  if (!(pct >= kRhSetpointMinPct && pct <= kRhSetpointMaxPct)) return false;
  u.rhSetpointPct = pct;
  return true;
}

bool roundSetpointsToSteps(UserSettings& u, float* wasTemp, float* wasRh) {
  if (wasTemp) *wasTemp = u.targetTempC;
  if (wasRh) *wasRh = u.rhSetpointPct;
  const float t = roundToStep(u.targetTempC, kTargetTempStepC);
  const float r = roundToStep(u.rhSetpointPct, kRhSetpointStepPct);
  const bool changed = t != u.targetTempC || r != u.rhSetpointPct;
  u.targetTempC = t;
  u.rhSetpointPct = r;
  return changed;
}

bool ionRunsOk(uint8_t runs, uint8_t mins) {
  return runs <= kIonRunsMax && mins <= kIonMinutesMax && (uint32_t)runs * mins <= kIonMinutesPerDayMax;
}

Settings settingsFor(Profile p) {
  Settings s;            // the struct defaults ARE the real profile (spec 5.5)
  s.profile = p;
  if (p == Profile::Bench) {
    // Spec B3 table, "Bench" column. Everything not listed stays as real.
    s.safety.compressorMinOffMs   = seconds(30);
    s.safety.compressorMinOnMs    = seconds(10);
    s.safety.compressorMaxRunMs   = minutes(2);
    s.safety.heaterMinOffMs       = seconds(20);
    s.safety.heaterMinOnMs        = seconds(10);
    s.safety.heaterMaxRunMs       = minutes(2);
    s.safety.humidMinOffMs        = seconds(20);
    s.safety.humidMaxRunMs        = minutes(2);
    s.tuning.crossoverDelayMs     = seconds(30);
    s.safety.bootHoldMs           = seconds(30);
    s.safety.sensorRecoveryHoldMs = seconds(30);
    s.safety.fanStallRetryMs      = seconds(30);
    s.tuning.rhStripPersistMs     = seconds(30);
    s.tuning.humidifyPersistMs    = seconds(30);
    s.tuning.heaterIneffectiveWindowMs  = minutes(1);
    s.tuning.coolingIneffectiveWindowMs = minutes(1);
    s.tuning.humidIneffectiveWindowMs   = minutes(1);
    s.tuning.heaterIneffectiveRetryMs   = minutes(2);
    s.tuning.humidIneffectiveRetryMs    = minutes(2);
    s.tuning.harvestBackstopMs    = minutes(2);
    s.tuning.blowerIdleHoldMs     = minutes(1);    // Rev Q: blower back-to-idle hold
    s.safety.ionBenchPeriodMs     = minutes(30);   // Rev Q: ion runs every 30 min for 1 min;
    s.safety.ionBenchRunMs        = minutes(1);    //   the cap unchanged, so the bench exercises it
  }
  return s;
}

namespace {
// A fan duty is allowed if it is 0 (stopped) or from the minimum to 100.
bool fanDutyOk(uint8_t pct, uint8_t minPct) { return pct == 0 || (pct >= minPct && pct <= 100); }
}

const char* validateSettings(const Settings& s) {
  const UserSettings& u = s.user;
  const TuningSettings& t = s.tuning;
  const SafetySettings& f = s.safety;

  if (u.targetTempC < kTargetTempMinC || u.targetTempC > kTargetTempMaxC) return "target temperature out of bounds";
  if (u.rhSetpointPct < kRhSetpointMinPct || u.rhSetpointPct > kRhSetpointMaxPct) return "RH setpoint out of bounds";
  if (!ionRunsOk(u.ionRunsPerDay, u.ionMinutesPerRun)) return "ion generator runs over 10 minutes a day, or out of bounds";

  const uint8_t m = f.fanMinDutyPct;
  if (m < 20) return "fan minimum duty below 20%";
  if (!fanDutyOk(t.fanIdlePct, m))             return "fan idle duty is 1-19% or over 100%";
  if (!fanDutyOk(t.fanCompressorPct, m))       return "fan compressor duty is 1-19% or over 100%";
  if (!fanDutyOk(t.fanCoolHarvestStartPct, m)) return "fan harvest start duty is 1-19% or over 100%";
  if (!fanDutyOk(t.fanCoolHarvestEndPct, m))   return "fan harvest end duty is 1-19% or over 100%";
  if (!fanDutyOk(t.fanHeatingPct, m))          return "fan heating duty is 1-19% or over 100%";
  if (t.fanCoolHarvestStartPct < t.fanCoolHarvestEndPct) return "fan harvest start duty below its end duty";
  // The fan must run during compressor, heater and harvest (4.6) — never 0 there.
  if (t.fanCompressorPct == 0 || t.fanHeatingPct == 0 ||
      t.fanCoolHarvestStartPct == 0 || t.fanCoolHarvestEndPct == 0) return "fan set to stop while a load runs";

  if (t.heaterPwmCapPct > 100) return "heater PWM cap over 100%";
  if (t.sensorReadMs > t.logActiveMs) return "sensor read rate slower than the active log rate";
  if (t.blowerIdlePct < kBlowerIdleFloorPct || t.blowerIdlePct > 100) return "blower idle duty is 0 or over 100%";
  if (!(t.blowerIdleBelowC < t.blowerBoostAboveC) || !(t.blowerIdleBelowRhPct < t.blowerBoostAboveRhPct))
    return "blower back-to-idle spread not below the boost spread";
  if (t.blowerIdleHoldMs == 0) return "blower back-to-idle hold is 0";

  if (f.compressorMinOnMs > f.compressorMaxRunMs) return "compressor minimum on exceeds maximum run";
  if (f.heaterMinOnMs > f.heaterMaxRunMs)         return "heater minimum on exceeds maximum run";
  if (f.minHealthyCabinet > kCabinetSensors)      return "more healthy cabinet sensors required than exist";
  if (f.relaySettleMs < 20)                       return "relay settle shorter than 20 ms";
  if (f.watchdogMs != 5000)                       return "watchdog is not 5 s";
  if (f.ionCapMs != minutes(10) || f.ionCapWindowMs != hours(24)) return "ion generator cap is not 10 min in 24 hours";
  if ((f.ionBenchPeriodMs == 0) != (f.ionBenchRunMs == 0) || f.ionBenchRunMs > f.ionBenchPeriodMs)
    return "ion generator bench schedule wrong";
  if (f.relayExerciseCycles == 0 || f.relayExerciseOnMs < f.relaySettleMs || f.relayExerciseOffMs < f.relaySettleMs)
    return "relay exercise cycles or times wrong";
  return nullptr;
}

bool sameUserSettings(const UserSettings& a, const UserSettings& b) {
  return a.targetTempC == b.targetTempC && a.rhSetpointPct == b.rhSetpointPct && a.heaterEnable == b.heaterEnable &&
         a.humidifierEnable == b.humidifierEnable && a.ionEnable == b.ionEnable && a.manualExcludeMask == b.manualExcludeMask &&
         a.ionRunsPerDay == b.ionRunsPerDay && a.ionMinutesPerRun == b.ionMinutesPerRun;
}

}  // namespace wineador
