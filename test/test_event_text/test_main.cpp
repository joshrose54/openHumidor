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
#include "wineador/event_sentence.h"
#include "wineador/sensor_table.h"
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

// ---------------------------------------------------------------- plain sentences (5.5)
void test_every_event_type_has_a_sentence() {
  // a new event type can't reach the dashboard without its own sentence
  for (int i = 0; i < kEventTypeCount; i++)
    TEST_ASSERT_TRUE_MESSAGE(hasEventSentence(kEventTypes[i].type), kEventTypes[i].type);
  // and an unknown one still reads as something
  char s[200];
  eventSentence(s, sizeof(s), "future", "a new kind of event");
  TEST_ASSERT_EQUAL_STRING("future: a new kind of event", s);
}

static std::string sentence(const char* type, const char* detail) {
  char s[400];
  eventSentence(s, sizeof(s), type, detail);
  return s;
}

void test_sentences_from_the_real_writers() {
  const Settings st = settingsFor(Profile::Real);
  char d[300];
  // the spec's own example
  HumidEvent he;
  he.type = HumidEvent::Type::CallStart;
  he.rh = 59.4f;
  he.count = 3;
  formatHumidEvent(d, sizeof(d), he, st);
  TEST_ASSERT_EQUAL_STRING("Humidifier called: RH 59.4%, below 60.0%", sentence(kEvTypeHumidify, d).c_str());
  he.type = HumidEvent::Type::CallEnd;
  he.rh = 62.0f;
  formatHumidEvent(d, sizeof(d), he, st);
  TEST_ASSERT_EQUAL_STRING("Humidifier call ended: RH 62.0% reached the setpoint", sentence(kEvTypeHumidify, d).c_str());
  ControlEvent e;
  e.type = ControlEvent::Type::StripStart;
  e.a = 68.1f; e.b = 1.6f; e.count = 1;
  formatControlEvent(d, sizeof(d), e, st);
  TEST_ASSERT_EQUAL_STRING("RH strip started: RH 68.1%, above 67.0%", sentence(kEvTypeStrip, d).c_str());
  e.type = ControlEvent::Type::Crossover;
  e.a = 1;
  formatControlEvent(d, sizeof(d), e, st);
  TEST_ASSERT_EQUAL_STRING("Crossover: cooling started after heating", sentence(kEvTypeCrossover, d).c_str());
  formatLoadEvent(d, sizeof(d), 0, true, "temperature call");
  TEST_ASSERT_EQUAL_STRING("Compressor started: temperature call", sentence(loadEventType(0), d).c_str());
  formatLoadEvent(d, sizeof(d), 2, false, guardBlockName(GuardBlock::MaxRun));
  TEST_ASSERT_EQUAL_STRING("Humidifier stopped: max-run", sentence(loadEventType(2), d).c_str());
  TEST_ASSERT_EQUAL_STRING("Interlock moved to the heating side", sentence(kEvTypeInterlock, kEvK1Heating).c_str());
  formatRecovered12v(d, sizeof(d), 11.0f, 5300, 10.52f);
  TEST_ASSERT_EQUAL_STRING("12\xC2\xA0V supply back after 5.3\xC2\xA0s; lowest 10.52\xC2\xA0V", sentence(kEvTypeSupply12v, d).c_str());
  formatWifiBack(d, sizeof(d), 61);
  TEST_ASSERT_EQUAL_STRING("WiFi back after 61\xC2\xA0s", sentence(kEvTypeWifi, d).c_str());
  // settings: where from, old -> new
  UserSettings a, b;
  b.targetTempC = 20.5f;
  char c[200];
  describeUserChange(c, sizeof(c), a, b);
  snprintf(d, sizeof(d), "%s (dashboard): %s", kEvSettingsChanged, c);
  TEST_ASSERT_EQUAL_STRING("Settings changed from dashboard: target_temp_c 20.0 \xE2\x86\x92 20.5", sentence(kEvTypeSettings, d).c_str());
  // a sensor, the card, a boot, a door test, the ion setting
  snprintf(d, sizeof(d), "ch3 %s excluded: absent", kSensors[3].position);
  TEST_ASSERT_EQUAL_STRING("Sensor ch3 (mid_rear) left out: absent", sentence(kEvTypeSensor, d).c_str());
  snprintf(d, sizeof(d), "%s: write failed: /logs/2026/x.csv", kEvSdFlushFailure);
  TEST_ASSERT_EQUAL_STRING("SD card failed: the logs are kept in memory until it is back", sentence(kEvTypeSd, d).c_str());
  snprintf(d, sizeof(d), "%s POWERON; firmware 2.0.0; board Rev Q%sREAL", kEvBootLine, kEvBootProfile);
  TEST_ASSERT_EQUAL_STRING("Board started (POWERON), firmware 2.0.0", sentence(kEvTypeBoot, d).c_str());
  snprintf(d, sizeof(d), "%s: door open 30 s", kEvDoorTest);
  TEST_ASSERT_EQUAL_STRING("Door test: door open 30 s", sentence(kEvTypeDoor, d).c_str());
  b = a;
  b.ionEnable = false;
  describeUserChange(c, sizeof(c), a, b);
  TEST_ASSERT_EQUAL_STRING("ion_enable on -> off", c);
  // harvest, with °C written properly
  e.type = ControlEvent::Type::HarvestStart;
  e.kind = HarvestKind::Cooling;
  e.a = 6.02f;
  formatControlEvent(d, sizeof(d), e, st);
  TEST_ASSERT_TRUE(sentence(kEvTypeHarvest, d).find("Harvest after cooling started: chamber 6.02\xC2\xA0\xC2\xB0" "C from the cabinet mean") == 0);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_json_file_is_made_from_the_constants);
  RUN_TEST(test_names_are_unique_and_texts_sensible);
  RUN_TEST(test_the_real_writers_and_the_weekly_counts_agree);
  RUN_TEST(test_the_other_writers_use_the_words);
  RUN_TEST(test_every_event_type_has_a_sentence);
  RUN_TEST(test_sentences_from_the_real_writers);
  return UNITY_END();
}
