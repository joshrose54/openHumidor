// Plenum fan stall handling (5.6): kickstart at once, then one per retry
// interval while the tach reads zero; recovered once it reads again.
#include <unity.h>
#include <initializer_list>
#include "wineador/fan_monitor.h"
#include "wineador/settings.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

void test_kick_at_once_then_every_retry_interval() {
  for (Profile p : {Profile::Real, Profile::Bench}) {
    const uint32_t retry = settingsFor(p).safety.fanStallRetryMs;
    FanMonitor m;
    m.begin(retry);
    TEST_ASSERT_FALSE(m.window(5000, true, 420));
    TEST_ASSERT_TRUE(m.window(10000, true, 0));          // stall: kick at once
    TEST_ASSERT_TRUE(m.stalled());
    TEST_ASSERT_EQUAL_UINT32(1, m.kicksThisStall());
    uint32_t t = 10000, kicks = 1;
    for (int w = 0; w < 200; w++) {                      // tach stays at zero, 5 s windows
      t += 5000;
      if (m.window(t, true, 0)) {
        kicks++;
        TEST_ASSERT_EQUAL_UINT32(10000 + (kicks - 1) * retry, t);   // exactly one per interval
      }
    }
    TEST_ASSERT_EQUAL_UINT32(1 + (200 * 5000) / retry, kicks);
  }
}

void test_recovered_when_tach_reads_again() {
  FanMonitor m;
  m.begin(seconds(30));
  m.window(5000, true, 0);
  m.window(10000, true, 0);
  TEST_ASSERT_FALSE(m.window(15000, true, 234));
  TEST_ASSERT_FALSE(m.stalled());
  uint32_t k;
  TEST_ASSERT_TRUE(m.takeEvent(&k) == FanMonitor::Event::Stall);
  TEST_ASSERT_TRUE(m.takeEvent(&k) == FanMonitor::Event::Kick);
  TEST_ASSERT_EQUAL_UINT32(1, k);
  TEST_ASSERT_TRUE(m.takeEvent(&k) == FanMonitor::Event::Recovered);
  TEST_ASSERT_TRUE(m.takeEvent(&k) == FanMonitor::Event::None);
  // a second stall kicks at once again
  TEST_ASSERT_TRUE(m.window(20000, true, 0));
  TEST_ASSERT_EQUAL_UINT32(2, m.stalls());
}

void test_fan_meant_to_be_stopped_is_not_a_stall() {
  FanMonitor m;
  m.begin(seconds(30));
  TEST_ASSERT_FALSE(m.window(5000, false, 0));
  TEST_ASSERT_FALSE(m.stalled());
}

void test_retry_across_millis_wrap() {
  FanMonitor m;
  m.begin(seconds(30));
  TEST_ASSERT_TRUE(m.window(0xFFFFF000u, true, 0));
  TEST_ASSERT_FALSE(m.window(0xFFFFF000u + 29999u, true, 0));
  TEST_ASSERT_TRUE(m.window(0xFFFFF000u + 30000u, true, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_kick_at_once_then_every_retry_interval);
  RUN_TEST(test_recovered_when_tach_reads_again);
  RUN_TEST(test_fan_meant_to_be_stopped_is_not_a_stall);
  RUN_TEST(test_retry_across_millis_wrap);
  return UNITY_END();
}
