// The SD outage buffers (spec 5.4): lines kept in order at their real length,
// oldest dropped and counted at the line limit or the byte budget.
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include "wineador/line_ring.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

static void push(LineRing& r, const char* s) { TEST_ASSERT_TRUE(r.push(s, strlen(s))); }

void test_keeps_lines_in_order() {
  char store[256];
  LineRing r(store, sizeof(store), 10);
  push(r, "first");
  push(r, "second line");
  push(r, "third");
  TEST_ASSERT_EQUAL_UINT16(3, r.count());
  char out[64];
  TEST_ASSERT_EQUAL_UINT32(5, r.peek(0, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("first", out);
  r.peek(1, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("second line", out);
  r.peek(2, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("third", out);
  TEST_ASSERT_EQUAL_UINT32(0, r.peek(3, out, sizeof(out)));
  r.pop(2);
  r.peek(0, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("third", out);
  TEST_ASSERT_EQUAL_UINT32(0, r.dropped());       // written, not dropped
}

void test_line_limit_drops_oldest_and_counts() {
  char store[1024];
  LineRing r(store, sizeof(store), 3);
  char s[16];
  for (int i = 0; i < 5; i++) { snprintf(s, sizeof(s), "row %d", i); push(r, s); }
  TEST_ASSERT_EQUAL_UINT16(3, r.count());
  TEST_ASSERT_EQUAL_UINT32(2, r.dropped());
  char out[16];
  r.peek(0, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("row 2", out);         // the oldest kept
  r.peek(2, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("row 4", out);
}

void test_byte_budget_drops_oldest_and_wraps() {
  char store[64];                                  // small: forces wrap-around
  LineRing r(store, sizeof(store), 100);
  char s[32];
  for (int i = 0; i < 40; i++) {
    snprintf(s, sizeof(s), "line-%02d-xxxxxxxx", i); // 16 chars + 2 header = 18 bytes
    push(r, s);
    TEST_ASSERT_TRUE(r.bytesUsed() <= sizeof(store));
  }
  TEST_ASSERT_EQUAL_UINT16(3, r.count());          // 3 x 18 = 54 of 64 bytes
  TEST_ASSERT_EQUAL_UINT32(37, r.dropped());
  char out[32];
  for (int k = 0; k < 3; k++) {                    // still in order after many wraps
    r.peek(k, out, sizeof(out));
    snprintf(s, sizeof(s), "line-%02d-xxxxxxxx", 37 + k);
    TEST_ASSERT_EQUAL_STRING(s, out);
  }
}

void test_line_too_big_is_refused() {
  char store[32];
  LineRing r(store, sizeof(store), 10);
  char big[40];
  memset(big, 'x', sizeof(big));
  TEST_ASSERT_FALSE(r.push(big, sizeof(big)));
  TEST_ASSERT_EQUAL_UINT16(0, r.count());
  push(r, "ok");
  TEST_ASSERT_EQUAL_UINT16(1, r.count());
}

void test_no_psram_fallback_holds_the_spec_counts() {
  // The no-PSRAM fallback's budgets (include/board.h, 5.4: the sizes Rev N ran) must hold the full
  // counts of typical lines.
  static char ev[12000], rows[14000], hr[9000];
  LineRing e(ev, sizeof(ev), 60), r(rows, sizeof(rows), 30), h(hr, sizeof(hr), 24);
  char line[600];
  memset(line, 'e', 190);
  for (int i = 0; i < 60; i++) TEST_ASSERT_TRUE(e.push(line, 190));
  TEST_ASSERT_EQUAL_UINT16(60, e.count());
  TEST_ASSERT_EQUAL_UINT32(0, e.dropped());
  memset(line, 'r', 460);                          // a test-build readings row
  for (int i = 0; i < 30; i++) TEST_ASSERT_TRUE(r.push(line, 460));
  TEST_ASSERT_EQUAL_UINT16(30, r.count());
  TEST_ASSERT_EQUAL_UINT32(0, r.dropped());
  memset(line, 'h', 360);
  for (int i = 0; i < 24; i++) TEST_ASSERT_TRUE(h.push(line, 360));
  TEST_ASSERT_EQUAL_UINT16(24, h.count());
  TEST_ASSERT_EQUAL_UINT32(0, h.dropped());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_keeps_lines_in_order);
  RUN_TEST(test_line_limit_drops_oldest_and_counts);
  RUN_TEST(test_byte_budget_drops_oldest_and_wraps);
  RUN_TEST(test_line_too_big_is_refused);
  RUN_TEST(test_no_psram_fallback_holds_the_spec_counts);
  return UNITY_END();
}
