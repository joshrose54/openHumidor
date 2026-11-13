// Rev Q's LCD start-up (spec 3A.9, 3A.11; lib/core/lcd_seq.h): the reset timings against the
// spec's minimums, the start-up commands, the SPI clock limit and the test 29 pattern.
#include <unity.h>
#include "wineador/lcd_seq.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

// 3A.11: GPIO48 low at least 5 ms; at least 120 ms before Sleep Out; at least 300 ms before a touch read.
void test_reset_timings_meet_the_spec() {
  TEST_ASSERT_TRUE(kLcdResetLowMs >= 5);
  TEST_ASSERT_TRUE(kLcdResetToSleepOutMs >= 120);
  TEST_ASSERT_TRUE(kLcdResetToTouchMs >= 300);
}

// 3A.9: SPI2 at 15 MHz or less.
void test_spi_clock_limit() { TEST_ASSERT_TRUE(kLcdSpiHz <= 15000000UL); }

// Sleep Out first, with its 120 ms; 16 bits a pixel; Display On is not in the list (it is sent
// after the screen has been cleared to black, so nothing else ever shows).
void test_start_up_commands() {
  TEST_ASSERT_TRUE(kLcdInitSteps >= 3);
  TEST_ASSERT_EQUAL_HEX8(kLcdSleepOut, kLcdInit[0].cmd);
  TEST_ASSERT_TRUE(kLcdInit[0].waitMs >= 120);
  bool pixelFormat = false;
  for (size_t i = 0; i < kLcdInitSteps; i++) {
    TEST_ASSERT_TRUE(kLcdInit[i].cmd != kLcdDisplayOn);
    TEST_ASSERT_TRUE(kLcdInit[i].nArgs <= 4);
    if (kLcdInit[i].cmd == kLcdPixelFormat) { pixelFormat = true; TEST_ASSERT_EQUAL_HEX8(0x55, kLcdInit[i].args[0]); }
  }
  TEST_ASSERT_TRUE(pixelFormat);
}

void test_window_bytes() {
  uint8_t a[4];
  lcdWindowArgs(0, 479, a);
  TEST_ASSERT_EQUAL_HEX8(0x00, a[0]);
  TEST_ASSERT_EQUAL_HEX8(0x00, a[1]);
  TEST_ASSERT_EQUAL_HEX8(0x01, a[2]);
  TEST_ASSERT_EQUAL_HEX8(0xDF, a[3]);
}

// Eight bars of 40 pixels across the 320-pixel width, white first and black last.
void test_colour_bars() {
  TEST_ASSERT_EQUAL_HEX16(kWhite, colourBarAt(0, 0));
  TEST_ASSERT_EQUAL_HEX16(kWhite, colourBarAt(39, 479));
  TEST_ASSERT_EQUAL_HEX16(kYellow, colourBarAt(40, 0));
  TEST_ASSERT_EQUAL_HEX16(kRed, colourBarAt(5 * 40, 100));
  TEST_ASSERT_EQUAL_HEX16(kBlack, colourBarAt(319, 0));
  TEST_ASSERT_EQUAL_HEX16(kBlack, colourBarAt(400, 0));          // past the edge: the last bar
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_reset_timings_meet_the_spec);
  RUN_TEST(test_spi_clock_limit);
  RUN_TEST(test_start_up_commands);
  RUN_TEST(test_window_bytes);
  RUN_TEST(test_colour_bars);
  return UNITY_END();
}
