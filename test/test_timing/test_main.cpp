// Boot hold (4.4) and the fan kickstart / minimum duty (4.6), against a fake clock.
#include <unity.h>
#include <initializer_list>
#include "wineador/boot_hold.h"
#include "wineador/fan_controller.h"
#include "wineador/settings.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------- boot hold
void test_boot_hold_real_and_bench_lengths() {
  for (Profile p : {Profile::Real, Profile::Bench}) {
    Settings s = settingsFor(p);
    BootHold h;
    h.begin(0, s.safety.bootHoldMs);
    TEST_ASSERT_TRUE(h.active());
    h.update(s.safety.bootHoldMs - 1);
    TEST_ASSERT_TRUE(h.active());
    TEST_ASSERT_EQUAL_UINT32(1, h.remainingMs());
    h.update(s.safety.bootHoldMs);
    TEST_ASSERT_FALSE(h.active());
    TEST_ASSERT_EQUAL_UINT32(0, h.remainingMs());
  }
}

void test_boot_hold_counts_down() {
  BootHold h;
  h.begin(1000, seconds(30));
  h.update(1000 + seconds(12));
  TEST_ASSERT_EQUAL_UINT32(seconds(18), h.remainingMs());
}

void test_boot_hold_never_returns_after_millis_wrap() {
  BootHold h;
  h.begin(0, minutes(5));
  h.update(minutes(6));
  TEST_ASSERT_FALSE(h.active());
  h.update(100);            // millis() wrapped round to a small number again
  TEST_ASSERT_FALSE(h.active());
}

void test_boot_hold_across_wrap() {
  BootHold h;
  h.begin(0xFFFFFF00u, seconds(30));   // boot just before the wrap
  h.update(0x00000100u);              // 512 ms later, after the wrap
  TEST_ASSERT_TRUE(h.active());
  TEST_ASSERT_EQUAL_UINT32(seconds(30) - 512, h.remainingMs());
}

// ---------------------------------------------------------------- fan
void test_fan_kickstart_at_boot() {
  FanController f(20, 1000);
  TEST_ASSERT_TRUE(f.command(20, 0));
  TEST_ASSERT_EQUAL_UINT8(100, f.output(0));
  TEST_ASSERT_EQUAL_UINT8(100, f.output(999));
  TEST_ASSERT_TRUE(f.kicking());
  TEST_ASSERT_EQUAL_UINT8(20, f.output(1000));
  TEST_ASSERT_FALSE(f.kicking());
  TEST_ASSERT_EQUAL_UINT32(1, f.kickstarts());
}

void test_fan_change_while_running_no_kickstart() {
  FanController f(20, 1000);
  f.command(20, 0);
  f.output(0);
  f.output(2000);
  TEST_ASSERT_TRUE(f.command(40, 3000));
  TEST_ASSERT_EQUAL_UINT8(40, f.output(3000));
  TEST_ASSERT_EQUAL_UINT32(1, f.kickstarts());
}

void test_fan_restart_from_zero_kickstarts_again() {
  FanController f(20, 1000);
  f.command(20, 0); f.output(0); f.output(1500);
  f.command(0, 2000);
  TEST_ASSERT_EQUAL_UINT8(0, f.output(2000));
  f.command(30, 5000);
  TEST_ASSERT_EQUAL_UINT8(100, f.output(5000));
  TEST_ASSERT_EQUAL_UINT8(30, f.output(6000));
  TEST_ASSERT_EQUAL_UINT32(2, f.kickstarts());
}

void test_fan_1_to_19_refused() {
  FanController f(20, 1000);
  f.command(40, 0); f.output(0); f.output(1500);
  for (uint8_t d = 1; d <= 19; d++) TEST_ASSERT_FALSE(f.command(d, 2000));
  TEST_ASSERT_FALSE(f.command(101, 2000));
  TEST_ASSERT_EQUAL_UINT8(40, f.output(2000));   // previous duty kept
  TEST_ASSERT_TRUE(f.command(20, 2000));
  TEST_ASSERT_TRUE(f.command(100, 2000));
}

void test_fan_stop_cancels_kickstart() {
  FanController f(20, 1000);
  f.command(20, 0); f.output(0);
  f.command(0, 500);
  TEST_ASSERT_EQUAL_UINT8(0, f.output(500));
  TEST_ASSERT_FALSE(f.kicking());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_boot_hold_real_and_bench_lengths);
  RUN_TEST(test_boot_hold_counts_down);
  RUN_TEST(test_boot_hold_never_returns_after_millis_wrap);
  RUN_TEST(test_boot_hold_across_wrap);
  RUN_TEST(test_fan_kickstart_at_boot);
  RUN_TEST(test_fan_change_while_running_no_kickstart);
  RUN_TEST(test_fan_restart_from_zero_kickstarts_again);
  RUN_TEST(test_fan_1_to_19_refused);
  RUN_TEST(test_fan_stop_cancels_kickstart);
  return UNITY_END();
}
