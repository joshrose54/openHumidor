// Rev Q's touch controller, FT6336U at 0x38 (spec 3A.7, 3A.9; lib/core/touch_decode.h).
#include <unity.h>
#include "wineador/touch_decode.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

void test_address_and_registers() {
  TEST_ASSERT_EQUAL_HEX8(0x38, kTouchAddr);          // 3A.7
  TEST_ASSERT_EQUAL_HEX8(0x02, kTouchFirstReg);
  TEST_ASSERT_EQUAL_INT(5, kTouchReadLen);
}

void test_no_finger() {
  const uint8_t b[5] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF};
  const TouchPoint t = decodeTouch(b);
  TEST_ASSERT_TRUE(t.valid);
  TEST_ASSERT_EQUAL_UINT8(0, t.count);
}

// X and Y are 12 bits: the low four bits of XH / YH, then XL / YL. The flag and ID bits are ignored.
void test_one_touch() {
  const uint8_t b[5] = {0x01, 0x80 | 0x01, 0x2C, 0x10 | 0x01, 0xC2};   // x = 0x12C = 300, y = 0x1C2 = 450
  const TouchPoint t = decodeTouch(b);
  TEST_ASSERT_TRUE(t.valid);
  TEST_ASSERT_EQUAL_UINT8(1, t.count);
  TEST_ASSERT_EQUAL_UINT16(300, t.x);
  TEST_ASSERT_EQUAL_UINT16(450, t.y);
}

void test_two_touches_report_the_first() {
  const uint8_t b[5] = {0x02, 0x00, 0x05, 0x00, 0x07};
  const TouchPoint t = decodeTouch(b);
  TEST_ASSERT_TRUE(t.valid);
  TEST_ASSERT_EQUAL_UINT8(2, t.count);
  TEST_ASSERT_EQUAL_UINT16(5, t.x);
  TEST_ASSERT_EQUAL_UINT16(7, t.y);
}

// A count over 2 (e.g. 0xFF from a bus with nothing on it) is not a valid read.
void test_bad_count_is_invalid() {
  const uint8_t b[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  TEST_ASSERT_FALSE(decodeTouch(b).valid);
  const uint8_t c[5] = {0x03, 0, 0, 0, 0};
  TEST_ASSERT_FALSE(decodeTouch(c).valid);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_address_and_registers);
  RUN_TEST(test_no_finger);
  RUN_TEST(test_one_touch);
  RUN_TEST(test_two_touches_report_the_first);
  RUN_TEST(test_bad_count_is_invalid);
  return UNITY_END();
}
