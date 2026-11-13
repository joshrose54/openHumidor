// Sensor health (4.3), serial check (5.3), calibration (2.3) and the 4.3
// requirement gate with its recovery hold — against scripted sweeps.
#include <unity.h>
#include <math.h>
#include <initializer_list>
#include "wineador/sensor_health.h"
#include "wineador/sensor_table.h"
#include "wineador/settings.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

namespace {

// A rig: six sensors reading 20 C / 62 % corrected unless changed; every serial right.
struct Rig {
  Settings s = settingsFor(Profile::Bench);
  SensorHealth h;
  Readings r;
  uint32_t now = 0;

  explicit Rig(bool bootSerials = true) {
    h.begin(s);
    for (int i = 0; i < kNumSensors; i++) set(i, 20.0f, 62.0f);
    if (bootSerials) for (int i = 0; i < kNumSensors; i++) h.serialResult(i, BusResult::Ok, kSensors[i].serial);
  }
  void set(int i, float t, float rhCorrected) {
    r.s[i].bus = BusResult::Ok;
    r.s[i].tempC = t;
    r.s[i].rhRawPct = rawForCorrectedRh(i, rhCorrected);
  }
  // One sweep, then answer any serial requests correctly (as the driver would next sweep).
  void sweep(uint32_t dtMs = 15000, bool answerSerials = true) {
    now += dtMs;
    h.update(r, now);
    if (answerSerials)
      for (int i = 0; i < kNumSensors; i++)
        if (h.wantsSerial(i)) h.serialResult(i, BusResult::Ok, kSensors[i].serial);
  }
  int events() { int n = 0; SensorHealth::Event e; while (h.takeEvent(e)) n++; return n; }
};

}  // namespace

// ---------------------------------------------------------------- calibration
void test_calibration_per_serial() {
  // Build log: ch0 gain 1.0067 offset -2.556
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0067f * 65.0f - 2.556f, correctedRh(0, 65.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0274f * 65.0f - 3.878f, correctedRh(2, 65.0f));
  TEST_ASSERT_EQUAL_FLOAT(100.0f, correctedRh(0, 105.0f));    // clamped after correction
  TEST_ASSERT_EQUAL_FLOAT(0.0f, correctedRh(0, 1.0f));
  for (int i = 0; i < kNumSensors; i++)
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, 62.0f, correctedRh(i, rawForCorrectedRh(i, 62.0f)));
}

// ---------------------------------------------------------------- boot
void test_boot_healthy_from_first_good_read_with_right_serial() {
  Rig g;
  for (int i = 0; i < kNumSensors; i++) TEST_ASSERT_FALSE(g.h.healthy(i));   // nothing read yet
  g.sweep(1000);
  for (int i = 0; i < kNumSensors; i++) TEST_ASSERT_TRUE(g.h.healthy(i));
  TEST_ASSERT_EQUAL_INT(4, g.h.healthyCabinet());
  TEST_ASSERT_EQUAL_INT(0, g.events());
}

void test_boot_wrong_serial_excluded_and_logged() {
  Rig g(false);
  for (int i = 0; i < kNumSensors; i++) g.h.serialResult(i, BusResult::Ok, kSensors[i].serial);
  g.h.serialResult(1, BusResult::Ok, kSensors[2].serial);   // ch1 and ch2 plugs swapped
  g.h.serialResult(2, BusResult::Ok, kSensors[1].serial);
  g.sweep(1000, false);
  TEST_ASSERT_TRUE(g.h.reason(1) == Exclusion::WrongSerial);
  TEST_ASSERT_TRUE(g.h.reason(2) == Exclusion::WrongSerial);
  TEST_ASSERT_TRUE(g.h.healthy(0) && g.h.healthy(3));
  TEST_ASSERT_EQUAL_INT(2, g.events());
}

void test_boot_serial_unread_is_retried_before_healthy() {
  Rig g(false);
  for (int i = 0; i < kNumSensors; i++) g.h.serialResult(i, BusResult::Ok, kSensors[i].serial);
  g.h.serialResult(4, BusResult::CrcFail, 0);
  g.sweep(1000, false);
  TEST_ASSERT_FALSE(g.h.healthy(4));
  TEST_ASSERT_TRUE(g.h.wantsSerial(4));
  g.h.serialResult(4, BusResult::Ok, kSensors[4].serial);
  g.sweep();
  TEST_ASSERT_TRUE(g.h.healthy(4));
}

void test_sensor_absent_at_boot_logged() {
  Rig g;
  g.r.s[4].bus = BusResult::NoAck;
  g.sweep(1000);
  TEST_ASSERT_TRUE(g.h.reason(4) == Exclusion::Absent);
  TEST_ASSERT_EQUAL_INT(1, g.events());
}

// ---------------------------------------------------------------- exclusion
void test_three_crc_failures_in_a_row() {
  Rig g;
  g.sweep(1000);
  g.r.s[0].bus = BusResult::CrcFail;
  g.sweep(); g.sweep();
  TEST_ASSERT_TRUE(g.h.healthy(0));        // two: still healthy, last good value used
  g.sweep();
  TEST_ASSERT_TRUE(g.h.reason(0) == Exclusion::CrcFail);
  // two, a good read, two more: never three in a row
  Rig q;
  q.sweep(1000);
  for (int k = 0; k < 5; k++) {
    q.r.s[0].bus = BusResult::CrcFail; q.sweep(); q.sweep();
    q.r.s[0].bus = BusResult::Ok; q.sweep();
  }
  TEST_ASSERT_TRUE(q.h.healthy(0));
}

void test_three_timeouts_in_a_row() {
  Rig g;
  g.sweep(1000);
  g.r.s[3].bus = BusResult::ShortRead;
  g.sweep(); g.sweep();
  TEST_ASSERT_TRUE(g.h.healthy(3));
  g.sweep();
  TEST_ASSERT_TRUE(g.h.reason(3) == Exclusion::Timeout);
}

void test_absent_excluded_at_once() {
  Rig g;
  g.sweep(1000);
  g.r.s[2].bus = BusResult::NoAck;
  g.sweep();
  TEST_ASSERT_TRUE(g.h.reason(2) == Exclusion::Absent);
}

void test_implausible_on_raw_value() {
  Rig g;
  g.sweep(1000);
  g.set(1, 51.0f, 62.0f);              // over 50 C
  g.sweep();
  TEST_ASSERT_TRUE(g.h.reason(1) == Exclusion::Implausible);
  Rig q;
  q.sweep(1000);
  q.r.s[1].rhRawPct = 100.5f;          // raw over 100, though it would correct to under 100
  q.sweep();
  TEST_ASSERT_TRUE(q.h.reason(1) == Exclusion::Implausible);
  Rig c;
  c.sweep(1000);
  c.set(kChamberIndex, -10.5f, 62.0f); // chamber below -10 C
  c.sweep();
  TEST_ASSERT_TRUE(c.h.reason(kChamberIndex) == Exclusion::Implausible);
}

void test_bus_fault_excluded() {
  Rig g;
  g.sweep(1000);
  g.r.s[5].bus = BusResult::BusStuck;
  g.sweep();
  TEST_ASSERT_TRUE(g.h.reason(5) == Exclusion::BusFault);
}

void test_manual_exclude_and_release() {
  Rig g;
  g.s.user.manualExcludeMask = 1u << 1;
  g.h.begin(g.s);
  for (int i = 0; i < kNumSensors; i++) g.h.serialResult(i, BusResult::Ok, kSensors[i].serial);
  g.sweep(1000);
  TEST_ASSERT_TRUE(g.h.reason(1) == Exclusion::Manual);
  TEST_ASSERT_TRUE(g.h.healthy(0));
}

// ---------------------------------------------------------------- divergence
void test_divergence_after_10_minutes_cabinet_only() {
  Rig g;
  g.sweep(1000);
  g.set(0, 25.5f, 62.0f);              // 5.5 C above the others
  for (uint32_t t = 0; t < minutes(10) - seconds(15); t += seconds(15)) g.sweep();
  TEST_ASSERT_TRUE(g.h.healthy(0));    // not yet 10 min
  g.sweep(); g.sweep();
  TEST_ASSERT_TRUE(g.h.reason(0) == Exclusion::Divergence);

  // The chamber and ambient are never excluded for divergence
  Rig c;
  c.sweep(1000);
  c.set(kChamberIndex, 2.0f, 62.0f);
  c.set(kAmbientIndex, 35.0f, 50.0f);
  for (int k = 0; k < 100; k++) c.sweep();
  TEST_ASSERT_TRUE(c.h.healthy(kChamberIndex));
  TEST_ASSERT_TRUE(c.h.healthy(kAmbientIndex));
}

void test_divergence_only_while_three_healthy() {
  Rig g;
  g.sweep(1000);
  g.r.s[3].bus = BusResult::NoAck;     // only 3 cabinet healthy...
  g.r.s[2].bus = BusResult::NoAck;     // ...now 2
  g.sweep();
  g.set(0, 26.0f, 62.0f);
  for (int k = 0; k < 60; k++) g.sweep();
  TEST_ASSERT_TRUE(g.h.healthy(0));    // under 3 healthy: no divergence check
}

void test_divergence_back_within_5C_resets_timer() {
  Rig g;
  g.sweep(1000);
  g.set(0, 25.5f, 62.0f);
  for (int k = 0; k < 30; k++) g.sweep();       // 7.5 min out
  g.set(0, 24.0f, 62.0f);
  g.sweep();                                    // back within 5 C: timer resets
  g.set(0, 25.5f, 62.0f);
  for (int k = 0; k < 30; k++) g.sweep();       // another 7.5 min
  TEST_ASSERT_TRUE(g.h.healthy(0));
}

// ---------------------------------------------------------------- recovery
void test_recovery_after_10_good_reads_and_serial_reread() {
  Rig g;
  g.sweep(1000);
  g.r.s[2].bus = BusResult::NoAck;
  g.sweep();
  g.events();
  g.set(2, 20.0f, 62.0f);              // plugged back in
  for (int k = 0; k < 9; k++) { g.sweep(); TEST_ASSERT_FALSE(g.h.healthy(2)); }
  g.sweep(15000, false);               // 10th good read: now it asks for the serial
  TEST_ASSERT_FALSE(g.h.healthy(2));
  TEST_ASSERT_TRUE(g.h.wantsSerial(2));
  g.h.serialResult(2, BusResult::Ok, kSensors[2].serial);
  g.sweep();
  TEST_ASSERT_TRUE(g.h.healthy(2));
  SensorHealth::Event e;
  TEST_ASSERT_TRUE(g.h.takeEvent(e));
  TEST_ASSERT_FALSE(e.excluded);
  TEST_ASSERT_EQUAL_INT(2, e.index);
}

void test_recovery_refused_if_a_different_probe_came_back() {
  Rig g;
  g.sweep(1000);
  g.r.s[2].bus = BusResult::NoAck;
  g.sweep();
  g.set(2, 20.0f, 62.0f);
  for (int k = 0; k < 9; k++) g.sweep();
  g.sweep(15000, false);
  TEST_ASSERT_TRUE(g.h.wantsSerial(2));
  g.h.serialResult(2, BusResult::Ok, kSensors[3].serial);   // ch3's probe on ch2
  g.sweep(15000, false);
  TEST_ASSERT_TRUE(g.h.reason(2) == Exclusion::WrongSerial);
}

void test_bad_read_resets_the_good_run() {
  Rig g;
  g.sweep(1000);
  g.r.s[2].bus = BusResult::NoAck;
  g.sweep();
  g.set(2, 20.0f, 62.0f);
  for (int k = 0; k < 8; k++) g.sweep();
  g.r.s[2].bus = BusResult::CrcFail;
  g.sweep();
  g.set(2, 20.0f, 62.0f);
  for (int k = 0; k < 9; k++) { g.sweep(); TEST_ASSERT_FALSE(g.h.healthy(2)); }
}

void test_divergence_recovery_needs_10_within_5C() {
  Rig g;
  g.sweep(1000);
  g.set(0, 25.5f, 62.0f);
  for (int k = 0; k < 45; k++) g.sweep();
  TEST_ASSERT_TRUE(g.h.reason(0) == Exclusion::Divergence);
  for (int k = 0; k < 20; k++) g.sweep();       // good, plausible reads, still diverged
  TEST_ASSERT_FALSE(g.h.healthy(0));
  g.set(0, 21.0f, 62.0f);                       // back within 5 C
  for (int k = 0; k < 11; k++) g.sweep();
  TEST_ASSERT_TRUE(g.h.healthy(0));
}

// ---------------------------------------------------------------- summary
void test_summary_uses_healthy_sensors_and_corrected_rh() {
  Rig g;
  g.set(0, 21.0f, 64.0f);
  g.set(1, 19.0f, 60.0f);
  g.set(2, 20.0f, 62.0f);
  g.set(3, 22.0f, 63.0f);
  g.sweep(1000);
  CabinetSummary c = g.h.summary();
  TEST_ASSERT_EQUAL_INT(4, c.cabinetOk);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.5f, c.meanC);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 62.25f, c.meanRhPct);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 64.0f, c.maxRhPct);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f, c.spreadC);
  TEST_ASSERT_TRUE(c.controlWarmest);                       // spread over 2 C: warmest
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 22.0f, c.controlC);
  g.r.s[3].bus = BusResult::NoAck;                          // excluded: drops out
  g.sweep();
  c = g.h.summary();
  TEST_ASSERT_EQUAL_INT(3, c.cabinetOk);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, c.meanC);
  TEST_ASSERT_FALSE(c.controlWarmest);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, c.controlC);
}

void test_summary_keeps_last_good_value_through_a_failed_read() {
  Rig g;
  g.set(0, 21.0f, 62.0f);
  g.sweep(1000);
  g.r.s[0].bus = BusResult::CrcFail;
  g.sweep();
  CabinetSummary c = g.h.summary();
  TEST_ASSERT_EQUAL_INT(4, c.cabinetOk);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 21.0f, c.maxC);
}

// ---------------------------------------------------------------- requirement gate
void test_gate_no_extra_hold_at_boot() {
  RequirementGate q;
  q.begin(seconds(30));
  q.update(false, false, 0);
  TEST_ASSERT_FALSE(q.tempOk());
  q.update(true, true, 1000);
  TEST_ASSERT_TRUE(q.tempOk());
  TEST_ASSERT_TRUE(q.humidOk());
}

void test_gate_fails_at_once_then_holds_after_recovery() {
  for (Profile p : {Profile::Real, Profile::Bench}) {
    const uint32_t hold = settingsFor(p).safety.sensorRecoveryHoldMs;
    RequirementGate q;
    q.begin(hold);
    q.update(true, true, 0);
    q.update(false, true, 10000);          // chamber lost: temperature loads only
    TEST_ASSERT_FALSE(q.tempOk());
    TEST_ASSERT_TRUE(q.humidOk());
    q.update(true, true, 20000);           // back: the hold starts
    TEST_ASSERT_FALSE(q.tempOk());
    TEST_ASSERT_TRUE(q.tempHolding());
    TEST_ASSERT_EQUAL_UINT32(hold, q.tempHoldLeftMs(20000));
    q.update(true, true, 20000 + hold - 1);
    TEST_ASSERT_FALSE(q.tempOk());
    q.update(true, true, 20000 + hold);
    TEST_ASSERT_TRUE(q.tempOk());
  }
}

void test_gate_failing_again_during_hold_restarts_it() {
  RequirementGate q;
  q.begin(seconds(30));
  q.update(true, true, 0);
  q.update(false, false, 1000);
  q.update(true, true, 2000);
  q.update(false, false, 20000);           // lost again during the hold
  q.update(true, true, 25000);
  q.update(true, true, 50000);             // 25 s since it came back: not yet
  TEST_ASSERT_FALSE(q.humidOk());
  q.update(true, true, 55000);
  TEST_ASSERT_TRUE(q.humidOk());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_calibration_per_serial);
  RUN_TEST(test_boot_healthy_from_first_good_read_with_right_serial);
  RUN_TEST(test_boot_wrong_serial_excluded_and_logged);
  RUN_TEST(test_boot_serial_unread_is_retried_before_healthy);
  RUN_TEST(test_sensor_absent_at_boot_logged);
  RUN_TEST(test_three_crc_failures_in_a_row);
  RUN_TEST(test_three_timeouts_in_a_row);
  RUN_TEST(test_absent_excluded_at_once);
  RUN_TEST(test_implausible_on_raw_value);
  RUN_TEST(test_bus_fault_excluded);
  RUN_TEST(test_manual_exclude_and_release);
  RUN_TEST(test_divergence_after_10_minutes_cabinet_only);
  RUN_TEST(test_divergence_only_while_three_healthy);
  RUN_TEST(test_divergence_back_within_5C_resets_timer);
  RUN_TEST(test_recovery_after_10_good_reads_and_serial_reread);
  RUN_TEST(test_recovery_refused_if_a_different_probe_came_back);
  RUN_TEST(test_bad_read_resets_the_good_run);
  RUN_TEST(test_divergence_recovery_needs_10_within_5C);
  RUN_TEST(test_summary_uses_healthy_sensors_and_corrected_rh);
  RUN_TEST(test_summary_keeps_last_good_value_through_a_failed_read);
  RUN_TEST(test_gate_no_extra_hold_at_boot);
  RUN_TEST(test_gate_fails_at_once_then_holds_after_recovery);
  RUN_TEST(test_gate_failing_again_during_hold_restarts_it);
  return UNITY_END();
}
