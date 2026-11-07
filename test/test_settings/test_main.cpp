// Settings struct and profiles — spec 5.5 (real) and the B3 table (bench).
#include <unity.h>
#include "wineador/settings.h"
#include "wineador/build_info.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

void test_real_profile_matches_spec_5_5() {
  Settings s = settingsFor(Profile::Real);
  TEST_ASSERT_TRUE(s.profile == Profile::Real);
  // Safety values
  TEST_ASSERT_EQUAL_UINT32(minutes(5),  s.safety.compressorMinOffMs);
  TEST_ASSERT_EQUAL_UINT32(minutes(1),  s.safety.compressorMinOnMs);
  TEST_ASSERT_EQUAL_UINT32(minutes(30), s.safety.compressorMaxRunMs);
  TEST_ASSERT_EQUAL_UINT32(minutes(2),  s.safety.heaterMinOffMs);
  TEST_ASSERT_EQUAL_UINT32(minutes(1),  s.safety.heaterMinOnMs);
  TEST_ASSERT_EQUAL_UINT32(minutes(60), s.safety.heaterMaxRunMs);
  TEST_ASSERT_EQUAL_UINT32(minutes(5),  s.safety.humidMinOffMs);
  TEST_ASSERT_EQUAL_UINT32(minutes(30), s.safety.humidMaxRunMs);
  TEST_ASSERT_EQUAL_UINT32(minutes(5),  s.safety.bootHoldMs);
  TEST_ASSERT_EQUAL_UINT32(minutes(5),  s.safety.sensorRecoveryHoldMs);
  TEST_ASSERT_EQUAL_FLOAT(3.0f,  s.safety.cabinetCeilingAboveSetpointC);
  TEST_ASSERT_EQUAL_FLOAT(30.0f, s.safety.chamberCeilingC);
  TEST_ASSERT_EQUAL_FLOAT(70.0f, s.safety.rhHardCeilingPct);
  TEST_ASSERT_EQUAL_UINT8(3, s.safety.minHealthyCabinet);
  TEST_ASSERT_EQUAL_UINT32(5000, s.safety.watchdogMs);
  TEST_ASSERT_EQUAL_UINT32(20, s.safety.relaySettleMs);
  // User defaults
  TEST_ASSERT_EQUAL_FLOAT(20.0f, s.user.targetTempC);
  TEST_ASSERT_EQUAL_FLOAT(62.0f, s.user.rhSetpointPct);
  TEST_ASSERT_TRUE(s.user.heaterEnable);
  TEST_ASSERT_TRUE(s.user.humidifierEnable);
  // Tuning starting values
  TEST_ASSERT_EQUAL_FLOAT(0.5f, s.tuning.coolingHysteresisC);
  TEST_ASSERT_EQUAL_FLOAT(1.5f, s.tuning.heatingDeadbandC);
  TEST_ASSERT_EQUAL_UINT32(minutes(10), s.tuning.crossoverDelayMs);
  TEST_ASSERT_EQUAL_UINT8(20, s.tuning.fanIdlePct);
  TEST_ASSERT_EQUAL_UINT8(40, s.tuning.fanCompressorPct);
  TEST_ASSERT_EQUAL_UINT8(100, s.tuning.fanCoolHarvestStartPct);
  TEST_ASSERT_EQUAL_UINT8(40, s.tuning.fanCoolHarvestEndPct);
  TEST_ASSERT_EQUAL_UINT8(60, s.tuning.fanHeatingPct);
  TEST_ASSERT_EQUAL_UINT32(minutes(30), s.tuning.harvestBackstopMs);
  TEST_ASSERT_FALSE(s.tuning.dosedControl);
  TEST_ASSERT_EQUAL_UINT32(minutes(30), s.tuning.rhStripPersistMs);
  TEST_ASSERT_EQUAL_UINT32(minutes(10), s.tuning.humidifyPersistMs);
  TEST_ASSERT_FALSE(s.tuning.humidifyDuringCooling);
  TEST_ASSERT_FALSE(s.tuning.disturbanceDetection);
  TEST_ASSERT_EQUAL_UINT32(minutes(15), s.tuning.heaterIneffectiveWindowMs);
  TEST_ASSERT_EQUAL_UINT32(minutes(15), s.tuning.coolingIneffectiveWindowMs);
  TEST_ASSERT_EQUAL_UINT32(minutes(20), s.tuning.humidIneffectiveWindowMs);
  TEST_ASSERT_EQUAL_UINT32(minutes(30), s.tuning.heaterIneffectiveRetryMs);
  TEST_ASSERT_EQUAL_UINT32(hours(2),    s.tuning.humidIneffectiveRetryMs);
  TEST_ASSERT_EQUAL_UINT32(seconds(15), s.tuning.sensorReadMs);
}

void test_bench_profile_matches_b3_table() {
  Settings s = settingsFor(Profile::Bench);
  TEST_ASSERT_TRUE(s.profile == Profile::Bench);
  TEST_ASSERT_EQUAL_UINT32(seconds(30), s.safety.compressorMinOffMs);
  TEST_ASSERT_EQUAL_UINT32(seconds(10), s.safety.compressorMinOnMs);
  TEST_ASSERT_EQUAL_UINT32(minutes(2),  s.safety.compressorMaxRunMs);
  TEST_ASSERT_EQUAL_UINT32(seconds(20), s.safety.heaterMinOffMs);
  TEST_ASSERT_EQUAL_UINT32(seconds(10), s.safety.heaterMinOnMs);
  TEST_ASSERT_EQUAL_UINT32(minutes(2),  s.safety.heaterMaxRunMs);
  TEST_ASSERT_EQUAL_UINT32(seconds(20), s.safety.humidMinOffMs);
  TEST_ASSERT_EQUAL_UINT32(minutes(2),  s.safety.humidMaxRunMs);
  TEST_ASSERT_EQUAL_UINT32(seconds(30), s.tuning.crossoverDelayMs);
  TEST_ASSERT_EQUAL_UINT32(seconds(30), s.safety.bootHoldMs);
  TEST_ASSERT_EQUAL_UINT32(seconds(30), s.safety.sensorRecoveryHoldMs);
  TEST_ASSERT_EQUAL_UINT32(seconds(30), s.tuning.rhStripPersistMs);
  TEST_ASSERT_EQUAL_UINT32(seconds(30), s.tuning.humidifyPersistMs);
  TEST_ASSERT_EQUAL_UINT32(minutes(1),  s.tuning.heaterIneffectiveWindowMs);
  TEST_ASSERT_EQUAL_UINT32(minutes(1),  s.tuning.coolingIneffectiveWindowMs);
  TEST_ASSERT_EQUAL_UINT32(minutes(1),  s.tuning.humidIneffectiveWindowMs);
  TEST_ASSERT_EQUAL_UINT32(minutes(2),  s.tuning.heaterIneffectiveRetryMs);
  TEST_ASSERT_EQUAL_UINT32(minutes(2),  s.tuning.humidIneffectiveRetryMs);
  TEST_ASSERT_EQUAL_UINT32(minutes(2),  s.tuning.harvestBackstopMs);
}

void test_bench_changes_only_timers() {
  // Everything the B3 table does not list must be the same as real.
  Settings r = settingsFor(Profile::Real), b = settingsFor(Profile::Bench);
  TEST_ASSERT_EQUAL_FLOAT(r.safety.chamberCeilingC, b.safety.chamberCeilingC);
  TEST_ASSERT_EQUAL_FLOAT(r.safety.rhHardCeilingPct, b.safety.rhHardCeilingPct);
  TEST_ASSERT_EQUAL_FLOAT(r.safety.cabinetCeilingAboveSetpointC, b.safety.cabinetCeilingAboveSetpointC);
  TEST_ASSERT_EQUAL_UINT32(r.safety.divergenceMs, b.safety.divergenceMs);
  TEST_ASSERT_EQUAL_UINT32(r.safety.watchdogMs, b.safety.watchdogMs);
  TEST_ASSERT_EQUAL_FLOAT(r.tuning.coolingHysteresisC, b.tuning.coolingHysteresisC);
  TEST_ASSERT_EQUAL_FLOAT(r.user.targetTempC, b.user.targetTempC);
  TEST_ASSERT_EQUAL_UINT8(r.tuning.fanIdlePct, b.tuning.fanIdlePct);
}

void test_both_profiles_valid() {
  TEST_ASSERT_NULL(validateSettings(settingsFor(Profile::Real)));
  TEST_ASSERT_NULL(validateSettings(settingsFor(Profile::Bench)));
}

void test_validation_catches_bad_fan_duty() {
  Settings s = settingsFor(Profile::Real);
  s.tuning.fanIdlePct = 10;   // 1-19% is never allowed
  TEST_ASSERT_NOT_NULL(validateSettings(s));
  s.tuning.fanIdlePct = 0;    // stopped is allowed at idle (B6 step 3 may choose it)
  TEST_ASSERT_NULL(validateSettings(s));
  s.tuning.fanCompressorPct = 0;   // but never stopped with the compressor on
  TEST_ASSERT_NOT_NULL(validateSettings(s));
}

void test_user_bounds_rejected_not_clamped() {
  UserSettings u;
  TEST_ASSERT_FALSE(setTargetTemp(u, 14.9f));
  TEST_ASSERT_EQUAL_FLOAT(20.0f, u.targetTempC);
  TEST_ASSERT_FALSE(setTargetTemp(u, 22.1f));
  TEST_ASSERT_TRUE(setTargetTemp(u, 15.0f));
  TEST_ASSERT_TRUE(setTargetTemp(u, 22.0f));
  TEST_ASSERT_EQUAL_FLOAT(22.0f, u.targetTempC);
  TEST_ASSERT_FALSE(setRhSetpoint(u, 54.0f));
  TEST_ASSERT_FALSE(setRhSetpoint(u, 71.0f));
  TEST_ASSERT_TRUE(setRhSetpoint(u, 65.0f));
  TEST_ASSERT_EQUAL_FLOAT(65.0f, u.rhSetpointPct);
}

void test_version_strings() {
  char v[40];
  TEST_ASSERT_EQUAL_STRING("0.1.0-stage1", makeVersionString(v, sizeof(v), 1, Profile::Real, false));
  TEST_ASSERT_EQUAL_STRING("0.1.0-stage1-BENCH", makeVersionString(v, sizeof(v), 1, Profile::Bench, false));
  TEST_ASSERT_EQUAL_STRING("0.1.0-stage1-TEST", makeVersionString(v, sizeof(v), 1, Profile::Real, true));
  TEST_ASSERT_EQUAL_STRING("0.1.0-stage1-BENCH-TEST", makeVersionString(v, sizeof(v), 1, Profile::Bench, true));
  TEST_ASSERT_EQUAL_STRING("REAL", profileName(Profile::Real));
  TEST_ASSERT_EQUAL_STRING("BENCH", profileName(Profile::Bench));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_real_profile_matches_spec_5_5);
  RUN_TEST(test_bench_profile_matches_b3_table);
  RUN_TEST(test_bench_changes_only_timers);
  RUN_TEST(test_both_profiles_valid);
  RUN_TEST(test_validation_catches_bad_fan_duty);
  RUN_TEST(test_user_bounds_rejected_not_clamped);
  RUN_TEST(test_version_strings);
  return UNITY_END();
}
