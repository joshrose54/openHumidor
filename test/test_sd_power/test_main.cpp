// Rev Q's SD card power switch sequence (spec 3A.10, 3A.11; lib/core/sd_power_seq.h).
#include <unity.h>
#include "wineador/sd_power_seq.h"

using namespace wineador;
using A = SdPowerSeq::Action;

void setUp() {}
void tearDown() {}

// Runs the sequence 1 ms at a time from `t`, recording each action and when it came.
struct Run {
  A act[8];
  uint32_t at[8];
  int n = 0;
  void go(SdPowerSeq& s, uint32_t t, uint32_t untilMs) {
    for (uint32_t ms = t; ms != t + untilMs; ms++) {
      const A a = s.next(ms);
      if (a != A::None && n < 8) { act[n] = a; at[n] = ms; n++; }
    }
  }
};

// A power cycle: release the bus, THEN cut; at least 100 ms off; soft on; 10 ms; remount.
void test_cycle_order_and_timing() {
  SdPowerSeq s;
  s.startCycle(1000);
  Run r;
  r.go(s, 1000, 400);
  TEST_ASSERT_EQUAL_INT(4, r.n);
  TEST_ASSERT_TRUE(r.act[0] == A::ReleaseBus);       // lines released before the power is cut (3A.11)
  TEST_ASSERT_TRUE(r.act[1] == A::CutPower);
  TEST_ASSERT_TRUE(r.act[2] == A::SoftOn);
  TEST_ASSERT_TRUE(r.act[3] == A::Remount);
  TEST_ASSERT_TRUE(r.at[2] - r.at[1] >= SdPowerSeq::kMinOffMs);            // at least 100 ms off
  TEST_ASSERT_EQUAL_UINT32(SdPowerSeq::kCycleOffMs, r.at[2] - r.at[1]);
  TEST_ASSERT_TRUE(r.at[3] - r.at[2] >= SdPowerSeq::kSettleMs);            // card ready before the remount
  TEST_ASSERT_TRUE(s.state() == SdPowerSeq::State::On);
  TEST_ASSERT_FALSE(s.busy());
}

// Off and on separately (BP2 test 26): on is held until 100 ms after the cut, then soft on only.
void test_on_waits_100_ms_after_the_cut() {
  SdPowerSeq s;
  s.requestOff(0);
  TEST_ASSERT_TRUE(s.next(0) == A::ReleaseBus);
  TEST_ASSERT_TRUE(s.next(1) == A::CutPower);
  TEST_ASSERT_FALSE(s.powered());
  s.requestOn();
  TEST_ASSERT_EQUAL_UINT32(51, s.onAllowedInMs(50));
  for (uint32_t ms = 2; ms < 101; ms++) TEST_ASSERT_TRUE(s.next(ms) == A::None);
  TEST_ASSERT_TRUE(s.next(101) == A::SoftOn);        // 100 ms after the cut at 1
  TEST_ASSERT_TRUE(s.next(200) == A::None);          // no remount: the sketch does that itself
  TEST_ASSERT_TRUE(s.powered());
}

// Switched off and never asked back on: it stays off (nothing is done by itself).
void test_stays_off_until_asked() {
  SdPowerSeq s;
  s.requestOff(0);
  s.next(0);
  s.next(0);
  for (uint32_t ms = 1; ms < 5000; ms += 7) TEST_ASSERT_TRUE(s.next(ms) == A::None);
  TEST_ASSERT_FALSE(s.powered());
}

// One at a time: a second cycle asked for during one changes nothing.
void test_one_cycle_at_a_time() {
  SdPowerSeq s;
  s.startCycle(0);
  s.next(0);
  s.startCycle(1);
  s.requestOff(1);
  Run r;
  r.go(s, 1, 400);
  TEST_ASSERT_EQUAL_INT(3, r.n);                     // CutPower, SoftOn, Remount: the first cycle only
  TEST_ASSERT_TRUE(r.act[0] == A::CutPower);
}

// There is no step that drives GPIO39 low: every action in a cycle is one of these.
void test_never_drives_low() {
  SdPowerSeq s;
  s.startCycle(0);
  Run r;
  r.go(s, 0, 400);
  for (int i = 0; i < r.n; i++)
    TEST_ASSERT_TRUE(r.act[i] == A::ReleaseBus || r.act[i] == A::CutPower || r.act[i] == A::SoftOn ||
                     r.act[i] == A::Remount);
}

void test_across_millis_wrap() {
  SdPowerSeq s;
  s.startCycle(0xFFFFFF00u);
  Run r;
  r.go(s, 0xFFFFFF00u, 400);
  TEST_ASSERT_EQUAL_INT(4, r.n);
  TEST_ASSERT_EQUAL_UINT32(SdPowerSeq::kCycleOffMs, r.at[2] - r.at[1]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_cycle_order_and_timing);
  RUN_TEST(test_on_waits_100_ms_after_the_cut);
  RUN_TEST(test_stays_off_until_asked);
  RUN_TEST(test_one_cycle_at_a_time);
  RUN_TEST(test_never_drives_low);
  RUN_TEST(test_across_millis_wrap);
  return UNITY_END();
}
