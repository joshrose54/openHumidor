// Rev Q's hardware watchdog toggle (spec 3A.5, 3A.11; lib/core/wdi_toggle.h).
#include <unity.h>
#include "wineador/wdi_toggle.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

// An input with both pulls off until the control loop runs: the watchdog is disabled at boot.
void test_input_until_started() {
  WdiToggle t;
  TEST_ASSERT_TRUE(t.pin() == WdiToggle::Pin::InputNoPulls);
  TEST_ASSERT_FALSE(t.running());
  const bool before = t.level();
  for (uint32_t ms = 0; ms < 5000; ms += 4) t.pass(ms);   // passes before start change nothing
  TEST_ASSERT_EQUAL(before, t.level());
  TEST_ASSERT_EQUAL_UINT32(0, t.changes());
}

// From the first loop pass, the level changes on every pass.
void test_changes_every_pass() {
  WdiToggle t;
  t.start(1000);
  TEST_ASSERT_TRUE(t.pin() == WdiToggle::Pin::Output);
  bool last = t.level();
  for (uint32_t i = 1; i <= 100; i++) {
    const bool lv = t.pass(1000 + i * 4);
    TEST_ASSERT_TRUE(lv != last);
    last = lv;
  }
  TEST_ASSERT_EQUAL_UINT32(101, t.changes());
  TEST_ASSERT_EQUAL_UINT32(4, t.longestGapMs());
}

// The longest gap is kept; a gap over 0.9 s is reported once (an event), and 0.9 s itself is not.
void test_long_gap_reported_once() {
  WdiToggle t;
  t.start(0);
  t.pass(900);                                  // exactly the limit: allowed
  uint32_t gap = 0;
  TEST_ASSERT_FALSE(t.takeLongGap(&gap));
  t.pass(900 + 901);                            // over it
  TEST_ASSERT_TRUE(t.takeLongGap(&gap));
  TEST_ASSERT_EQUAL_UINT32(901, gap);
  TEST_ASSERT_FALSE(t.takeLongGap(&gap));       // once
  TEST_ASSERT_EQUAL_UINT32(901, t.longestGapMs());
  TEST_ASSERT_EQUAL_UINT32(900, WdiToggle::kMaxGapMs);
}

// Pause: back to an input, no changes while paused, and the pause is not counted as a gap.
void test_pause_and_resume() {
  WdiToggle t;
  t.start(0);
  t.pass(5);
  t.pause();
  TEST_ASSERT_TRUE(t.pin() == WdiToggle::Pin::InputNoPulls);
  const uint32_t n = t.changes();
  t.pass(3000);                                 // ignored while paused
  TEST_ASSERT_EQUAL_UINT32(n, t.changes());
  t.resume(10000);                              // 10 s paused: not a gap
  TEST_ASSERT_TRUE(t.pin() == WdiToggle::Pin::Output);
  t.pass(10004);
  uint32_t gap;
  TEST_ASSERT_FALSE(t.takeLongGap(&gap));
  TEST_ASSERT_EQUAL_UINT32(5, t.longestGapMs());
}

// start() twice does nothing the second time (it must never restart a paused watchdog by accident).
void test_start_once() {
  WdiToggle t;
  t.start(0);
  t.pause();
  t.start(100);
  TEST_ASSERT_FALSE(t.running());
}

void test_across_millis_wrap() {
  WdiToggle t;
  t.start(0xFFFFFFF0u);
  t.pass(0x00000010u);                          // 32 ms later, across the wrap
  TEST_ASSERT_EQUAL_UINT32(32, t.longestGapMs());
  uint32_t gap;
  TEST_ASSERT_FALSE(t.takeLongGap(&gap));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_input_until_started);
  RUN_TEST(test_changes_every_pass);
  RUN_TEST(test_long_gap_reported_once);
  RUN_TEST(test_pause_and_resume);
  RUN_TEST(test_start_once);
  RUN_TEST(test_across_millis_wrap);
  return UNITY_END();
}
