// The interlock switching sequence (spec 3.2), the relay exercise (5.2) and the
// whole minimum job together (Controller), against a fake clock.
#include <unity.h>
#include <stdlib.h>
#include "wineador/interlock.h"
#include "wineador/relay_exercise.h"
#include "wineador/controller.h"
#include "wineador/settings.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------- sequence
void test_rests_on_cooling_side_and_cools_at_once() {
  InterlockSequencer s;
  s.begin(20, 0);
  LoadOutputs o = s.step(1, true, false, false);
  TEST_ASSERT_FALSE(o.relayHeating);
  TEST_ASSERT_TRUE(o.cool);          // already on the cooling side and settled
  TEST_ASSERT_EQUAL_UINT32(0, s.relayMoves());
}

void test_heat_waits_20ms_after_k1_moves() {
  InterlockSequencer s;
  s.begin(20, 0);
  LoadOutputs o = s.step(1000, false, true, false);
  TEST_ASSERT_TRUE(o.relayHeating);  // K1 moves...
  TEST_ASSERT_FALSE(o.heat);         // ...the heater waits
  o = s.step(1019, false, true, false);
  TEST_ASSERT_FALSE(o.heat);
  o = s.step(1020, false, true, false);
  TEST_ASSERT_TRUE(o.heat);
}

void test_back_to_cooling_drops_heat_first_then_waits() {
  InterlockSequencer s;
  s.begin(20, 0);
  s.step(0, false, true, false);
  LoadOutputs o = s.step(100, false, true, false);
  TEST_ASSERT_TRUE(o.heat);
  o = s.step(200, true, false, false);   // the guards now allow cooling instead
  TEST_ASSERT_FALSE(o.heat);
  TEST_ASSERT_FALSE(o.relayHeating);
  TEST_ASSERT_FALSE(o.cool);
  o = s.step(219, true, false, false);
  TEST_ASSERT_FALSE(o.cool);
  o = s.step(220, true, false, false);
  TEST_ASSERT_TRUE(o.cool);
}

void test_both_wanted_neither_asserted() {
  InterlockSequencer s;
  s.begin(20, 0);
  LoadOutputs o = s.step(100, true, true, true);
  TEST_ASSERT_FALSE(o.cool);
  TEST_ASSERT_FALSE(o.heat);
  TEST_ASSERT_TRUE(o.humid);
}

void test_relay_hold_asserts_no_temperature_load() {
  InterlockSequencer s;
  s.begin(20, 0);
  LoadOutputs o = s.step(100, true, false, true, 1);
  TEST_ASSERT_TRUE(o.relayHeating);
  o = s.step(500, false, true, true, 1);
  TEST_ASSERT_FALSE(o.heat);         // held: never asserted, even settled on its side
  TEST_ASSERT_TRUE(o.humid);
  o = s.step(600, true, false, true, 0);
  TEST_ASSERT_FALSE(o.relayHeating);
  o = s.step(900, true, false, true, 0);
  TEST_ASSERT_FALSE(o.cool);
}

void test_settle_across_millis_wrap() {
  InterlockSequencer s;
  s.begin(20, 0xFFFFFFF0u);
  s.step(0xFFFFFFF8u, false, true, false);      // K1 moves 8 ms before the wrap
  LoadOutputs o = s.step(0x0000000Bu, false, true, false);   // 19 ms later
  TEST_ASSERT_FALSE(o.heat);
  o = s.step(0x0000000Cu, false, true, false);  // 20 ms
  TEST_ASSERT_TRUE(o.heat);
}

void test_random_wants_never_break_the_interlock() {
  // 200,000 passes of random requests, 1-7 ms apart. On every pass: never both,
  // never a load on the wrong side, never a load within 20 ms of K1 moving.
  InterlockSequencer s;
  s.begin(20, 0);
  srand(12345);
  uint32_t now = 0, movedAt = 0;
  bool relay = false, everMoved = false;   // K1 starts settled on the cooling side
  for (int i = 0; i < 200000; i++) {
    now += 1 + rand() % 7;
    const int r = rand() % 100;
    const bool c = r < 30, h = r >= 30 && r < 60, b = r >= 95;
    const int hold = (rand() % 50 == 0) ? rand() % 2 : -1;
    LoadOutputs o = s.step(now, c || b, h || b, false, hold);
    if (o.relayHeating != relay) { relay = o.relayHeating; movedAt = now; everMoved = true; }
    TEST_ASSERT_FALSE(o.cool && o.heat);
    TEST_ASSERT_FALSE(o.cool && o.relayHeating);
    TEST_ASSERT_FALSE(o.heat && !o.relayHeating);
    if ((o.cool || o.heat) && everMoved) TEST_ASSERT_GREATER_OR_EQUAL_UINT32(20, now - movedAt);
  }
}

// ---------------------------------------------------------------- relay exercise
void test_exercise_three_cycles_one_second_each_ends_cooling() {
  RelayExercise e;
  e.begin(0, hours(168), 1000, 1000, 3);
  TEST_ASSERT_TRUE(e.request(10));
  TEST_ASSERT_TRUE(e.waiting());
  TEST_ASSERT_EQUAL_INT(-1, e.relayHold());
  // Walk 1 ms at a time; record the hold pattern.
  int rises = 0;
  int prev = -1;
  uint32_t riseAt = 0, fallAt = 0;
  for (uint32_t t = 10; t < 10000; t++) {
    e.update(t, true);
    int h = e.relayHold();
    if (h == 1 && prev != 1) {
      if (rises > 0) TEST_ASSERT_EQUAL_UINT32(1000, t - fallAt);
      rises++;
      riseAt = t;
    }
    if (h != 1 && prev == 1) { TEST_ASSERT_EQUAL_UINT32(1000, t - riseAt); fallAt = t; }
    prev = h;
  }
  TEST_ASSERT_EQUAL_INT(3, rises);
  TEST_ASSERT_FALSE(e.active());
  TEST_ASSERT_EQUAL_INT(-1, e.relayHold());   // free again: K1 rests on the cooling side
  TEST_ASSERT_EQUAL_UINT32(1, e.completed());

  // Events in order
  uint8_t cyc;
  TEST_ASSERT_TRUE(e.takeEvent(&cyc) == RelayExercise::Event::Waiting);
  for (int i = 1; i <= 3; i++) {
    TEST_ASSERT_TRUE(e.takeEvent(&cyc) == RelayExercise::Event::Energised);
    TEST_ASSERT_EQUAL_UINT8(i, cyc);
    TEST_ASSERT_TRUE(e.takeEvent(&cyc) == RelayExercise::Event::Released);
  }
  TEST_ASSERT_TRUE(e.takeEvent(&cyc) == RelayExercise::Event::Done);
  TEST_ASSERT_TRUE(e.takeEvent(&cyc) == RelayExercise::Event::None);
}

void test_exercise_waits_for_loads_off() {
  RelayExercise e;
  e.begin(0, hours(168), 1000, 1000, 3);
  e.request(0);
  for (uint32_t t = 0; t < 60000; t += 10) e.update(t, false);
  TEST_ASSERT_TRUE(e.waiting());
  e.update(60000, true);
  TEST_ASSERT_TRUE(e.cycling());
  TEST_ASSERT_EQUAL_INT(1, e.relayHold());
}

void test_exercise_weekly_from_boot_then_from_last() {
  RelayExercise e;
  const uint32_t week = hours(168);
  e.begin(0, week, 1000, 1000, 3);
  e.update(week - 1, true);
  TEST_ASSERT_FALSE(e.active());
  e.update(week, true);
  TEST_ASSERT_TRUE(e.active());
  for (uint32_t t = week; t < week + 10000; t += 10) e.update(t, true);
  TEST_ASSERT_FALSE(e.active());
  // A manual one restarts the count
  e.request(week + hours(24));
  for (uint32_t t = week + hours(24); t < week + hours(24) + 10000; t += 10) e.update(t, true);
  e.update(2 * week, true);                          // would have been due: not now
  TEST_ASSERT_FALSE(e.active());
  e.update(week + hours(24) + week, true);
  TEST_ASSERT_TRUE(e.active());
}

void test_exercise_request_while_running_refused() {
  RelayExercise e;
  e.begin(0, hours(168), 1000, 1000, 3);
  TEST_ASSERT_TRUE(e.request(0));
  TEST_ASSERT_FALSE(e.request(5));
}

// ---------------------------------------------------------------- the minimum job
namespace {
CabinetSummary healthy() {
  CabinetSummary c;
  c.sensorsOk = 6; c.cabinetOk = 4;
  c.meanC = c.minC = c.maxC = 20.0f;
  c.meanRhPct = c.maxRhPct = 62.0f;
  c.chamberOk = true; c.chamberC = 20.0f;
  return c;
}
struct Loop {
  Settings s = settingsFor(Profile::Bench);
  Controller c;
  PassInputs in;
  LoadOutputs live;
  uint32_t now = 0;
  Loop() { c.begin(s, 0); in.fanOk = true; in.readingsFresh = true; in.cab = healthy(); }
  void step(const Demand& d, uint32_t dt = 5) {
    now += dt;
    in.nowMs = now;
    in.live = live;
    live = c.pass(in, d);
    TEST_ASSERT_FALSE(live.cool && live.heat);
    TEST_ASSERT_FALSE(live.cool && live.relayHeating);
    TEST_ASSERT_FALSE(live.heat && !live.relayHeating);
  }
};
}  // namespace

void test_controller_boot_hold_then_heat_with_relay_settle() {
  Loop L;
  Demand d; d.heat = true;
  while (L.now < L.s.safety.bootHoldMs - 5) { L.step(d); TEST_ASSERT_FALSE(L.live.heat || L.live.relayHeating); }
  uint32_t relayAt = 0, heatAt = 0;
  while (L.now < L.s.safety.bootHoldMs + 1000) {
    L.step(d, 1);
    if (L.live.relayHeating && !relayAt) relayAt = L.now;
    if (L.live.heat && !heatAt) heatAt = L.now;
  }
  TEST_ASSERT_EQUAL_UINT32(L.s.safety.bootHoldMs, relayAt);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(20, heatAt - relayAt);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(22, heatAt - relayAt);
}

void test_controller_exercise_with_both_loads_off_ends_cooling() {
  Loop L;
  Demand cool; cool.cool = true;
  Demand none;
  while (L.now < seconds(31)) L.step(cool);
  TEST_ASSERT_TRUE(L.live.cool);
  TEST_ASSERT_TRUE(L.c.requestExercise(L.now));
  // Compressor carries on while asked; the exercise waits
  for (int i = 0; i < 100; i++) L.step(cool);
  TEST_ASSERT_TRUE(L.live.cool);
  TEST_ASSERT_TRUE(L.c.exercise().waiting());
  // Demand ends -> compressor stops after its min on -> the exercise runs
  int relayMoves = 0;
  bool prevRelay = false;
  const uint32_t t0 = L.now;
  while (L.now - t0 < seconds(30)) {
    L.step(none, 1);
    if (L.c.exercise().cycling()) TEST_ASSERT_FALSE(L.live.cool || L.live.heat);
    if (L.live.relayHeating != prevRelay) { relayMoves++; prevRelay = L.live.relayHeating; }
  }
  TEST_ASSERT_EQUAL_INT(6, relayMoves);       // 3 x (energise + release)
  TEST_ASSERT_FALSE(L.live.relayHeating);
  TEST_ASSERT_FALSE(L.c.exercise().active());
}

void test_controller_guard_text() {
  Loop L;
  Demand d; d.heat = true; d.humid = true;
  L.step(d);
  char t[200];
  formatGuardText(t, sizeof(t), L.c);
  TEST_ASSERT_EQUAL_STRING("demand heat+humid | held: heat boot-hold 30s, humid boot-hold 30s", t);
  while (L.now < seconds(31)) L.step(d);
  L.step(Demand{});
  formatGuardText(t, sizeof(t), L.c);
  TEST_ASSERT_EQUAL_STRING("demand none | min-on: heat", t);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_rests_on_cooling_side_and_cools_at_once);
  RUN_TEST(test_heat_waits_20ms_after_k1_moves);
  RUN_TEST(test_back_to_cooling_drops_heat_first_then_waits);
  RUN_TEST(test_both_wanted_neither_asserted);
  RUN_TEST(test_relay_hold_asserts_no_temperature_load);
  RUN_TEST(test_settle_across_millis_wrap);
  RUN_TEST(test_random_wants_never_break_the_interlock);
  RUN_TEST(test_exercise_three_cycles_one_second_each_ends_cooling);
  RUN_TEST(test_exercise_waits_for_loads_off);
  RUN_TEST(test_exercise_weekly_from_boot_then_from_last);
  RUN_TEST(test_exercise_request_while_running_refused);
  RUN_TEST(test_controller_boot_hold_then_heat_with_relay_settle);
  RUN_TEST(test_controller_exercise_with_both_loads_off_ends_cooling);
  RUN_TEST(test_controller_guard_text);
  return UNITY_END();
}
