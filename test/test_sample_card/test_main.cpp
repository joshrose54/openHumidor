// Writes two small SAMPLE SD cards on the Mac with the firmware's own log code (logbook,
// hourly), so tools/check_logs.py can be proved on schema 2 before any board can write one:
//   .pio.nosync/sample-cards/revq/logs/   Rev Q, real profile, cabinet build
//   .pio.nosync/sample-cards/revn/logs/   Rev N, bench profile, test build (*_real columns)
// Then, from the project folder:
//   python3 tools/check_logs.py .pio.nosync/sample-cards/revq --profile real
//   python3 tools/check_logs.py .pio.nosync/sample-cards/revn --profile bench
// The values are made up; the file names, headers, columns and RH sums are the firmware's.
#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "wineador/logbook.h"
#include "wineador/hourly.h"
#include "wineador/sensor_table.h"
#include "wineador/sensor_health.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

static void mkdirs(const char* path) {   // mkdir -p
  char p[256];
  snprintf(p, sizeof(p), "%s", path);
  for (char* c = p + 1; *c; c++)
    if (*c == '/') { *c = 0; mkdir(p, 0755); *c = '/'; }
  mkdir(p, 0755);
}

static bool writeFile(const char* path, const char* header, const char* body) {
  FILE* f = fopen(path, "w");
  if (!f) return false;
  fputs(header, f);
  fputs(body, f);
  fclose(f);
  return true;
}

static void makeCard(const char* root, const char* board, Profile profile, bool testBuild, bool revQ) {
  char logs[200], dir[220], path[300];
  snprintf(logs, sizeof(logs), "%s/logs", root);
  snprintf(dir, sizeof(dir), "%s/2026", logs);
  mkdirs(dir);

  LogHeaderInfo info;
  info.firmware = testBuild ? "0.1.0-stage6-BENCH-TEST (sample card)" : "0.1.0-stage6 (sample card)";
  info.board = board;
  info.profile = profile;
  info.testBuild = testBuild;

  // ---- readings: 20 rows a minute apart, one sensor failing a read part way
  static char hdr[3000], body[40000], row[1500];
  formatReadingsHeader(hdr, sizeof(hdr), info);
  body[0] = 0;
  for (int k = 0; k < 20; k++) {
    char ts[32];
    snprintf(ts, sizeof(ts), "2026-11-13T10:%02d:00Z", k);
    ReadingsRow r;
    r.timestamp = ts;
    r.timeTrusted = true;
    for (int i = 0; i < kNumSensors; i++) {
      ChannelLog& c = r.ch[i];
      c.read = !(i == 1 && k == 7);
      c.flag = c.read ? "ok" : "crc";
      c.tempC = 19.8f + 0.1f * i;
      c.rhRawPct = 64.0f + 0.2f * i + 0.05f * k;
      c.rhPct = correctedRh(i, c.rhRawPct);
      r.real[i] = c;
    }
    r.cab.cabinetOk = 4; r.cab.meanC = 19.95f; r.cab.minC = 19.8f; r.cab.maxC = 20.1f;
    r.cab.meanRhPct = 62.0f; r.cab.controlC = 19.95f;
    r.fanPct = 30; r.fanRpm = 700; r.loopMaxUs = 4100; r.heapFree = 90000; r.heapMin = 85000; r.sdWorstMs = 30;
    if (revQ) {   // Rev Q's columns (5.4); empty on Rev N, which has no 12V sense or PSRAM
      r.hasV12 = true; r.v12MeanV = 12.03f - 0.01f * (k % 3); r.v12Min1sV = (k == 12) ? 10.62f : 11.96f;
      r.hasPsram = true; r.psramMin = 1081344 - 64 * k;
    }
    formatReadingsRow(row, sizeof(row), r, testBuild);
    strncat(body, row, sizeof(body) - strlen(body) - 1);
  }
  logPath(path, sizeof(path), LogKind::Readings, 2026, 11);
  char full[400];
  snprintf(full, sizeof(full), "%s%s", root, path);
  TEST_ASSERT_TRUE(writeFile(full, hdr, body));

  // ---- events
  formatEventsHeader(hdr, sizeof(hdr), info);
  body[0] = 0;
  char detail[300];
  snprintf(detail, sizeof(detail), "BOOT: reason POWERON; firmware %s; board %s; profile %s%s; sample card",
           info.firmware, board, profile == Profile::Bench ? "BENCH" : "REAL", testBuild ? " TEST-BUILD" : "");
  formatEventRow(row, sizeof(row), "2026-11-13T09:59:58Z", true, 1200, "boot", detail);
  strncat(body, row, sizeof(body) - strlen(body) - 1);
  formatEventRow(row, sizeof(row), "2026-11-13T10:07:00Z", true, 421000, "crc",
                 "ch1 bottom_front: CRC failure (1 since boot)");
  strncat(body, row, sizeof(body) - strlen(body) - 1);
  if (revQ) {
    formatEventRow(row, sizeof(row), "2026-11-13T10:12:03Z", true, 725000, "12v", "12V DIP: below 11.0 V for 1 s (10.71 V now)");
    strncat(body, row, sizeof(body) - strlen(body) - 1);
    formatEventRow(row, sizeof(row), "2026-11-13T10:12:06Z", true, 728000, "12v",
                   "12V recovered: back above 11.0 V after 3200 ms; lowest 10.55 V");
    strncat(body, row, sizeof(body) - strlen(body) - 1);
    formatEventRow(row, sizeof(row), "2026-11-13T10:15:00Z", true, 902000, "wifi", "WiFi lost");
    strncat(body, row, sizeof(body) - strlen(body) - 1);
    formatEventRow(row, sizeof(row), "2026-11-13T10:15:42Z", true, 944000, "wifi", "WiFi back after 42 s");
    strncat(body, row, sizeof(body) - strlen(body) - 1);
  }
  logPath(path, sizeof(path), LogKind::Events, 2026, 11);
  snprintf(full, sizeof(full), "%s%s", root, path);
  TEST_ASSERT_TRUE(writeFile(full, hdr, body));

  // ---- hourly: two hours
  formatHourlyHeader(hdr, sizeof(hdr), info);
  body[0] = 0;
  for (int hr = 10; hr <= 11; hr++) {
    char ts[32];
    snprintf(ts, sizeof(ts), "2026-11-13T%02d:00:00Z", hr);
    HourlyStats h;
    h.begin((uint32_t)hr, ts, true);
    bool ok[kNumSensors] = {true, true, true, true, true, true};
    float t[kNumSensors] = {19.8f, 19.9f, 20.0f, 20.1f, 20.2f, 21.0f};
    float rh[kNumSensors] = {62, 62, 62, 62, 62, 48};
    CabinetSummary c;
    c.cabinetOk = 4; c.meanC = 19.95f; c.meanRhPct = 62; c.controlC = 19.95f;
    h.addSweep(ok, t, rh, c, 30, 700);
    h.noteHeapMin(85000);
    if (revQ) { h.noteSupply(12.02f, hr == 10 ? 10.62f : 11.95f); h.notePsramMin(1080000); }
    h.formatRow(row, sizeof(row));
    strncat(body, row, sizeof(body) - strlen(body) - 1);
  }
  logPath(path, sizeof(path), LogKind::Hourly, 0, 0);
  snprintf(full, sizeof(full), "%s%s", root, path);
  TEST_ASSERT_TRUE(writeFile(full, hdr, body));
}

void test_write_revq_sample_card() {
  makeCard(".pio.nosync/sample-cards/revq", "rev-q (channel n = mux n)", Profile::Real, false, true);
  struct stat st;
  TEST_ASSERT_EQUAL_INT(0, stat(".pio.nosync/sample-cards/revq/logs/hourly-s2.csv", &st));
  TEST_ASSERT_EQUAL_INT(0, stat(".pio.nosync/sample-cards/revq/logs/2026/2026-11-readings-s2.csv", &st));
}

void test_write_revn_sample_card() {
  makeCard(".pio.nosync/sample-cards/revn", "rev-n (channel n = mux n+2)", Profile::Bench, true, false);
  struct stat st;
  TEST_ASSERT_EQUAL_INT(0, stat(".pio.nosync/sample-cards/revn/logs/2026/2026-11-events-s2.csv", &st));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_write_revq_sample_card);
  RUN_TEST(test_write_revn_sample_card);
  return UNITY_END();
}
