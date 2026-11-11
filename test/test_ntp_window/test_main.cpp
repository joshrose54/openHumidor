// WiFi only for the NTP sync (spec 5.4, Rev N): when the radio window opens and closes.
#include <unity.h>
#include "wineador/ntp_window.h"

using namespace wineador;
using A = NtpWindow::Action;

void setUp() {}
void tearDown() {}

void test_opens_at_boot_and_closes_on_sync() {
  NtpWindow w;
  TEST_ASSERT_TRUE(w.update(1000, 0) == A::Open);
  TEST_ASSERT_TRUE(w.isOpen());
  TEST_ASSERT_TRUE(w.update(5000, 0) == A::None);
  TEST_ASSERT_TRUE(w.update(9000, 1) == A::CloseSynced);     // synced: radio off at once
  TEST_ASSERT_FALSE(w.isOpen());
  TEST_ASSERT_EQUAL_UINT32(NtpWindow::kIntervalMs, w.waitMs());
}

void test_closes_after_two_minutes_without_a_sync() {
  NtpWindow w;
  w.update(0, 0);
  TEST_ASSERT_TRUE(w.update(NtpWindow::kWindowMs - 1, 0) == A::None);
  TEST_ASSERT_TRUE(w.update(NtpWindow::kWindowMs, 0) == A::CloseTimedOut);
  TEST_ASSERT_EQUAL_UINT32(NtpWindow::kRetryMs, w.waitMs());
}

void test_next_window_24_h_after_a_sync_1_h_after_a_miss() {
  NtpWindow w;
  w.update(0, 0);
  w.update(10000, 1);                                          // synced at 10 s
  TEST_ASSERT_TRUE(w.update(10000 + NtpWindow::kIntervalMs - 1, 1) == A::None);
  TEST_ASSERT_TRUE(w.update(10000 + NtpWindow::kIntervalMs, 1) == A::Open);
  const uint32_t t = 10000 + NtpWindow::kIntervalMs + NtpWindow::kWindowMs;
  TEST_ASSERT_TRUE(w.update(t, 1) == A::CloseTimedOut);       // no sync this time
  TEST_ASSERT_TRUE(w.update(t + NtpWindow::kRetryMs - 1, 1) == A::None);
  TEST_ASSERT_TRUE(w.update(t + NtpWindow::kRetryMs, 1) == A::Open);
}

void test_request_now_opens_at_once_only_when_closed() {
  NtpWindow w;
  w.update(0, 0);
  w.update(1000, 1);                                           // closed, next in 24 h
  w.requestNow();
  TEST_ASSERT_TRUE(w.update(2000, 1) == A::Open);
  w.requestNow();                                              // already open: nothing new
  TEST_ASSERT_TRUE(w.update(3000, 1) == A::None);
  TEST_ASSERT_TRUE(w.update(4000, 2) == A::CloseSynced);
}

void test_survives_millis_wrap() {
  NtpWindow w;
  const uint32_t t0 = 0xFFFFFFFFu - 30000;                     // 30 s before millis() wraps
  w.update(t0, 0);
  TEST_ASSERT_TRUE(w.update(t0 + 60000, 0) == A::None);       // 60 s on, past the wrap
  TEST_ASSERT_TRUE(w.update(t0 + NtpWindow::kWindowMs, 0) == A::CloseTimedOut);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_opens_at_boot_and_closes_on_sync);
  RUN_TEST(test_closes_after_two_minutes_without_a_sync);
  RUN_TEST(test_next_window_24_h_after_a_sync_1_h_after_a_miss);
  RUN_TEST(test_request_now_opens_at_once_only_when_closed);
  RUN_TEST(test_survives_millis_wrap);
  return UNITY_END();
}
