// The words the pages show for the API's codes (spec 5.5 "Words on screen"), defined once in
// lib/core words.cpp and the sensor table:
//   - web/words.js (what the dashboard reads) is exactly what they make; set
//     WINEADOR_WRITE_WEB_WORDS=1 to rewrite it (tools/make_web_words.py does);
//   - every code the firmware can send has a word, so a new code can't reach a page as a code.
#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include "wineador/words.h"
#include "wineador/sensor_table.h"
#include "wineador/guards.h"
#include "wineador/sensor_health.h"
#include "wineador/ion_schedule.h"
#include "wineador/blower_rule.h"
#include "wineador/event_text.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

static const char* kJsPath = "web/words.js";

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

void test_the_page_file_is_made_from_the_tables() {
  static char b[16384];
  const int n = formatWebWordsJs(b, sizeof(b));
  TEST_ASSERT_TRUE(n > 0 && n < (int)sizeof(b));
  const std::string g(b, (size_t)n);
  const char* write = getenv("WINEADOR_WRITE_WEB_WORDS");
  if (write && write[0] == '1') {
    FILE* f = fopen(kJsPath, "wb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "can't write web/words.js (run from the project folder)");
    fwrite(g.data(), 1, g.size(), f);
    fclose(f);
  }
  const std::string onDisk = readFile(kJsPath);
  TEST_ASSERT_TRUE_MESSAGE(!onDisk.empty(), "web/words.js missing: run python3 tools/make_web_words.py");
  TEST_ASSERT_TRUE_MESSAGE(onDisk == g, "web/words.js is out of date: run python3 tools/make_web_words.py");
}

void test_every_position_has_its_name_from_the_sensor_table() {
  for (int i = 0; i < kNumSensors; i++) {
    TEST_ASSERT_TRUE(kSensors[i].name && kSensors[i].name[0] >= 'A' && kSensors[i].name[0] <= 'Z');
    TEST_ASSERT_EQUAL_STRING(kSensors[i].name, positionName(kSensors[i].position));
  }
  TEST_ASSERT_EQUAL_STRING("Middle rear", positionName("mid_rear"));
  TEST_ASSERT_EQUAL_STRING("Room", positionName("ambient"));
  TEST_ASSERT_EQUAL_STRING("elsewhere", positionName("elsewhere"));   // unknown: the code itself
}

void test_every_code_the_firmware_sends_has_a_word() {
  for (int b = 1; b <= (int)GuardBlock::OtherLoad; b++)
    TEST_ASSERT_NOT_NULL_MESSAGE(wordFor("guards", guardBlockName((GuardBlock)b)), guardBlockName((GuardBlock)b));
  for (int e = 1; e <= (int)Exclusion::Manual; e++)
    TEST_ASSERT_NOT_NULL_MESSAGE(wordFor("exclusions", exclusionName((Exclusion)e)), exclusionName((Exclusion)e));
  for (int w = 1; w <= (int)IonWhy::RecordUnsaved; w++)
    TEST_ASSERT_NOT_NULL_MESSAGE(wordFor("ion_why", ionWhyText((IonWhy)w)), ionWhyText((IonWhy)w));
  for (BlowerWhy w : {BlowerWhy::Boot, BlowerWhy::Fault, BlowerWhy::Stale})
    TEST_ASSERT_NOT_NULL_MESSAGE(wordFor("blower_why", blowerWhyText(w)), blowerWhyText(w));
  for (int i = 0; i < kEventTypeCount; i++)
    TEST_ASSERT_NOT_NULL_MESSAGE(wordFor("event_types", kEventTypes[i].type), kEventTypes[i].type);
  for (const char* r : {"POWERON", "SOFTWARE", "PANIC", "INT_WATCHDOG", "TASK_WATCHDOG", "WATCHDOG", "DEEPSLEEP",
                        "BROWNOUT", "SDIO", "EXTERNAL", "OTHER"})   // src/drivers/reset_reason.h
    TEST_ASSERT_NOT_NULL_MESSAGE(wordFor("restarts", r), r);
}

void test_the_words_themselves() {
  TEST_ASSERT_EQUAL_STRING("changeover", guardWords("crossover"));
  TEST_ASSERT_EQUAL_STRING("rest", guardWords("min-off"));
  TEST_ASSERT_EQUAL_STRING("run limit", guardWords(kEvMaxRun));
  TEST_ASSERT_EQUAL_STRING("bad reads", exclusionWords("crc"));
  TEST_ASSERT_EQUAL_STRING("Software restart", restartWords("SOFTWARE"));
  TEST_ASSERT_EQUAL_STRING("storage error", ionWhyWords("cap record can't be saved"));
  TEST_ASSERT_EQUAL_STRING("no reading yet", blowerWhyWords("no reading yet since boot"));
  TEST_ASSERT_EQUAL_STRING("something new", guardWords("something new"));   // unknown: the code itself
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_page_file_is_made_from_the_tables);
  RUN_TEST(test_every_position_has_its_name_from_the_sensor_table);
  RUN_TEST(test_every_code_the_firmware_sends_has_a_word);
  RUN_TEST(test_the_words_themselves);
  return UNITY_END();
}
