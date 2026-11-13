// The wording of the events (spec 5.4), defined once in lib/core event_text.h:
//   - tools/event_text.json (what tools/check_logs.py reads) is exactly what the constants make;
//     set WINEADOR_WRITE_EVENT_TEXT=1 to rewrite it (tools/make_event_text.py does);
//   - the real event-writing code writes those words, and the weekly counts count what it wrote.
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string>
#include "wineador/event_text.h"
#include "wineador/temp_control.h"
#include "wineador/humid_control.h"
#include "wineador/guards.h"
#include "wineador/web_api.h"
#include "wineador/logbook.h"
#include "wineador/log_read.h"
#include "../json_check.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

static const char* kJsonPath = "tools/event_text.json";

static std::string generated() {
  static char b[16384];
  const int n = formatEventTextJson(b, sizeof(b));
  TEST_ASSERT_TRUE(n > 0);
  return std::string(b, (size_t)n);
}

static std::string readFile(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return "";
  std::string s;
  char b[4096];
  size_t k;
  while ((k = fread(b, 1, sizeof(b), f)) > 0) s.append(b, k);
  fclose(f);
  return s;
}

void test_the_json_file_is_made_from_the_constants() {
  const std::string g = generated();
  TEST_ASSERT_TRUE(jsoncheck::valid(g));
  const char* write = getenv("WINEADOR_WRITE_EVENT_TEXT");
  if (write && write[0] == '1') {
    FILE* f = fopen(kJsonPath, "wb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "can't write tools/event_text.json (run from the project folder)");
    fwrite(g.data(), 1, g.size(), f);
    fclose(f);
  }
  const std::string onDisk = readFile(kJsonPath);
  TEST_ASSERT_TRUE_MESSAGE(!onDisk.empty(), "tools/event_text.json missing: run python3 tools/make_event_text.py");
  TEST_ASSERT_TRUE_MESSAGE(onDisk == g, "tools/event_text.json is out of date: run python3 tools/make_event_text.py");
}

void test_names_are_unique_and_texts_sensible() {
  for (int i = 0; i < kEventTextCount; i++) {
    TEST_ASSERT_TRUE(strlen(kEventTexts[i].text) > 0);
    for (int j = 0; j < i; j++) TEST_ASSERT_TRUE(strcmp(kEventTexts[i].name, kEventTexts[j].name) != 0);
  }
  for (int i = 0; i < kEventTypeCount; i++)
    for (int j = 0; j < i; j++) {
      TEST_ASSERT_TRUE(strcmp(kEventTypes[i].name, kEventTypes[j].name) != 0);
      TEST_ASSERT_TRUE(strcmp(kEventTypes[i].type, kEventTypes[j].type) != 0);
    }
}

static const EventText& textNamed(const char* name) {
  for (int i = 0; i < kEventTextCount; i++)
    if (strcmp(kEventTexts[i].name, name) == 0) return kEventTexts[i];
  TEST_FAIL_MESSAGE(name);
  return kEventTexts[0];
}

// Writes an event row as the firmware does (logbook), reads it back as the dashboard does
// (log_read), and counts it.
static WeekKind roundTrip(const char* type, const char* detail, WeekCounts* w, int64_t t) {
  char row[600];
  const int k = formatEventRow(row, sizeof(row), "2026-11-13T12:00:00Z", true, 1, type, detail);
  EventRow e;
  TEST_ASSERT_TRUE(parseEventRow(row, (size_t)k, &e));
  const WeekKind kind = weekKindOf(e.type, e.detail);
  w->add(t, kind);
  return kind;
}

void test_the_real_writers_and_the_weekly_counts_agree() {
  const Settings s = settingsFor(Profile::Real);
  char d[300];
  WeekCounts w;
  const int64_t t = 1794571200;   // 2026-11-13T12:00:00Z
  // RH strip start and stop: written by the temperature control's own formatter
  ControlEvent e;
  e.type = ControlEvent::Type::StripStart;
  e.count = 1; e.a = 68.0f; e.b = 1.62f;
  formatControlEvent(d, sizeof(d), e, s);
  TEST_ASSERT_TRUE(eventIs(kEvTypeStrip, d, textNamed("StripStart")));
  TEST_ASSERT_TRUE(roundTrip(kEvTypeStrip, d, &w, t) == WeekKind::Strip);
  e.type = ControlEvent::Type::StripStop;
  formatControlEvent(d, sizeof(d), e, s);
  TEST_ASSERT_EQUAL_INT(0, strncmp(d, kEvStripStop, strlen(kEvStripStop)));
  TEST_ASSERT_TRUE(roundTrip(kEvTypeStrip, d, &w, t) == WeekKind::None);
  // crossovers, twice
  e.type = ControlEvent::Type::Crossover;
  e.a = 1;
  formatControlEvent(d, sizeof(d), e, s);
  TEST_ASSERT_TRUE(roundTrip(kEvTypeCrossover, d, &w, t) == WeekKind::Crossover);
  e.a = 0;
  formatControlEvent(d, sizeof(d), e, s);
  TEST_ASSERT_TRUE(roundTrip(kEvTypeCrossover, d, &w, t) == WeekKind::Crossover);
  // humidifier runs: written by the load-event formatter main.cpp uses; OFF is not a run
  formatLoadEvent(d, sizeof(d), 2, true, "humidify call, mean RH 59.5%");
  TEST_ASSERT_EQUAL_STRING(kEvTypeHumidifier, loadEventType(2));
  TEST_ASSERT_TRUE(roundTrip(loadEventType(2), d, &w, t) == WeekKind::HumidRun);
  formatLoadEvent(d, sizeof(d), 2, false, "demand ended");
  TEST_ASSERT_TRUE(roundTrip(loadEventType(2), d, &w, t) == WeekKind::None);
  // a compressor ON is no humidifier run, even though it says ON
  formatLoadEvent(d, sizeof(d), 0, true, "temperature call");
  TEST_ASSERT_TRUE(roundTrip(loadEventType(0), d, &w, t) == WeekKind::None);
  uint32_t x, st, h, dist;
  w.counts(t, &x, &st, &h, &dist);
  TEST_ASSERT_EQUAL_UINT32(2, x);
  TEST_ASSERT_EQUAL_UINT32(1, st);
  TEST_ASSERT_EQUAL_UINT32(1, h);
  TEST_ASSERT_EQUAL_UINT32(0, dist);
}

void test_the_other_writers_use_the_words() {
  const Settings s = settingsFor(Profile::Real);
  char d[300];
  // humidify calls (check_logs counts them)
  HumidEvent he;
  he.type = HumidEvent::Type::CallStart;
  he.rh = 59.0f;
  formatHumidEvent(d, sizeof(d), he, s);
  TEST_ASSERT_EQUAL_INT(0, strncmp(d, kEvHumidifyStart, strlen(kEvHumidifyStart)));
  he.type = HumidEvent::Type::CallEnd;
  formatHumidEvent(d, sizeof(d), he, s);
  TEST_ASSERT_EQUAL_INT(0, strncmp(d, kEvHumidifyEnd, strlen(kEvHumidifyEnd)));
  // a max-run stop: the guard's own name, inside "compressor OFF (max-run)" (check_logs' guard audit)
  TEST_ASSERT_EQUAL_STRING(kEvMaxRun, guardBlockName(GuardBlock::MaxRun));
  formatLoadEvent(d, sizeof(d), 0, false, guardBlockName(GuardBlock::MaxRun));
  TEST_ASSERT_EQUAL_STRING("compressor OFF (max-run)", d);
  // the load events start with ON / OFF for each load
  for (int L = 0; L < 3; L++) {
    formatLoadEvent(d, sizeof(d), L, true, "x");
    char want[40];
    snprintf(want, sizeof(want), "%s ON", loadEventType(L));
    TEST_ASSERT_EQUAL_INT(0, strncmp(d, want, strlen(want)));
  }
  // a dashboard settings change says old -> new
  UserSettings a, b;
  b.targetTempC = 20.5f;
  describeUserChange(d, sizeof(d), a, b);
  TEST_ASSERT_NOT_NULL(strstr(d, kEvChangeArrow));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_json_file_is_made_from_the_constants);
  RUN_TEST(test_names_are_unique_and_texts_sensible);
  RUN_TEST(test_the_real_writers_and_the_weekly_counts_agree);
  RUN_TEST(test_the_other_writers_use_the_words);
  return UNITY_END();
}
