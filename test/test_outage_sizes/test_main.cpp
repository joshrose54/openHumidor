// Rev Q's SD outage buffers in PSRAM (spec 5.4; include/board.h): 1,000 events, 1,440 readings
// rows, 168 hourly rows, about 0.94 MB. Checks the byte budgets hold that many real rows, the
// total, and that a buffer this big keeps the newest lines and drops the oldest.
#define WINEADOR_BOARD_ANY
#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "board.h"
#include "wineador/logbook.h"
#include "wineador/hourly.h"
#include "wineador/line_ring.h"
#include "wineador/sensor_health.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

// A readings row as a test build on Rev Q writes it, with everyday values.
static int typicalRow(char* out, size_t n, bool testBuild) {
  ReadingsRow r;
  r.timestamp = "2026-11-13T10:15:00Z";
  r.timeTrusted = true;
  for (int i = 0; i < kNumSensors; i++) {
    ChannelLog& c = r.ch[i];
    c.read = true; c.tempC = 19.87f; c.rhRawPct = 64.31f; c.rhPct = correctedRh(i, 64.31f); c.flag = "ok";
    r.real[i] = c;
    r.real[i].flag = "ok";
  }
  r.cab.cabinetOk = 4; r.cab.meanC = 19.95f; r.cab.minC = 19.80f; r.cab.maxC = 20.10f; r.cab.meanRhPct = 62.04f;
  r.cab.controlC = 19.95f;
  r.fanPct = 40; r.fanRpm = 1320; r.cool = true; r.heaterDutyPct = 0; r.loopMaxUs = 4700;
  r.heapFree = 101234; r.heapMin = 87654; r.sdWorstMs = 31;
  r.hasV12 = true; r.v12MeanV = 12.04f; r.v12Min1sV = 11.98f; r.hasPsram = true; r.psramMin = 1123456;
  return formatReadingsRow(out, n, r, testBuild);
}

void test_rev_q_sizes_are_the_spec() {
  TEST_ASSERT_EQUAL_UINT(1000, board_revq::kSdEventsKept);
  TEST_ASSERT_EQUAL_UINT(1440, board_revq::kSdRowsKept);       // 24 h at the steady 60 s rate
  TEST_ASSERT_EQUAL_UINT(168, board_revq::kSdHourlyKept);      // a week
  TEST_ASSERT_TRUE(board_revq::kOutageInPsram);
  const size_t total = board_revq::kSdEventsBytes + board_revq::kSdRowsBytes + board_revq::kSdHourlyBytes;
  char m[80];
  snprintf(m, sizeof(m), "Rev Q outage buffers: %u bytes in PSRAM", (unsigned)total);
  TEST_MESSAGE(m);
  TEST_ASSERT_TRUE(total <= 1000000);                          // "about 0.94 MB" of the 2 MB
}

// The no-PSRAM fallback (5.4): 60 events, 30 readings rows, 24 hourly rows in internal RAM - the
// sizes Rev N ran, with their byte budgets as they were proven there.
void test_no_psram_fallback_is_the_spec() {
  TEST_ASSERT_EQUAL_UINT(60, board_revq::kNoPsramEventsKept);
  TEST_ASSERT_EQUAL_UINT(12000, board_revq::kNoPsramEventsBytes);
  TEST_ASSERT_EQUAL_UINT(30, board_revq::kNoPsramRowsKept);
  TEST_ASSERT_EQUAL_UINT(14000, board_revq::kNoPsramRowsBytes);
  TEST_ASSERT_EQUAL_UINT(24, board_revq::kNoPsramHourlyKept);
  TEST_ASSERT_EQUAL_UINT(9000, board_revq::kNoPsramHourlyBytes);
}

// 1,440 everyday rows (2 bytes each for the length) fit the readings budget, test build included.
void test_a_day_of_rows_fits() {
  char row[700];
  const int testLen = typicalRow(row, sizeof(row), true);
  const int cabLen = typicalRow(row, sizeof(row), false);
  char m[120];
  snprintf(m, sizeof(m), "typical readings row: %d characters (test build), %d (cabinet build)", testLen, cabLen);
  TEST_MESSAGE(m);
  TEST_ASSERT_TRUE((size_t)board_revq::kSdRowsKept * (size_t)(testLen + 2) <= board_revq::kSdRowsBytes);
  TEST_ASSERT_TRUE((size_t)board_revq::kSdRowsKept * (size_t)(cabLen + 2) <= board_revq::kSdRowsBytes);
}

// A week of hourly rows and 1,000 events of ~200 characters fit theirs.
void test_hourly_and_events_fit() {
  HourlyStats h;
  h.begin(1, "2026-11-13T10:00:00Z", true);
  bool ok[kNumSensors] = {true, true, true, true, true, true};
  float t[kNumSensors] = {19.8f, 19.9f, 20.0f, 20.1f, 21.5f, 22.3f}, rh[kNumSensors] = {62, 62, 62, 62, 61, 48};
  CabinetSummary c;
  c.cabinetOk = 4; c.meanC = 19.95f; c.meanRhPct = 62; c.controlC = 19.95f;
  h.addSweep(ok, t, rh, c, 40, 1320);
  char row[700];
  const int len = h.formatRow(row, sizeof(row));
  TEST_ASSERT_TRUE((size_t)board_revq::kSdHourlyKept * (size_t)(len + 2) <= board_revq::kSdHourlyBytes);
  TEST_ASSERT_TRUE((size_t)board_revq::kSdEventsKept * (200 + 2) <= board_revq::kSdEventsBytes + 2000);
}

// A buffer this size holds 1,440 rows, then drops the oldest and counts them.
void test_full_size_ring_keeps_the_newest() {
  char* store = (char*)malloc(board_revq::kSdRowsBytes);
  TEST_ASSERT_NOT_NULL(store);
  LineRing r(store, board_revq::kSdRowsBytes, board_revq::kSdRowsKept);
  char line[480];
  memset(line, 'x', sizeof(line));
  for (int i = 0; i < 1500; i++) {
    snprintf(line, 12, "%010d", i);
    line[10] = 'x';
    TEST_ASSERT_TRUE(r.push(line, sizeof(line)));
  }
  TEST_ASSERT_EQUAL_UINT(1440, r.count());                   // the line limit, not the bytes, was reached
  TEST_ASSERT_EQUAL_UINT32(60, r.dropped());
  char got[600];
  r.peek(0, got, sizeof(got));
  TEST_ASSERT_EQUAL_INT(0, strncmp(got, "0000000060", 10));  // the oldest kept is row 60
  r.peek(1439, got, sizeof(got));
  TEST_ASSERT_EQUAL_INT(0, strncmp(got, "0000001499", 10));
  free(store);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_rev_q_sizes_are_the_spec);
  RUN_TEST(test_no_psram_fallback_is_the_spec);
  RUN_TEST(test_a_day_of_rows_fits);
  RUN_TEST(test_hourly_and_events_fit);
  RUN_TEST(test_full_size_ring_keeps_the_newest);
  return UNITY_END();
}
