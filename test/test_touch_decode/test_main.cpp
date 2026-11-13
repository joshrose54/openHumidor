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

// Monitor mode off after every reset (3A.11): nothing before a reset or within 300 ms (well,
// kLcdResetToTouchMs) of one; then the 0x86 write; then plain reads until the next reset.
using N = TouchSetup::Next;

void test_setup_register_and_ids() {
  TEST_ASSERT_EQUAL_HEX8(0x86, kTouchRegCtrl);
  TEST_ASSERT_EQUAL_HEX8(0x00, kTouchCtrlStayActive);
  TEST_ASSERT_TRUE(isFt6336(0x11, 0x26, 0x64));
  TEST_ASSERT_FALSE(isFt6336(0x11, 0x26, 0x00));
  TEST_ASSERT_FALSE(isFt6336(0xFF, 0xFF, 0xFF));             // what an empty bus reads
}

void test_setup_after_each_reset() {
  TouchSetup s;
  TEST_ASSERT_TRUE(s.next(0, 0, 5000) == N::Wait);            // no reset yet: no I2C
  // Boot reset released at 1000 ms.
  TEST_ASSERT_TRUE(s.next(1, 1000, 1000) == N::Wait);
  TEST_ASSERT_TRUE(s.next(1, 1000, 1000 + kLcdResetToTouchMs - 1) == N::Wait);
  TEST_ASSERT_TRUE(s.next(1, 1000, 1000 + kLcdResetToTouchMs) == N::WriteCtrl);
  TEST_ASSERT_FALSE(s.done(1));
  s.written(1, true);
  TEST_ASSERT_TRUE(s.done(1));
  TEST_ASSERT_TRUE(s.next(1, 1000, 9000) == N::Read);
  TEST_ASSERT_TRUE(s.next(1, 1000, 60000) == N::Read);        // stays read-only
  TEST_ASSERT_FALSE(s.takeFailure());
  // GPIO48 pulsed again (test 29 step 5) at 70000 ms: wait, then write again.
  TEST_ASSERT_FALSE(s.done(2));
  TEST_ASSERT_TRUE(s.next(2, 70000, 70100) == N::Wait);
  TEST_ASSERT_TRUE(s.next(2, 70000, 70000 + kLcdResetToTouchMs) == N::WriteCtrl);
  s.written(2, true);
  TEST_ASSERT_TRUE(s.next(2, 70000, 80000) == N::Read);
}

// A failed write: reported once after that reset, tried again at every poll until it works.
void test_setup_failure_logged_once() {
  TouchSetup s;
  const uint32_t t = 1000 + kLcdResetToTouchMs;
  TEST_ASSERT_TRUE(s.next(1, 1000, t) == N::WriteCtrl);
  s.written(1, false);
  TEST_ASSERT_TRUE(s.takeFailure());
  TEST_ASSERT_FALSE(s.takeFailure());                         // taken
  TEST_ASSERT_TRUE(s.next(1, 1000, t + 50) == N::WriteCtrl);  // tried again
  s.written(1, false);
  TEST_ASSERT_FALSE(s.takeFailure());                         // not reported twice for one reset
  s.written(1, true);
  TEST_ASSERT_TRUE(s.next(1, 1000, t + 100) == N::Read);
  // After the next reset a failure is reported again (once).
  s.written(2, false);
  s.written(2, false);
  TEST_ASSERT_TRUE(s.takeFailure());
  TEST_ASSERT_FALSE(s.takeFailure());
  TEST_ASSERT_TRUE(s.next(2, 5000, 5000 + kLcdResetToTouchMs) == N::WriteCtrl);
}

// millis() wrapping past 2^32 between the reset and the poll changes nothing.
void test_setup_across_millis_wrap() {
  TouchSetup s;
  const uint32_t rel = 0xFFFFFF00u;
  TEST_ASSERT_TRUE(s.next(1, rel, rel + 100) == N::Wait);
  TEST_ASSERT_TRUE(s.next(1, rel, rel + kLcdResetToTouchMs) == N::WriteCtrl);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_address_and_registers);
  RUN_TEST(test_no_finger);
  RUN_TEST(test_one_touch);
  RUN_TEST(test_two_touches_report_the_first);
  RUN_TEST(test_bad_count_is_invalid);
  RUN_TEST(test_setup_register_and_ids);
  RUN_TEST(test_setup_after_each_reset);
  RUN_TEST(test_setup_failure_logged_once);
  RUN_TEST(test_setup_across_millis_wrap);
  return UNITY_END();
}
