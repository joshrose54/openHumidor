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
#include "wineador/ota_update.h"
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
  TEST_ASSERT_EQUAL_STRING("Humidifying started: humidity 59.4%, below 60.0%", sentence(kEvTypeHumidify, d).c_str());
  he.type = HumidEvent::Type::CallEnd;
  he.rh = 62.0f;
  formatHumidEvent(d, sizeof(d), he, st);
  TEST_ASSERT_EQUAL_STRING("Humidifying ended: humidity 62.0%, at target", sentence(kEvTypeHumidify, d).c_str());
  ControlEvent e;
  e.type = ControlEvent::Type::StripStart;
  e.a = 68.1f; e.b = 1.6f; e.count = 1;
  formatControlEvent(d, sizeof(d), e, st);
  TEST_ASSERT_EQUAL_STRING("Drying started: humidity 68.1%, above 67.0%", sentence(kEvTypeStrip, d).c_str());
  e.type = ControlEvent::Type::Crossover;
  e.a = 1;
  formatControlEvent(d, sizeof(d), e, st);
  TEST_ASSERT_EQUAL_STRING("Changeover: heating to cooling", sentence(kEvTypeCrossover, d).c_str());
  formatLoadEvent(d, sizeof(d), 0, true, "temperature call, control 20.62 C");
  TEST_ASSERT_EQUAL_STRING("Compressor started: cooling, 20.6\xC2\xB0" "C", sentence(loadEventType(0), d).c_str());
  formatLoadEvent(d, sizeof(d), 2, true, "demanded directly (test)");
  TEST_ASSERT_EQUAL_STRING("Humidifier started: test", sentence(loadEventType(2), d).c_str());
  formatLoadEvent(d, sizeof(d), 1, false, "demand ended");
  TEST_ASSERT_EQUAL_STRING("Heater stopped", sentence(loadEventType(1), d).c_str());
  formatLoadEvent(d, sizeof(d), 2, false, guardBlockName(GuardBlock::MaxRun));
  TEST_ASSERT_EQUAL_STRING("Humidifier stopped: run limit", sentence(loadEventType(2), d).c_str());
  TEST_ASSERT_EQUAL_STRING("Power switched to heating", sentence(kEvTypeInterlock, kEvK1Heating).c_str());
  snprintf(d, sizeof(d), "%s (relay exercise)", kEvK1Cooling);
  TEST_ASSERT_EQUAL_STRING("Power switched to cooling (relay check)", sentence(kEvTypeInterlock, d).c_str());
  formatRecovered12v(d, sizeof(d), 11.0f, 5300, 10.52f);
  TEST_ASSERT_EQUAL_STRING("12V supply back after 5.3\xC2\xA0s, lowest 10.5V", sentence(kEvTypeSupply12v, d).c_str());
  formatWifiBack(d, sizeof(d), 61);
  TEST_ASSERT_EQUAL_STRING("WiFi back after 61\xC2\xA0s", sentence(kEvTypeWifi, d).c_str());
  // settings: where from, old -> new
  UserSettings a, b;
  b.targetTempC = 20.5f;
  char c[200];
  describeUserChange(c, sizeof(c), a, b);
  snprintf(d, sizeof(d), "%s (dashboard): %s", kEvSettingsChanged, c);
  TEST_ASSERT_EQUAL_STRING("Target temperature 20.0 \xE2\x86\x92 20.5\xC2\xB0" "C (dashboard)", sentence(kEvTypeSettings, d).c_str());
  // a sensor, the card, a boot, a door test, the ion setting
  snprintf(d, sizeof(d), "ch3 %s excluded: absent", kSensors[3].position);
  TEST_ASSERT_EQUAL_STRING("Middle rear excluded: not found", sentence(kEvTypeSensor, d).c_str());
  snprintf(d, sizeof(d), "%s: write failed: /logs/2026/x.csv", kEvSdFlushFailure);
  TEST_ASSERT_EQUAL_STRING("SD card failed: logs kept in memory until it's back", sentence(kEvTypeSd, d).c_str());
  snprintf(d, sizeof(d), "%s POWERON; firmware 2.0.0; board Rev Q%sREAL", kEvBootLine, kEvBootProfile);
  TEST_ASSERT_EQUAL_STRING("Started: power on or reset, firmware 2.0.0", sentence(kEvTypeBoot, d).c_str());
  snprintf(d, sizeof(d), "%s: door open 30 s", kEvDoorTest);
  TEST_ASSERT_EQUAL_STRING("Door test: door open 30 s", sentence(kEvTypeDoor, d).c_str());
  b = a;
  b.ionEnable = false;
  describeUserChange(c, sizeof(c), a, b);
  TEST_ASSERT_EQUAL_STRING("ion_enable on -> off", c);
  // the fan's run-on (harvest): the figures stay in the logged text
  e.type = ControlEvent::Type::HarvestStart;
  e.kind = HarvestKind::Cooling;
  e.a = 6.02f;
  formatControlEvent(d, sizeof(d), e, st);
  TEST_ASSERT_EQUAL_STRING("Fan run-on after cooling started", sentence(kEvTypeHarvest, d).c_str());
  e.type = ControlEvent::Type::HarvestEnd;
  e.harvestEnd = HarvestEnd::Convergence;
  e.ms = 55000;
  formatControlEvent(d, sizeof(d), e, st);
  TEST_ASSERT_EQUAL_STRING("Fan run-on ended after 55\xC2\xA0s", sentence(kEvTypeHarvest, d).c_str());
}

// The firmware's own logged formats (src/main.cpp), as the card holds them.
void test_sentences_from_the_logged_text() {
  struct C { const char* type; const char* detail; const char* want; };
  const C k[] = {
    {"sensor", "SENSOR EXCLUDED: ch3 mid_rear - divergence", "Middle rear excluded: disagrees with the others"},
    {"sensor", "SENSOR RECOVERED: ch5 ambient - 10 good reads, serial re-checked", "Room back in use"},
    {"crc", "ch3 mid_rear: CRC failure (12 since boot)", "Bad read: Middle rear"},
    {"timeout", "ch2 mid_front: read timed out (4 since boot)", "No answer: Middle front"},
    {"crc", "ch3: 5 more CRC failures / timeouts in the last hour (only the first 10 logged)",
     "Middle rear: 5 more bad reads or no answers this hour"},
    {"guard", "humidifier blocked: compressor-run", "Humidifier waiting: compressor on"},
    {"guard", "heater blocked: crossover (120 s left)", "Heater waiting: changeover"},
    {"guard", "heater block cleared (started; was crossover)", "Heater no longer waiting"},
    {"fan", "FAN STALL: tach reads zero with PWM asserted. Compressor and heater held off.",
     "Plenum fan stalled: compressor and heater off"},
    {"fan", "FAN STALL: kickstart 2 (100% for 1 s); next in 30 s if the tach still reads zero", "Plenum fan restart try 2"},
    {"fan", "FAN RECOVERED: tach reads 1060 rpm. Loads may resume.", "Plenum fan running again"},
    {"settings", "settings changed (LCD): target 20.0 -> 20.5 C; RH setpoint 62 -> 63%; saved",
     "Target temperature 20.0 \xE2\x86\x92 20.5\xC2\xB0" "C, Target humidity 62 \xE2\x86\x92 63% (cabinet screen)"},
    {"settings", "settings changed (serial: set heater): heater on -> off; saved", "Heater on \xE2\x86\x92 off (serial console)"},
    {"settings", "settings changed (dashboard): manual excludes 0x00 -> 0x08; saved", "Middle rear excluded by hand (dashboard)"},
    {"settings", "settings changed (dashboard): ch3 mid_rear manual exclude off", "Middle rear back in use (dashboard)"},
    {"settings", "LCD: settings refused (20.5 C, 62%)", "Settings from the cabinet screen not saved"},
    {"settings", "settings changed (dashboard): target 20.0 -> 20.5 C; NOT SAVED (NVS write failed) - lost at the next boot",
     "Target temperature 20.0 \xE2\x86\x92 20.5\xC2\xB0" "C (dashboard): not saved"},
    {"i2c", "I2C BUS STUCK with ch2 selected: ch2 excluded, mux reset pulsed, I2C restarted (1 since boot). RTC and other sensors carry on.",
     "Sensor bus stuck: Middle front excluded"},
    {"ntp", "NTP correction: DS3231 was +5 s out; set to 2026-11-13 14:32:05", "Clock corrected by 5\xC2\xA0s"},
    {"ntp", "NTP: DS3231 within 2 s (+1 s); not written", "Clock right to 2\xC2\xA0s"},
    {"rtc", "DS3231 EOSC was SET at boot (the clock would stop on its cell at every power cut): cleared", "Clock battery setting fixed"},
    {"strip", "RH STRIP STOP: vapour pressure reached target. RH 62.0%", "Drying ended: back to target"},
    {"exercise", "RELAY EXERCISE: cycle 2/3, K1 energised", "Relay check: cycle 2 of 3"},
    {"ineffective", "HEATER INEFFECTIVE: chamber changed +0.10 C in 900 s (needs +0.5). Heater off, flagged; tried again in 1800 s.",
     "Heater not working: chamber rose 0.1\xC2\xB0" "C in 15\xC2\xA0min"},
    {"hwdog", "HARDWARE WATCHDOG: 950 ms between toggles (the limit is 900 ms; U5 resets at 1.12 s minimum)",
     "Watchdog nearly missed: 950\xC2\xA0ms"},
    {"lcd", "LCD: Allow update pressed - an update may be sent in the next 5 minutes (5.5)", "Allow update pressed"},
    {"12v", "12V DIP: below 11.0 V for 1 s (10.90 V now)", "12V supply dipped below 11.0V"},
    {"12v", "5 more 12V dips in the last hour (only the first 10 logged)", "5 more 12V dips this hour"},
    {"wifi", "5 more WiFi outages in the last hour (only the first 10 logged)", "5 more WiFi drops this hour"},
    {"sd", "retention: free space under 10%, deleted /logs/2025/readings-2025-01.csv (12 MB)",
     "Card nearly full: oldest readings deleted (Jan 2025)"},
    {"ota", "OTA UPDATE STARTED: 2.0.0 (1413978 bytes), replacing 1.9.0; loads off", "Update started: 2.0.0 (was 1.9.0)"},
    {"ota", "OTA UPDATE INSTALLED: 2.0.0, checked; restarting", "Update installed: 2.0.0. Restarting"},
    {"ota", "OTA UPDATE ABANDONED: 2.0.0, no data for 60 s; half-written image discarded, normal control",
     "Update stopped: upload stalled. Nothing changed"},
    {"ota", "OTA UPDATE FAILED: 2.0.0, the image's own check failed (corrupt); nothing changed, normal control",
     "Update failed: file damaged. Nothing changed"},
    {"ota", "OTA upload refused: compressor_running (Compressor on. Try again when it stops.)", "Update refused: compressor on"},
    {"ota", "OTA upload refused: wrong_image bench_build (Bench build: not for the cabinet.)", "Update refused: bench build"},
    {"ota", "OTA upload refused: window_closed", "Update refused: updates closed"},   // as logged now: the code only
    {"ota", "OTA: new image running: 2.0.0, waiting to start properly (5.2: 60 s of the loop; a reset before then rolls back)",
     "Checking new firmware 2.0.0"},
    {"ota", "OTA: started properly: 2.0.0 marked good (60 s of the loop, settings check passed, watchdog toggling, first full sensor read)",
     "Update done: 2.0.0"},
    {"ota", "OTA ROLLED BACK from 2.0.0 to 1.9.0: it didn't start properly", "Update rolled back: 2.0.0 didn't start; back on 1.9.0"},
    {"ota", "5 more OTA uploads refused in the last hour (only the first 10 logged)", "5 more refused updates this hour"},
    {"boot", "BOOT: reason SOFTWARE; firmware 2.0.0; board Rev Q; profile REAL", "Started: software restart, firmware 2.0.0"},
  };
  for (const C& c : k) TEST_ASSERT_EQUAL_STRING_MESSAGE(c.want, sentence(c.type, c.detail).c_str(), c.detail);
  // the refused-update event logs the codes only, so a reworded message never changes the card
  char d[200];
  ApiResult r = refuse(Refusal::WrongImage, nullptr, "Bench build: not for the cabinet.");
  r.image = "bench_build";
  formatOtaRefused(d, sizeof(d), r);
  TEST_ASSERT_EQUAL_STRING("OTA upload refused: wrong_image bench_build", d);
  TEST_ASSERT_EQUAL_STRING("Update refused: bench build", sentence("ota", d).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_json_file_is_made_from_the_constants);
  RUN_TEST(test_names_are_unique_and_texts_sensible);
  RUN_TEST(test_the_real_writers_and_the_weekly_counts_agree);
  RUN_TEST(test_the_other_writers_use_the_words);
  RUN_TEST(test_every_event_type_has_a_sentence);
  RUN_TEST(test_sentences_from_the_real_writers);
  RUN_TEST(test_sentences_from_the_logged_text);
  return UNITY_END();
}
