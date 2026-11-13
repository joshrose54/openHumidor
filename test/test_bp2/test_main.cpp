// Stage Q3: the BP2 sketches' go-ahead and steps (lib/core/bp2_steps.h), the sums they print
// (bp2_calc.h), the DS3231 EOSC bit (decode.h), and the new test-build commands.
#include <unity.h>
#include "wineador/bp2_steps.h"
#include "wineador/bp2_calc.h"
#include "wineador/decode.h"
#include "wineador/commands.h"

using namespace wineador;
using E = StepSequencer::Event;

void setUp() {}
void tearDown() {}

// Nothing happens until g; g applies step 1.
void test_nothing_until_g() {
  StepSequencer s;
  s.begin(3);
  TEST_ASSERT_TRUE(s.waiting());
  TEST_ASSERT_TRUE(s.handle("n") == E::Unknown);        // n before g does nothing
  TEST_ASSERT_TRUE(s.handle("start") == E::Unknown);
  TEST_ASSERT_EQUAL_INT(-1, s.step());
  TEST_ASSERT_TRUE(s.handle("g") == E::Start);
  TEST_ASSERT_EQUAL_INT(0, s.step());
  TEST_ASSERT_TRUE(s.running());
}

// Each n the next step; n after the last finishes (everything off); g then starts again.
void test_steps_then_finish() {
  StepSequencer s;
  s.begin(3);
  s.handle("g");
  TEST_ASSERT_TRUE(s.handle("g") == E::Unknown);        // g while running does nothing
  TEST_ASSERT_TRUE(s.handle("n") == E::Step);
  TEST_ASSERT_EQUAL_INT(1, s.step());
  TEST_ASSERT_TRUE(s.handle("n") == E::Step);
  TEST_ASSERT_EQUAL_INT(2, s.step());
  TEST_ASSERT_TRUE(s.handle("n") == E::Finish);
  TEST_ASSERT_TRUE(s.waiting());
  TEST_ASSERT_TRUE(s.handle("g") == E::Start);
  TEST_ASSERT_EQUAL_INT(0, s.step());
}

// x aborts at any time, waiting or running.
void test_x_aborts_any_time() {
  StepSequencer s;
  s.begin(5);
  TEST_ASSERT_TRUE(s.handle("x") == E::Abort);
  s.handle("g");
  s.handle("n");
  TEST_ASSERT_TRUE(s.handle("x") == E::Abort);
  TEST_ASSERT_TRUE(s.waiting());
  TEST_ASSERT_TRUE(s.handle("n") == E::Unknown);        // after an abort: g again first
}

void test_empty_line_and_no_steps() {
  StepSequencer s;
  s.begin(0);
  TEST_ASSERT_TRUE(s.handle("") == E::None);
  TEST_ASSERT_TRUE(s.handle("g") == E::Unknown);        // nothing to run
}

// Test 27: the scale factor from the meter and the pin.
void test_scale_from_meter() {
  TEST_ASSERT_FLOAT_WITHIN(0.0005f, 7.6688f, scaleFromMeter(12.04f, 1570));   // 12.04 / 1.570
  TEST_ASSERT_FLOAT_WITHIN(0.0005f, 7.6000f, scaleFromMeter(11.40f, 1500));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, scaleFromMeter(12.0f, 0));
}

// Test 20: U8's input current from the R21/R22 drop (3.4 ohm).
void test_u8_input_current() {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.147f, u8InputAmps(0.5f));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.588f, u8InputAmps(2.0f));   // the pass limit, 2.0V
}

// Test 22: the blower PWM value: 9 bits, 100% = 512 (always high), 50% = 256, 0 = off.
void test_blower_duty() {
  TEST_ASSERT_EQUAL_UINT32(512, blowerDuty(100, 9));
  TEST_ASSERT_EQUAL_UINT32(256, blowerDuty(50, 9));
  TEST_ASSERT_EQUAL_UINT32(51, blowerDuty(10, 9));
  TEST_ASSERT_EQUAL_UINT32(0, blowerDuty(0, 9));
  TEST_ASSERT_EQUAL_UINT32(512, blowerDuty(150, 9));             // over 100: full
}

// EOSC is bit 7 of the control register; setting or clearing it leaves the other bits alone.
void test_eosc_bit_only() {
  TEST_ASSERT_EQUAL_HEX8(0x0E, kDs3231ControlReg);
  TEST_ASSERT_EQUAL_HEX8(0x9C, ds3231WithEosc(0x1C, true));     // 0x1C: the DS3231's power-on value
  TEST_ASSERT_EQUAL_HEX8(0x1C, ds3231WithEosc(0x9C, false));
  TEST_ASSERT_EQUAL_HEX8(0x1C, ds3231WithEosc(0x1C, false));     // already clear: unchanged
  TEST_ASSERT_EQUAL_HEX8(0xFF, ds3231WithEosc(0x7F, true));
  TEST_ASSERT_EQUAL_HEX8(0x7F, ds3231WithEosc(0xFF, false));
}

// The new test-build commands, and refused outside test builds.
void test_hwdog_and_rtc_commands() {
  CommandArgs a;
  TEST_ASSERT_TRUE(parseCommand("hwdog off", true, a) == Command::HwDog);
  TEST_ASSERT_EQUAL_INT(0, a.arg);
  TEST_ASSERT_TRUE(parseCommand("hwdog on", true, a) == Command::HwDog);
  TEST_ASSERT_EQUAL_INT(1, a.arg);
  TEST_ASSERT_TRUE(parseCommand("hwdog", true, a) == Command::HwDog);
  TEST_ASSERT_EQUAL_INT(-2, a.arg);
  TEST_ASSERT_TRUE(parseCommand("hwdog off", false, a) == Command::TestOnly);
  TEST_ASSERT_TRUE(parseCommand("rtc", true, a) == Command::Rtc);
  TEST_ASSERT_EQUAL_INT(-1, a.arg);
  TEST_ASSERT_TRUE(parseCommand("rtc eosc on", true, a) == Command::Rtc);
  TEST_ASSERT_EQUAL_INT(1, a.arg);
  TEST_ASSERT_TRUE(parseCommand("RTC EOSC OFF", true, a) == Command::Rtc);
  TEST_ASSERT_EQUAL_INT(0, a.arg);
  TEST_ASSERT_TRUE(parseCommand("rtc eosc", true, a) == Command::Rtc);
  TEST_ASSERT_EQUAL_INT(-2, a.arg);
  TEST_ASSERT_TRUE(parseCommand("rtc osf on", true, a) == Command::Rtc);
  TEST_ASSERT_EQUAL_INT(-2, a.arg);
  TEST_ASSERT_TRUE(parseCommand("rtc eosc on", false, a) == Command::TestOnly);
  TEST_ASSERT_TRUE(parseCommand("stall", true, a) == Command::Stall);          // unchanged
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_nothing_until_g);
  RUN_TEST(test_steps_then_finish);
  RUN_TEST(test_x_aborts_any_time);
  RUN_TEST(test_empty_line_and_no_steps);
  RUN_TEST(test_scale_from_meter);
  RUN_TEST(test_u8_input_current);
  RUN_TEST(test_blower_duty);
  RUN_TEST(test_eosc_bit_only);
  RUN_TEST(test_hwdog_and_rtc_commands);
  return UNITY_END();
}
