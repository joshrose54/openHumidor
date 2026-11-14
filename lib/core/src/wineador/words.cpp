#include "wineador/words.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "wineador/sensor_table.h"

namespace wineador {

namespace {

// A load held by a rule (guards.cpp guardBlockName): "Waiting · rest 3:12".
const CodeWord kGuards[] = {
  {"settings-problem", "settings error"}, {"updating", "updating"}, {"boot-hold", "starting"},
  {"relay-exercise", "relay check"}, {"disabled", "off in Settings"}, {"sensors", "too few sensors"},
  {"fan", "fan stalled"}, {"cabinet-ceiling", "cabinet too warm"}, {"chamber-ceiling", "chamber too warm"},
  {"rh-ceiling", "humidity limit"}, {"heater-ineffective", "retry"}, {"humid-ineffective", "retry"},
  {"stripping", "drying"}, {"compressor-run", "compressor on"}, {"max-run", "run limit"}, {"min-off", "rest"},
  {"crossover", "changeover"}, {"other-load", "other load on"},
};

// Why a sensor is excluded (sensor_health.cpp exclusionName): "Excluded: bad reads".
const CodeWord kExclusions[] = {
  {"not-read", "not read yet"}, {"crc", "bad reads"}, {"timeout", "no answer"}, {"absent", "not found"},
  {"implausible", "impossible reading"}, {"divergence", "disagrees with the others"},
  {"bus-fault", "sensor bus fault"}, {"wrong-serial", "wrong sensor"}, {"manual", "by hand"},
};

// The last restart (src/drivers/reset_reason.h). The S3 can't tell power on from the EN reset.
const CodeWord kRestarts[] = {
  {"POWERON", "Power on or reset"}, {"SOFTWARE", "Software restart"}, {"PANIC", "Crash"},
  {"INT_WATCHDOG", "Watchdog"}, {"TASK_WATCHDOG", "Watchdog"}, {"WATCHDOG", "Watchdog"},
  {"DEEPSLEEP", "Other"}, {"BROWNOUT", "Power dip"}, {"SDIO", "Other"}, {"EXTERNAL", "Reset"}, {"OTHER", "Other"},
};

// The ion generator's why (ion_schedule.cpp ionWhyText): "Waiting · storage error".
const CodeWord kIonWhy[] = {
  {"boot hold", "starting"}, {"update", "updating"}, {"fault", "fault"},
  {"at most 10 minutes in 24 hours", "10-minute limit"}, {"switched off", "switched off"},
  {"cap record can't be saved", "storage error"},
};

// The blowers' why (blower_rule.cpp blowerWhyText): "Full speed · no fresh reading".
const CodeWord kBlowerWhy[] = {
  {"no reading yet since boot", "no reading yet"}, {"fault", "fault"}, {"no fresh reading", "no fresh reading"},
};

// The Events page's filter: a name for each event type (event_text.h).
const CodeWord kTypeNames[] = {
  {"compressor", "Compressor"}, {"heater", "Heater"}, {"humidifier", "Humidifier"}, {"interlock", "Power to"},
  {"harvest", "Fan run-on"}, {"guard", "Waiting"}, {"crc", "Bad read"}, {"timeout", "No answer"},
  {"sensor", "Sensor"}, {"fan", "Plenum fan"}, {"settings", "Settings"}, {"boot", "Restart"}, {"sd", "SD card"},
  {"i2c", "Sensor bus"}, {"rtc", "Clock"}, {"ntp", "Clock"}, {"crossover", "Changeover"}, {"strip", "Drying"},
  {"humidify", "Humidifying"}, {"exercise", "Relay check"}, {"ineffective", "Not working"},
  {"disturbance", "Disturbance"}, {"heap", "Memory"}, {"psram", "Memory"}, {"hwdog", "Watchdog"},
  {"lcd", "Cabinet screen"}, {"12v", "12V supply"}, {"wifi", "WiFi"}, {"door", "Door test"}, {"ota", "Update"},
  {"blowers", "Blowers"}, {"ion", "Ion generator"}, {"scenario", "Test"},
};

#define WINEADOR_LIST(n, a) {n, a, (int)(sizeof(a) / sizeof(a[0]))}

}  // namespace

const WordList kWordLists[] = {
  WINEADOR_LIST("guards", kGuards), WINEADOR_LIST("exclusions", kExclusions),
  WINEADOR_LIST("restarts", kRestarts), WINEADOR_LIST("ion_why", kIonWhy),
  WINEADOR_LIST("blower_why", kBlowerWhy), WINEADOR_LIST("event_types", kTypeNames),
};
const int kWordListCount = (int)(sizeof(kWordLists) / sizeof(kWordLists[0]));

const char* wordFor(const char* list, const char* code) {
  if (!list || !code) return nullptr;
  for (int i = 0; i < kWordListCount; i++) {
    if (strcmp(kWordLists[i].name, list) != 0) continue;
    for (int k = 0; k < kWordLists[i].count; k++)
      if (strcmp(kWordLists[i].items[k].code, code) == 0) return kWordLists[i].items[k].word;
  }
  return nullptr;
}

static const char* orCode(const char* w, const char* code) { return w ? w : (code ? code : ""); }

const char* guardWords(const char* code) { return orCode(wordFor("guards", code), code); }
const char* exclusionWords(const char* code) { return orCode(wordFor("exclusions", code), code); }
const char* restartWords(const char* code) { return orCode(wordFor("restarts", code), code); }
const char* ionWhyWords(const char* text) { return orCode(wordFor("ion_why", text), text); }
const char* blowerWhyWords(const char* text) { return orCode(wordFor("blower_why", text), text); }

const char* positionName(const char* code) {
  if (!code) return "";
  for (int i = 0; i < kNumSensors; i++)
    if (strcmp(kSensors[i].position, code) == 0) return kSensors[i].name;
  return code;
}

// ---------------------------------------------------------------- web/words.js
namespace {
struct Out {
  char* p; size_t n; size_t len = 0;
  Out(char* p_, size_t n_) : p(p_), n(n_) { if (n) p[0] = 0; }
  void add(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    va_list a;
    va_start(a, fmt);
    const int w = vsnprintf(len < n ? p + len : nullptr, len < n ? n - len : 0, fmt, a);
    va_end(a);
    if (w > 0) len += (size_t)w;
  }
};
}  // namespace

int formatWebWordsJs(char* out, size_t n) {
  Out o(out, n);
  o.add("// GENERATED by tools/make_web_words.py from lib/core/src/wineador/words.cpp and the sensor\n"
        "// table (sensor_table.cpp): never edit by hand. The words the pages show for the API's codes\n"
        "// (spec 5.5 \"Words on screen\"); test_web_words fails if this file is out of date.\n"
        "\"use strict\";\n"
        "const WORDS = {\n");
  o.add("  positions: {");
  for (int i = 0; i < kNumSensors; i++) o.add("%s\"%s\": \"%s\"", i ? ", " : "", kSensors[i].position, kSensors[i].name);
  o.add("},\n");
  for (int l = 0; l < kWordListCount; l++) {
    const WordList& w = kWordLists[l];
    o.add("  %s: {", w.name);
    for (int k = 0; k < w.count; k++) o.add("%s\"%s\": \"%s\"", k ? ", " : "", w.items[k].code, w.items[k].word);
    o.add("},\n");
  }
  o.add("};\n"
        "// The word for a code, or the code itself if this page doesn't know it.\n"
        "const word = (list, code) => (code === null || code === undefined ? code : WORDS[list][code] ?? code);\n");
  return (int)o.len;
}

}  // namespace wineador
