// Rev Q's LCD start-up (spec 3A.9, 3A.11; lib/core/lcd_seq.h): the reset timings against the
// spec's minimums, the start-up commands, the real SPI clock and the test 29 pattern.
#include <unity.h>
#include <stdio.h>
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

// 3A.9: SPI2 at 15 MHz or less - the clock the S3 really makes, not the one asked for. Its SPI
// clock is 80 MHz / a whole number, and ESP-IDF picks the divider nearest the request.
void test_spi_clock_limit() {
  TEST_ASSERT_EQUAL_UINT32(13333333UL, s3SpiClockHz(kSpiSourceHz, kLcdSpiHz));   // 80 / 6
  TEST_ASSERT_TRUE(s3SpiClockHz(kSpiSourceHz, kLcdSpiHz) <= 15000000UL);
}

// The 20 MHz trial (BP2 test 29) is exactly 80 / 4.
void test_spi_trial_clock() { TEST_ASSERT_EQUAL_UINT32(20000000UL, s3SpiClockHz(kSpiSourceHz, kLcdTrialSpiHz)); }

// Why we don't ask for 15 MHz: the nearest divider is 80 / 5 = 16 MHz (62.5 ns, under the
// ST7796S's 66 ns). And the arithmetic's other cases, as ESP-IDF's spi_ll_master_cal_clock.
void test_spi_divider_arithmetic() {
  TEST_ASSERT_EQUAL_UINT32(16000000UL, s3SpiClockHz(kSpiSourceHz, 15000000UL));
  TEST_ASSERT_EQUAL_UINT32(80000000UL, s3SpiClockHz(kSpiSourceHz, 70000000UL));  // above 3/4: the source itself
  TEST_ASSERT_EQUAL_UINT32(40000000UL, s3SpiClockHz(kSpiSourceHz, 40000000UL));
  TEST_ASSERT_EQUAL_UINT32(10000000UL, s3SpiClockHz(kSpiSourceHz, 10000000UL));
  TEST_ASSERT_EQUAL_UINT32(1000000UL, s3SpiClockHz(kSpiSourceHz, 1000000UL));
}

// The whole start-up list, step by step (3A.11): Sleep Out and its 120 ms, then lcdwiki's
// ST7796_Init.txt for the MSP4031 in its order (C2 with 0xA7 as its parameter; the file's own
// Sleep Out and Display On left out), then inversion off. Display On is not in the list: it is
// sent after the screen has been cleared to black. This copy is typed from the maker's file
// separately from lcd_seq.h, so a slip in either shows.
struct Want { uint8_t cmd; uint8_t n; uint8_t a[14]; uint16_t wait; };
static const Want kWant[] = {
  {0x11, 0, {0}, 120},
  {0xF0, 1, {0xC3}, 0},
  {0xF0, 1, {0x96}, 0},
  {0x36, 1, {0x48}, 0},
  {0x3A, 1, {0x05}, 0},
  {0xB0, 1, {0x80}, 0},
  {0xB6, 2, {0x00, 0x02}, 0},
  {0xB5, 4, {0x02, 0x03, 0x00, 0x04}, 0},
  {0xB1, 2, {0x80, 0x10}, 0},
  {0xB4, 1, {0x00}, 0},
  {0xB7, 1, {0xC6}, 0},
  {0xC5, 1, {0x1C}, 0},
  {0xE4, 1, {0x31}, 0},
  {0xE8, 8, {0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33}, 0},
  {0xC2, 1, {0xA7}, 0},
  {0xE0, 14, {0xF0, 0x09, 0x13, 0x12, 0x12, 0x2B, 0x3C, 0x44, 0x4B, 0x1B, 0x18, 0x17, 0x1D, 0x21}, 0},
  {0xE1, 14, {0xF0, 0x09, 0x13, 0x0C, 0x0D, 0x27, 0x3B, 0x44, 0x4D, 0x0B, 0x17, 0x17, 0x1D, 0x21}, 0},
  {0xF0, 1, {0x3C}, 0},
  {0xF0, 1, {0x69}, 0},
  {0x13, 0, {0}, 0},
  {0x20, 0, {0}, 0},
};

void test_start_up_commands() {
  TEST_ASSERT_EQUAL_UINT32(sizeof(kWant) / sizeof(kWant[0]), kLcdInitSteps);
  for (size_t i = 0; i < kLcdInitSteps; i++) {
    char m[32];
    snprintf(m, sizeof m, "step %u (0x%02X)", (unsigned)i, kWant[i].cmd);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(kWant[i].cmd, kLcdInit[i].cmd, m);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(kWant[i].n, kLcdInit[i].nArgs, m);
    TEST_ASSERT_TRUE_MESSAGE(kLcdInit[i].nArgs <= kLcdMaxArgs, m);
    for (uint8_t k = 0; k < kWant[i].n; k++) TEST_ASSERT_EQUAL_HEX8_MESSAGE(kWant[i].a[k], kLcdInit[i].args[k], m);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(kWant[i].wait, kLcdInit[i].waitMs, m);
  }
}

// The rules the list must keep whatever its values: Sleep Out first with at least 120 ms; never
// Display On, Sleep Out again or 0xA7 as a command; the extension commands unlocked before any
// is sent and locked again at the end.
void test_start_up_rules() {
  TEST_ASSERT_EQUAL_HEX8(kLcdSleepOut, kLcdInit[0].cmd);
  TEST_ASSERT_TRUE(kLcdInit[0].waitMs >= 120);
  bool unlocked = false;
  for (size_t i = 1; i < kLcdInitSteps; i++) {
    const uint8_t c = kLcdInit[i].cmd;
    TEST_ASSERT_TRUE(c != kLcdDisplayOn && c != kLcdSleepOut && c != 0xA7);
    if (c == 0xF0 && kLcdInit[i].args[0] == 0x96) unlocked = true;
    if (c == 0xF0 && kLcdInit[i].args[0] == 0x69) unlocked = false;
    if (c >= 0xB0 && c != 0xF0) TEST_ASSERT_TRUE(unlocked);
    if (c == 0xC2) { TEST_ASSERT_EQUAL_UINT8(1, kLcdInit[i].nArgs); TEST_ASSERT_EQUAL_HEX8(0xA7, kLcdInit[i].args[0]); }
  }
  TEST_ASSERT_FALSE(unlocked);
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
  RUN_TEST(test_spi_trial_clock);
  RUN_TEST(test_spi_divider_arithmetic);
  RUN_TEST(test_start_up_commands);
  RUN_TEST(test_start_up_rules);
  RUN_TEST(test_window_bytes);
  RUN_TEST(test_colour_bars);
  return UNITY_END();
}
