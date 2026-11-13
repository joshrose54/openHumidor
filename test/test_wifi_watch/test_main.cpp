// Rev Q's WiFi events (spec 5.4; lib/core/wifi_watch.h): "WiFi lost" and "back after N s".
#include <unity.h>
#include "wineador/wifi_watch.h"
#include "wineador/ntp_window.h"

using namespace wineador;
using E = WifiWatch::Event;

void setUp() {}
void tearDown() {}

// Connecting at boot is not a loss; nothing is reported while it never connected.
void test_boot_is_not_an_outage() {
  WifiWatch w;
  for (uint32_t ms = 0; ms < 60000; ms += 100) TEST_ASSERT_TRUE(w.update(false, ms) == E::None);
  TEST_ASSERT_TRUE(w.update(true, 60000) == E::FirstConnect);
  TEST_ASSERT_TRUE(w.update(true, 60100) == E::None);
  TEST_ASSERT_EQUAL_UINT32(0, w.outages());
}

// Lost, then back with how long it was gone; once each.
void test_lost_then_back_after_n_seconds() {
  WifiWatch w;
  w.update(true, 1000);
  TEST_ASSERT_TRUE(w.update(false, 5000) == E::Lost);
  TEST_ASSERT_TRUE(w.update(false, 5100) == E::None);
  TEST_ASSERT_TRUE(w.update(true, 47000) == E::Back);
  TEST_ASSERT_EQUAL_UINT32(42000, w.lastOutageMs());
  TEST_ASSERT_TRUE(w.update(true, 47100) == E::None);
  TEST_ASSERT_EQUAL_UINT32(1, w.outages());
}

void test_outage_across_millis_wrap() {
  WifiWatch w;
  w.update(true, 0);
  w.update(false, 0xFFFFF000u);
  TEST_ASSERT_TRUE(w.update(true, 0x00001000u) == E::Back);
  TEST_ASSERT_EQUAL_UINT32(0x2000, w.lastOutageMs());
}

// Rev Q uses Rev N's NTP window to start and stop SNTP (5.4): at boot, 24 h after a sync,
// 1 h after a window with no sync. (The window's own tests are in test_ntp_window.)
void test_ntp_schedule_with_wifi_always_on() {
  NtpWindow n;
  TEST_ASSERT_TRUE(n.update(0, 0) == NtpWindow::Action::Open);              // at boot
  TEST_ASSERT_TRUE(n.update(30000, 1) == NtpWindow::Action::CloseSynced);   // synced: SNTP stops
  TEST_ASSERT_EQUAL_UINT32(NtpWindow::kIntervalMs, n.waitMs());              // next in 24 h
  TEST_ASSERT_TRUE(n.update(30000 + NtpWindow::kIntervalMs, 1) == NtpWindow::Action::Open);
  TEST_ASSERT_TRUE(n.update(30000 + NtpWindow::kIntervalMs + NtpWindow::kWindowMs, 1) ==
                   NtpWindow::Action::CloseTimedOut);                          // a miss (e.g. WiFi down)
  TEST_ASSERT_EQUAL_UINT32(NtpWindow::kRetryMs, n.waitMs());                 // next try in 1 h
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_boot_is_not_an_outage);
  RUN_TEST(test_lost_then_back_after_n_seconds);
  RUN_TEST(test_outage_across_millis_wrap);
  RUN_TEST(test_ntp_schedule_with_wifi_always_on);
  return UNITY_END();
}
