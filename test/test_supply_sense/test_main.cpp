// Rev Q's 12V sense (spec 3A.3, 3A.11, 5.4; lib/core/supply.h): volts from the pin, 1 s averages,
// the readings row's figures, and the dip / recovered events.
#include <unity.h>
#include "wineador/supply.h"

using namespace wineador;
using K = SupplySense::Event::Kind;

void setUp() {}
void tearDown() {}

// Feeds `n` readings of `v`, 100 ms apart, from *t.
static void feed(SupplySense& s, uint32_t* t, int n, float v) {
  for (int i = 0; i < n; i++) { s.add(v, *t); *t += 100; }
}

void test_volts_from_the_pin() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.6667f, kDividerScale12V);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 12.04f, supplyVolts(1570, kDividerScale12V));   // 3A.3: 12V reads 1.57 V
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 23.8f, supplyVolts(3100, kDividerScale12V));     // the 12 dB range's top
  TEST_ASSERT_EQUAL_FLOAT(0.0f, supplyVolts(0, kDividerScale12V));
}

// Ten readings make one 1 s average.
void test_one_second_averages() {
  SupplySense s;
  uint32_t t = 0;
  for (int i = 0; i < 9; i++) { TEST_ASSERT_FALSE(s.add(12.0f + 0.1f * i, t)); t += 100; }
  TEST_ASSERT_FALSE(s.haveAverage());
  TEST_ASSERT_TRUE(s.add(12.9f, t));
  TEST_ASSERT_TRUE(s.haveAverage());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.45f, s.lastAverage());
}

// The readings row: the mean of the 1 s averages and the lowest 1 s average since the last row.
void test_interval_mean_and_lowest() {
  SupplySense s;
  uint32_t t = 0;
  float mean, low;
  TEST_ASSERT_FALSE(s.takeInterval(&mean, &low));    // nothing yet: the columns stay empty
  feed(s, &t, 10, 12.0f);
  feed(s, &t, 10, 11.5f);
  feed(s, &t, 10, 12.5f);
  TEST_ASSERT_TRUE(s.takeInterval(&mean, &low));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.0f, mean);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 11.5f, low);
  TEST_ASSERT_FALSE(s.takeInterval(&mean, &low));    // taken: a fresh interval starts
  feed(s, &t, 10, 12.2f);
  TEST_ASSERT_TRUE(s.takeInterval(&mean, &low));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.2f, low);
}

// A dip is below 11.0 V for 1 s or more: nine low readings are not a dip, ten are.
void test_dip_needs_a_full_second() {
  SupplySense s;
  uint32_t t = 0;
  SupplySense::Event e;
  feed(s, &t, 20, 12.0f);
  feed(s, &t, 9, 10.5f);
  feed(s, &t, 1, 11.2f);                             // back up before the second was over
  TEST_ASSERT_FALSE(s.takeEvent(e));
  TEST_ASSERT_FALSE(s.inDip());
  feed(s, &t, 10, 10.8f);
  TEST_ASSERT_TRUE(s.takeEvent(e));
  TEST_ASSERT_TRUE(e.kind == K::Dip);
  TEST_ASSERT_TRUE(s.inDip());
  TEST_ASSERT_FALSE(s.takeEvent(e));                 // one dip event, however long it lasts
  feed(s, &t, 30, 10.8f);
  TEST_ASSERT_FALSE(s.takeEvent(e));
}

// Recovered: at or above 11.0 V for 1 s, with the lowest reading during the dip and how long it lasted.
void test_recovered_with_the_lowest_value() {
  SupplySense s;
  uint32_t t = 10000;
  SupplySense::Event e;
  feed(s, &t, 10, 12.0f);
  const uint32_t dipStart = t;
  feed(s, &t, 5, 10.9f);
  feed(s, &t, 1, 9.7f);                              // the lowest
  feed(s, &t, 14, 10.6f);                            // 20 readings = 2 s below
  TEST_ASSERT_TRUE(s.takeEvent(e));
  TEST_ASSERT_TRUE(e.kind == K::Dip);
  feed(s, &t, 5, 11.4f);
  feed(s, &t, 1, 10.9f);                             // dips again before a full second above: still in the dip
  TEST_ASSERT_FALSE(s.takeEvent(e));
  const uint32_t backAt = t;
  feed(s, &t, 10, 11.6f);
  TEST_ASSERT_TRUE(s.takeEvent(e));
  TEST_ASSERT_TRUE(e.kind == K::Recovered);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 9.7f, e.lowestV);
  TEST_ASSERT_EQUAL_UINT32(backAt - dipStart, e.durationMs);
  TEST_ASSERT_FALSE(s.inDip());
}

// Exactly 11.0 V is not below 11.0 V.
void test_exactly_eleven_is_not_a_dip() {
  SupplySense s;
  uint32_t t = 0;
  SupplySense::Event e;
  feed(s, &t, 50, 11.0f);
  TEST_ASSERT_FALSE(s.takeEvent(e));
}

// The hourly row has its own window: taking the readings row's figures doesn't empty it.
void test_hour_window_is_separate() {
  SupplySense s;
  uint32_t t = 0;
  float mean, low;
  feed(s, &t, 10, 12.0f);
  feed(s, &t, 10, 11.4f);
  TEST_ASSERT_TRUE(s.takeInterval(&mean, &low));       // a readings row
  feed(s, &t, 10, 12.6f);
  TEST_ASSERT_TRUE(s.takeInterval(&mean, &low));       // another
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.6f, low);
  TEST_ASSERT_TRUE(s.takeHour(&mean, &low));           // the hour saw all three seconds
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.0f, mean);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 11.4f, low);
  TEST_ASSERT_FALSE(s.takeHour(&mean, &low));
}

// The Board section's lowest today (5.5): reset at UK midnight, not UTC midnight; carried on
// while the clock isn't trusted.
void test_lowest_today_resets_at_uk_midnight() {
  const int64_t julyNoonUtc = 1783166400;    // 2026-07-04T12:00:00Z (BST)
  DayLow d;
  TEST_ASSERT_FALSE(d.have());
  d.add(11.9f, julyNoonUtc, true);
  d.add(12.1f, julyNoonUtc + 60, true);
  TEST_ASSERT_EQUAL_FLOAT(11.9f, d.low());
  d.add(12.2f, julyNoonUtc + 11 * 3600 + 1800, true);   // 23:30 UTC = 00:30 BST: a new UK day
  TEST_ASSERT_EQUAL_FLOAT(12.2f, d.low());
  d.add(11.5f, 0, false);                               // the clock lost: carries on
  TEST_ASSERT_EQUAL_FLOAT(11.5f, d.low());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_volts_from_the_pin);
  RUN_TEST(test_one_second_averages);
  RUN_TEST(test_interval_mean_and_lowest);
  RUN_TEST(test_dip_needs_a_full_second);
  RUN_TEST(test_recovered_with_the_lowest_value);
  RUN_TEST(test_exactly_eleven_is_not_a_dip);
  RUN_TEST(test_hour_window_is_separate);
  RUN_TEST(test_lowest_today_resets_at_uk_midnight);
  return UNITY_END();
}
