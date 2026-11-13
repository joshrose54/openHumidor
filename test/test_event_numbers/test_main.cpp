// The numbers tools/check_logs.py reads out of events (spec 5.4): for each event it reads numbers
// from, a real example is written with the firmware's own formatter (lib/core event_text.cpp),
// then check_logs.py's own parser reads it (python3 tools/check_logs.py --parse-selftest), and the
// numbers must come back the same. A wording change the patterns no longer read fails here.
// Needs python3 on the Mac (as the other tools do). Run from the project folder (pio test does).
#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <algorithm>
#include "wineador/event_text.h"
#include "wineador/json_out.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

struct Example {
  const char* name;      // the event text's name in event_text.h
  std::string detail;    // as the firmware writes it
  double numbers[4];
  int n;
};

static std::string jsonOf(const Example* ex, int count) {
  std::string s = "[\n";
  char q[600];
  for (int i = 0; i < count; i++) {
    JsonWriter w;
    w.setBuffer(q, sizeof(q) - 1);
    w.beginObject();
    w.kStr("name", ex[i].name);
    w.kStr("detail", ex[i].detail.c_str());
    w.key("numbers");
    w.beginArray();
    for (int k = 0; k < ex[i].n; k++) w.num(ex[i].numbers[k], 3);
    w.endArray();
    w.endObject();
    q[w.length()] = 0;
    s += "  ";
    s += q;
    s += i + 1 < count ? ",\n" : "\n";
  }
  return s + "]\n";
}

// Writes the examples and runs check_logs.py's parsers on them. Returns its exit code; its
// output goes into `out`.
static int runSelftest(const Example* ex, int count, const char* file, std::string* out) {
  mkdir(".pio.nosync", 0755);
  FILE* f = fopen(file, "wb");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, file);
  const std::string j = jsonOf(ex, count);
  fwrite(j.data(), 1, j.size(), f);
  fclose(f);
  char cmd[300];
  snprintf(cmd, sizeof(cmd), "python3 tools/check_logs.py %s --parse-selftest 2>&1", file);
  FILE* p = popen(cmd, "r");
  TEST_ASSERT_NOT_NULL_MESSAGE(p, "can't run python3");
  char b[512];
  while (fgets(b, sizeof(b), p)) *out += b;
  const int status = pclose(p);
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void examples(Example* ex) {
  char d[320];
  formatRecovered12v(d, sizeof(d), 11.0f, 5300, 10.52f);
  ex[0] = Example{"Recovered12v", d, {5300, 10.52}, 2};
  formatWifiBack(d, sizeof(d), 61);
  ex[1] = Example{"WifiBack", d, {61}, 1};
  SdStressFigures f;
  f.khz = 1000; f.retry = true; f.writes = 19998; f.target = 20000; f.failures = 2; f.retries = 31;
  f.atData = 31; f.worstMs = 412;
  formatSdStressLine(d, sizeof(d), true, f);
  ex[2] = Example{"SdStressDone", d, {19998, 20000, 2, 31}, 4};
  formatSdStressChecks(d, sizeof(d), 1, 61234, 30000, 41720, 12);
  ex[3] = Example{"SdStressChecks", d, {1, 41720}, 2};
}

void test_check_logs_reads_back_what_the_firmware_writes() {
  Example ex[4];
  examples(ex);
  std::string out;
  const int code = runSelftest(ex, 4, ".pio.nosync/event-numbers.json", &out);
  printf("%s", out.c_str());
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, code, out.c_str());
  TEST_ASSERT_EQUAL_INT(4, (int)std::count(out.begin(), out.end(), '\n'));
  TEST_ASSERT_TRUE(out.find("FAIL") == std::string::npos);
}

void test_the_selftest_notices_a_wrong_number() {
  // proves the round trip really compares: one number changed must fail
  Example ex[4];
  examples(ex);
  ex[2].numbers[2] = 0;   // the failures count, written as 2
  std::string out;
  const int code = runSelftest(ex, 4, ".pio.nosync/event-numbers-wrong.json", &out);
  TEST_ASSERT_NOT_EQUAL(0, code);
  TEST_ASSERT_TRUE(out.find("FAIL  SdStressDone") != std::string::npos);
}

void test_the_selftest_notices_wording_it_cant_read() {
  // a detail in different words (as if the formatter changed) must fail, not be skipped
  Example ex[1];
  ex[0] = Example{"WifiBack", std::string(kEvWifiBack) + " one minute", {60}, 1};
  std::string out;
  const int code = runSelftest(ex, 1, ".pio.nosync/event-numbers-words.json", &out);
  TEST_ASSERT_NOT_EQUAL(0, code);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_check_logs_reads_back_what_the_firmware_writes);
  RUN_TEST(test_the_selftest_notices_a_wrong_number);
  RUN_TEST(test_the_selftest_notices_wording_it_cant_read);
  return UNITY_END();
}
