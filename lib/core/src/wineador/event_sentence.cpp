#include "wineador/event_sentence.h"
#include "wineador/event_text.h"
#include "wineador/sensor_table.h"
#include "wineador/words.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// The sentences follow spec 5.5 "Words on screen": the word list's words, never a code; numbers
// as rule 8 ("20.6°C", "12.1V", "7 min"), except that temperature and humidity keep one decimal
// so a figure never contradicts its threshold ("59.6%, below 60.0%"). They are read back out of
// the logged detail, which never changes (tools read it): old cards' wording still reads.

namespace wineador {

namespace {

constexpr const char* kDeg = "\xC2\xB0";      // °
constexpr const char* kNbsp = "\xC2\xA0";     // a non-breaking space: a word unit stays with its number

bool starts(const char* s, const char* p) { return strncmp(s, p, strlen(p)) == 0; }

const char* afterColon(const char* s) {
  const char* c = strstr(s, ": ");
  return c ? c + 2 : s;
}

// Copies s with "20.5 C" as "20.5°C", " -> " as " → ", " <= " / " >= " as ≤ / ≥.
int plain(char* out, size_t n, const char* s) {
  size_t k = 0;
  auto put = [&](const char* t) { for (; *t && k + 1 < n; t++) out[k++] = *t; };
  for (const char* c = s; *c && k + 1 < n; c++) {
    if (*c == ' ' && c[1] == 'C' && c > s && isdigit((unsigned char)c[-1]) &&
        (c[2] == 0 || c[2] == ' ' || c[2] == ',' || c[2] == ')' || c[2] == ';' || c[2] == '.')) {
      put(kDeg); put("C");
      c++;
      continue;
    }
    if (starts(c, " -> ")) { put(" \xE2\x86\x92 "); c += 3; continue; }
    if (starts(c, " <= ")) { put(" \xE2\x89\xA4 "); c += 3; continue; }
    if (starts(c, " >= ")) { put(" \xE2\x89\xA5 "); c += 3; continue; }
    out[k++] = *c;
  }
  out[k] = 0;
  return (int)k;
}

// "Label: the rest, made plain" ("Label" alone when there is no rest).
int labelled(char* out, size_t n, const char* label, const char* rest) {
  if (!rest || !*rest) return snprintf(out, n, "%s", label);
  int k = snprintf(out, n, "%s: ", label);
  if (k < 0 || (size_t)k >= n) return k;
  return k + plain(out + k, n - (size_t)k, rest);
}

// The text inside the first (...) of d, into out ("" if none).
void inParens(const char* d, char* out, size_t n) {
  out[0] = 0;
  const char* a = strchr(d, '(');
  if (!a) return;
  const char* b = strrchr(a, ')');
  const size_t len = b ? (size_t)(b - a - 1) : strlen(a + 1);
  snprintf(out, n, "%.*s", (int)(len < n ? len : n - 1), a + 1);
}

// The value after `key` in d, as a float. False if not there.
bool numberAfter(const char* d, const char* key, float* v) {
  const char* p = strstr(d, key);
  return p && sscanf(p + strlen(key), "%f", v) == 1;
}

// A word with its first letter small ("Power on or reset" -> "power on or reset").
void lowerFirst(char* out, size_t n, const char* w) {
  snprintf(out, n, "%s", w ? w : "");
  if (out[0]) out[0] = (char)tolower((unsigned char)out[0]);
}

// Rule 8's durations from seconds: "45 s", "1 min 3 s", "7 min", "3 h 5 min", "31 d 4 h"; a zero
// second part dropped ("2 min", "3 h", "31 d").
void duration(char* out, size_t n, unsigned long s) {
  if (s < 60) { snprintf(out, n, "%lu%ss", s, kNbsp); return; }
  if (s < 3600) {
    const unsigned long m = s / 60, r = s % 60;
    if (r) snprintf(out, n, "%lu%smin %lu%ss", m, kNbsp, r, kNbsp);
    else snprintf(out, n, "%lu%smin", m, kNbsp);
    return;
  }
  if (s < 86400) {
    const unsigned long h = s / 3600, m = (s % 3600) / 60;
    if (m) snprintf(out, n, "%lu%sh %lu%smin", h, kNbsp, m, kNbsp);
    else snprintf(out, n, "%lu%sh", h, kNbsp);
    return;
  }
  const unsigned long d = s / 86400, h = (s % 86400) / 3600;
  if (h) snprintf(out, n, "%lu%sd %lu%sh", d, kNbsp, h, kNbsp);
  else snprintf(out, n, "%lu%sd", d, kNbsp);
}

// "on 2 min 3 s" / "on 45 s" in a logged detail -> seconds. False if not there.
bool onFor(const char* d, unsigned long* s) {
  const char* p = strstr(d, "on ");
  for (; p; p = strstr(p + 1, "on ")) {
    unsigned long m = 0, x = 0;
    if (sscanf(p, "on %lu min %lu s", &m, &x) == 2) { *s = m * 60 + x; return true; }
    if (sscanf(p, "on %lu s", &x) == 1 && strstr(p, " s")) { *s = x; return true; }
  }
  return false;
}

// "ch3 mid_rear ..." at p -> the channel and the position's name.
bool channelAndPosition(const char* p, int* ch, char* name, size_t n) {
  char pos[32] = "";
  if (sscanf(p, "ch%d %31[^ :,]", ch, pos) != 2) return false;
  snprintf(name, n, "%s", positionName(pos));
  return true;
}

// The position's name for a channel number (0-5), or "ch7" if it isn't one.
void channelName(int ch, char* out, size_t n) {
  if (ch >= 0 && ch < kNumSensors) snprintf(out, n, "%s", kSensors[ch].name);
  else snprintf(out, n, "ch%d", ch);
}

typedef int (*Maker)(char* out, size_t n, const char* d);

// ---------------------------------------------------------------- the loads
// "compressor ON (temperature call, control 20.62 C)" -> "Compressor started: cooling, 20.6°C"
int loadSentence(char* out, size_t n, const char* name, const char* onText, const char* offText, const char* d) {
  char why[160];
  inParens(d, why, sizeof(why));
  if (starts(d, onText)) {
    float t;
    const bool test = strstr(why, "scenario") || strstr(why, "directly");
    if (test) return snprintf(out, n, "%s started: test", name);
    if (starts(why, "RH strip")) return snprintf(out, n, "%s started: drying", name);
    if (starts(why, "temperature call") && numberAfter(why, "control ", &t)) {
      if (strcmp(name, "Compressor") == 0) return snprintf(out, n, "%s started: cooling, %.1f%sC", name, (double)t, kDeg);
      return snprintf(out, n, "%s started: %.1f%sC", name, (double)t, kDeg);
    }
    if (starts(why, "temperature call") && strcmp(name, "Compressor") == 0) return snprintf(out, n, "%s started: cooling", name);
    return snprintf(out, n, "%s started", name);
  }
  if (starts(d, offText)) {
    if (!why[0] || strcmp(why, "demand ended") == 0) return snprintf(out, n, "%s stopped", name);
    return snprintf(out, n, "%s stopped: %s", name, guardWords(why));
  }
  return labelled(out, n, name, d);
}

int compressor(char* o, size_t n, const char* d) { return loadSentence(o, n, "Compressor", kEvCompressorOn, kEvCompressorOff, d); }
int heater(char* o, size_t n, const char* d) { return loadSentence(o, n, "Heater", kEvHeaterOn, kEvHeaterOff, d); }
int humidifier(char* o, size_t n, const char* d) { return loadSentence(o, n, "Humidifier", kEvHumidifierOn, kEvHumidifierOff, d); }

int interlock(char* o, size_t n, const char* d) {
  const char* extra = strstr(d, "relay exercise") ? " (relay check)" : "";
  if (starts(d, kEvK1Heating)) return snprintf(o, n, "Power switched to heating%s", extra);
  if (starts(d, kEvK1Cooling)) return snprintf(o, n, "Power switched to cooling%s", extra);
  return labelled(o, n, "Power to", d);
}

// "HARVEST START (cooling): ..." -> "Fan run-on after cooling started"
int harvest(char* o, size_t n, const char* d) {
  const char* kind = strstr(d, "(heating)") ? "heating" : "cooling";
  if (starts(d, "HARVEST START")) return snprintf(o, n, "Fan run-on after %s started", kind);
  if (starts(d, "HARVEST END")) {
    float s = 0;
    char t[32] = "";
    if (numberAfter(d, " after ", &s)) duration(t, sizeof(t), (unsigned long)(s + 0.5f));
    const char* why = strstr(d, "BACKSTOP") ? ": time limit" : strstr(d, "a load started") ? ": a load started" : "";
    return t[0] ? snprintf(o, n, "Fan run-on ended after %s%s", t, why) : snprintf(o, n, "Fan run-on ended%s", why);
  }
  return labelled(o, n, "Fan run-on", d);
}

// "humidifier blocked: compressor-run" -> "Humidifier waiting: compressor on"
int guard(char* o, size_t n, const char* d) {
  char b[240];
  snprintf(b, sizeof(b), "%s", d);
  char* x = strstr(b, " blocked: ");
  if (x) {
    *x = 0;
    char code[48] = "";
    sscanf(x + 10, "%47[^ (]", code);
    if (b[0]) b[0] = (char)toupper((unsigned char)b[0]);
    return snprintf(o, n, "%s waiting: %s", b, guardWords(code));
  }
  x = strstr(b, " block cleared");
  if (x) {
    *x = 0;
    if (b[0]) b[0] = (char)toupper((unsigned char)b[0]);
    return snprintf(o, n, "%s no longer waiting", b);
  }
  return labelled(o, n, "Waiting", d);
}

// ---------------------------------------------------------------- sensors
// "ch3 mid_rear: CRC failure (12 since boot)" -> "Bad read: Middle rear";
// "ch3: 5 more CRC failures / timeouts in the last hour ..." -> "Middle rear: 5 more bad reads or no answers this hour"
int readProblem(char* o, size_t n, const char* d, const char* word) {
  int ch = -1;
  unsigned long more = 0;
  char name[32];
  if (sscanf(d, "ch%d: %lu more", &ch, &more) == 2) {
    channelName(ch, name, sizeof(name));
    return snprintf(o, n, "%s: %lu more bad reads or no answers this hour", name, more);
  }
  if (channelAndPosition(d, &ch, name, sizeof(name))) return snprintf(o, n, "%s: %s", word, name);
  return labelled(o, n, word, d);
}
int crc(char* o, size_t n, const char* d) { return readProblem(o, n, d, "Bad read"); }
int timeoutS(char* o, size_t n, const char* d) { return readProblem(o, n, d, "No answer"); }

// "SENSOR EXCLUDED: ch3 mid_rear - absent" (or "ch3 mid_rear excluded: absent") -> "Middle rear excluded: not found"
int sensor(char* o, size_t n, const char* d) {
  const bool out = starts(d, "SENSOR EXCLUDED") || strstr(d, " excluded");
  const bool back = starts(d, "SENSOR RECOVERED") || strstr(d, " recovered");
  const char* p = strstr(d, "ch");
  int ch = -1;
  char name[32];
  if (p && channelAndPosition(p, &ch, name, sizeof(name))) {
    if (out) {
      const char* r = strstr(p, " - ");
      r = r ? r + 3 : strstr(p, ": ") ? strstr(p, ": ") + 2 : "";
      char code[32] = "";
      sscanf(r, "%31[^ ,;.]", code);
      return code[0] ? snprintf(o, n, "%s excluded: %s", name, exclusionWords(code)) : snprintf(o, n, "%s excluded", name);
    }
    if (back) return snprintf(o, n, "%s back in use", name);
  }
  return labelled(o, n, "Sensor", d);
}

int fan(char* o, size_t n, const char* d) {
  unsigned long k = 0;
  const char* p = strstr(d, "kickstart");
  if (p) {
    if (sscanf(p, "kickstart %lu", &k) == 1) return snprintf(o, n, "Plenum fan restart try %lu", k);
    return snprintf(o, n, "Plenum fan restart try");
  }
  if (starts(d, "FAN STALL")) return snprintf(o, n, "Plenum fan stalled: compressor and heater off");
  if (starts(d, "FAN RECOVERED")) return snprintf(o, n, "Plenum fan running again");
  return labelled(o, n, "Plenum fan", d);
}

// ---------------------------------------------------------------- settings
// One change, "target 20.0 -> 20.5 C" or "target_temp_c 20.0 -> 20.5", in words.
void settingChange(char* out, size_t n, const char* item) {
  struct K { const char* code; const char* word; const char* unit; };
  static const K kKeys[] = {   // the firmware's words (main.cpp) and the API's names
    {"target_temp_c", "Target temperature", "\xC2\xB0" "C"}, {"rh_setpoint_pct", "Target humidity", "%"},
    {"heater_enable", "Heater", ""}, {"humidifier_enable", "Humidifier", ""}, {"ion_enable", "Ion generator", ""},
    {"ion_runs_per_day", "Ion runs per day", ""}, {"ion_minutes_per_run", "Ion minutes per run", ""},
    {"target", "Target temperature", "\xC2\xB0" "C"}, {"RH setpoint", "Target humidity", "%"},
    {"heater", "Heater", ""}, {"humidifier", "Humidifier", ""}, {"ion generator", "Ion generator", ""},
    {"ion runs per day", "Ion runs per day", ""}, {"ion minutes per run", "Ion minutes per run", ""},
  };
  out[0] = 0;
  // a sensor excluded by hand: "ch3 mid_rear manual exclude on" / "manual excludes 0x00 -> 0x08"
  int ch = -1;
  char name[32], onoff[8] = "";
  if (strstr(item, "manual exclude ") && channelAndPosition(item, &ch, name, sizeof(name)) &&
      sscanf(strstr(item, "manual exclude ") + 15, "%7s", onoff) == 1) {
    snprintf(out, n, strcmp(onoff, "on") == 0 ? "%s excluded by hand" : "%s back in use", name);
    return;
  }
  unsigned a = 0, b = 0;
  if (sscanf(item, "manual excludes 0x%x -> 0x%x", &a, &b) == 2) {
    size_t len = 0;
    for (int i = 0; i < kNumSensors; i++) {
      const bool x = a & (1u << i), y = b & (1u << i);
      if (x == y) continue;
      const int w = snprintf(out + len, len < n ? n - len : 0, "%s%s %s", len ? ", " : "", kSensors[i].name,
                             y ? "excluded by hand" : "back in use");
      if (w > 0) len += (size_t)w;
      if (len >= n) break;
    }
    return;
  }
  const char* arrow = strstr(item, kEvChangeArrow);
  for (const K& k : kKeys) {
    const size_t L = strlen(k.code);
    if (strncmp(item, k.code, L) != 0 || item[L] != ' ' || !arrow) continue;
    char from[24] = "", to[24] = "";
    sscanf(item + L + 1, "%23s", from);
    sscanf(arrow + strlen(kEvChangeArrow), "%23[^ ;,%]", to);
    snprintf(out, n, "%s %s \xE2\x86\x92 %s%s", k.word, from, to, k.unit);
    return;
  }
  plain(out, n, item);
}

// "settings changed (dashboard): target 20.0 -> 20.5 C; saved" -> "Target temperature 20.0 → 20.5°C (dashboard)"
int settings(char* o, size_t n, const char* d) {
  if (starts(d, kEvSettingsChanged)) {
    char from[64] = "";
    sscanf(d + strlen(kEvSettingsChanged), " (%63[^)])", from);   // to the first ")": "NOT SAVED (...)" may follow
    if (strstr(d, "rounded")) return snprintf(o, n, "Targets rounded to their steps");
    const char* where = strcmp(from, "LCD") == 0 ? "cabinet screen" : starts(from, "serial") ? "serial console" : from;
    // the changes: after "): ", split at "; " (the firmware) or ", " (describeUserChange)
    const char* p = strstr(d, "): ");
    p = p ? p + 3 : afterColon(d);
    char rest[240];
    snprintf(rest, sizeof(rest), "%s", p);
    size_t len = 0;
    bool notSaved = false;
    o[0] = 0;
    for (char* item = rest; item && *item;) {
      char* next = strstr(item, "; ");
      char* comma = strstr(item, ", ");
      if (comma && (!next || comma < next) && !strstr(item, "NOT SAVED")) next = comma;
      if (next) { *next = 0; next += 2; }
      if (strcmp(item, "saved") == 0) { item = next; continue; }
      if (starts(item, "NOT SAVED")) { notSaved = true; break; }
      char one[120];
      settingChange(one, sizeof(one), item);
      if (one[0]) {
        const int w = snprintf(o + len, len < n ? n - len : 0, "%s%s", len ? ", " : "", one);
        if (w > 0) len += (size_t)w;
      }
      item = next;
    }
    if (len == 0) len = (size_t)snprintf(o, n, "Settings changed");
    const int w = snprintf(o + (len < n ? len : n - 1), len < n ? n - len : 1, "%s%s%s%s", where[0] ? " (" : "", where,
                           where[0] ? ")" : "", notSaved ? ": not saved" : "");
    return (int)len + (w > 0 ? w : 0);
  }
  if (starts(d, "LCD: settings refused")) return snprintf(o, n, "Settings from the cabinet screen not saved");
  return labelled(o, n, "Settings", d);
}

// ---------------------------------------------------------------- the board
// "BOOT: reason POWERON; firmware 2.0.0; ..." -> "Started: power on or reset, firmware 2.0.0"
int boot(char* o, size_t n, const char* d) {
  if (starts(d, kEvBootLine)) {
    char reason[40] = "", fw[40] = "", w[40];
    sscanf(d + strlen(kEvBootLine), " %39[^;]", reason);
    const char* f = strstr(d, "firmware ");
    if (f) sscanf(f + 9, "%39[^;]", fw);
    lowerFirst(w, sizeof(w), restartWords(reason));
    return snprintf(o, n, "Started: %s%s%s", w, fw[0] ? ", firmware " : "", fw);
  }
  return labelled(o, n, "Restart", d);
}

int sd(char* o, size_t n, const char* d) {
  if (starts(d, kEvSdFlushFailure)) return snprintf(o, n, "SD card failed: logs kept in memory until it's back");
  if (starts(d, kEvSdCardBack)) return snprintf(o, n, "SD card back: writing the kept logs");
  if (starts(d, kEvSdRetention)) {
    // "... deleted /logs/2025/readings-2025-01.csv (12 MB)": the month as "Jan 2025"
    static const char* const mon[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    for (const char* p = d + strlen(kEvSdRetention); *p; p++) {
      int y = 0, m = 0;
      if (isdigit((unsigned char)p[0]) && sscanf(p, "%4d-%2d", &y, &m) == 2 && y > 2000 && m >= 1 && m <= 12)
        return snprintf(o, n, "Card nearly full: oldest readings deleted (%s %d)", mon[m - 1], y);
    }
    return snprintf(o, n, "Card nearly full: oldest readings deleted");
  }
  if (starts(d, kEvSdRetriedOk)) return snprintf(o, n, "A card write failed once and worked on the retry");
  if (starts(d, kEvSdPowerCycled)) return snprintf(o, n, "SD card switched off and on again to recover it");
  if (starts(d, kEvSdFailNo)) return snprintf(o, n, "Card write failed");
  if (starts(d, kEvSdStressStart) || starts(d, kEvSdStressDone) || starts(d, kEvSdStressChecks) || starts(d, "SD STRESS"))
    return labelled(o, n, "SD stress test", afterColon(d));
  return labelled(o, n, "SD card", d);
}

// "I2C BUS STUCK with ch2 selected: ..." -> "Sensor bus stuck: Middle front excluded"
int i2c(char* o, size_t n, const char* d) {
  int ch = -1;
  char name[32];
  const char* p = strstr(d, "with ch");
  if (p && sscanf(p, "with ch%d", &ch) == 1) {
    channelName(ch, name, sizeof(name));
    return snprintf(o, n, "Sensor bus stuck: %s excluded", name);
  }
  return labelled(o, n, "Sensor bus fault", d);
}

int rtc(char* o, size_t n, const char* d) {
  if (starts(d, kEvRtcLostTime)) return snprintf(o, n, "Clock lost its time. Internet time will set it.");
  if (strstr(d, "EOSC was SET")) return snprintf(o, n, "Clock battery setting fixed");
  if (strstr(d, "status register not read")) return snprintf(o, n, "Clock check skipped at start");
  return labelled(o, n, "Clock", d);
}

int ntp(char* o, size_t n, const char* d) {
  long long s = 0;
  const char* p = strstr(d, " was ");
  if (starts(d, kEvNtpCorrection) && p && sscanf(p + 5, "%lld", &s) == 1) {
    char t[32];
    duration(t, sizeof(t), (unsigned long)llabs(s));
    return snprintf(o, n, "Clock corrected by %s", t);
  }
  if (starts(d, "NTP SYNC")) return snprintf(o, n, "Clock checked against internet time");
  if (starts(d, "NTP: DS3231 within")) {
    unsigned long w = 2;
    sscanf(d, "NTP: DS3231 within %lu", &w);
    return snprintf(o, n, "Clock right to %lu%ss", w, kNbsp);
  }
  if (starts(d, "NTP: DS3231 write FAILED")) return snprintf(o, n, "Clock not corrected: write failed");
  return labelled(o, n, "Internet time", d);
}

// ---------------------------------------------------------------- control
int crossover(char* o, size_t n, const char* d) {
  if (strstr(d, "cooling started")) return snprintf(o, n, "Changeover: heating to cooling");
  if (strstr(d, "heating started")) return snprintf(o, n, "Changeover: cooling to heating");
  return labelled(o, n, "Changeover", d);
}

// "RH STRIP START (...): mean RH 68.1% >= 67.0% ..." -> "Drying started: humidity 68.1%, above 67.0%"
int strip(char* o, size_t n, const char* d) {
  float rh, trig;
  if (starts(d, kEvStripStart) && numberAfter(d, "mean RH ", &rh) && numberAfter(d, ">= ", &trig))
    return snprintf(o, n, "Drying started: humidity %.1f%%, above %.1f%%", (double)rh, (double)trig);
  if (starts(d, kEvStripStop)) {
    struct W { const char* text; const char* word; };
    static const W kWhy[] = {   // temp_control.cpp stripStopName
      {"vapour pressure", "back to target"}, {"temperature floor", "too cold"}, {"heating called", "heating needed"},
      {"too few healthy", "too few sensors"}, {"stripping disabled", "switched off"}, {"scenario", "test"},
    };
    const char* why = afterColon(d);
    for (const W& w : kWhy)
      if (starts(why, w.text)) return snprintf(o, n, "Drying ended: %s", w.word);
    return snprintf(o, n, "Drying ended");
  }
  return labelled(o, n, "Drying", d);
}

// "HUMIDIFY CALL START (...): mean RH 59.4% <= 60.0% ..." -> "Humidifying started: humidity 59.4%, below 60.0%"
int humidify(char* o, size_t n, const char* d) {
  float rh, below;
  if (starts(d, kEvHumidifyStart) && numberAfter(d, "mean RH ", &rh) && numberAfter(d, "<= ", &below))
    return snprintf(o, n, "Humidifying started: humidity %.1f%%, below %.1f%%", (double)rh, (double)below);
  if (starts(d, kEvHumidifyEnd) && numberAfter(d, "mean RH ", &rh))
    return snprintf(o, n, "Humidifying ended: humidity %.1f%%, at target", (double)rh);
  return labelled(o, n, "Humidifying", d);
}

int exercise(char* o, size_t n, const char* d) {
  unsigned a = 0, b = 0;
  const char* p = strstr(d, "cycle ");
  if (p && sscanf(p, "cycle %u/%u", &a, &b) == 2)
    return snprintf(o, n, "Relay check: cycle %u of %u%s", a, b, strstr(d, "released") ? " done" : "");
  if (strstr(d, "started")) return snprintf(o, n, "Relay check started");
  if (strstr(d, "done")) return snprintf(o, n, "Relay check done");
  return labelled(o, n, "Relay check", d);
}

// "HEATER INEFFECTIVE: chamber changed +0.10 C in 900 s ..." -> "Heater not working: chamber rose 0.1°C in 15 min"
int ineffective(char* o, size_t n, const char* d) {
  const char* who = starts(d, "HEATER") ? "Heater" : starts(d, "COOLING") ? "Cooling" : starts(d, "HUMIDIFIER") ? "Humidifier" : nullptr;
  if (!who) return labelled(o, n, "Not working", d);
  if (strstr(d, "EFFECTIVE AGAIN")) return snprintf(o, n, "%s working again", who);
  float c;
  unsigned long s;
  const char* p = strstr(d, "chamber changed ");
  if (p && sscanf(p + 16, "%f C in %lu s", &c, &s) == 2) {
    char t[32];
    duration(t, sizeof(t), s);
    return snprintf(o, n, "%s not working: chamber %s %.1f%sC in %s", who, c < 0 ? "fell" : "rose",
                    (double)(c < 0 ? -c : c), kDeg, t);
  }
  if (strstr(d, "unreadable")) return snprintf(o, n, "%s not working: chamber unreadable", who);
  return snprintf(o, n, "%s not working", who);
}

int disturbance(char* o, size_t n, const char* d) { return labelled(o, n, "Disturbance detected", d); }
int heap(char* o, size_t n, const char* d) { (void)d; return snprintf(o, n, "Memory low: below 30%sKB", kNbsp); }
int psram(char* o, size_t n, const char* d) {
  if (starts(d, "NO PSRAM")) return snprintf(o, n, "Extra memory not found: screens off, smaller log buffers");
  return labelled(o, n, "Extra memory", d);
}
int hwdog(char* o, size_t n, const char* d) {
  unsigned long ms;
  if (starts(d, "HARDWARE WATCHDOG: ") && sscanf(d + 19, "%lu ms", &ms) == 1)
    return snprintf(o, n, "Watchdog nearly missed: %lu%sms", ms, kNbsp);
  return labelled(o, n, "Watchdog", d);
}
int lcd(char* o, size_t n, const char* d) {
  unsigned long ms;
  if (strstr(d, "Allow update pressed")) return snprintf(o, n, "Allow update pressed");
  const char* p = strstr(d, "took up to ");
  if (starts(d, "LCD STEP OVER") && p && sscanf(p + 11, "%lu", &ms) == 1)
    return snprintf(o, n, "Cabinet screen slow: %lu%sms", ms, kNbsp);
  if (starts(d, "TOUCH SETUP FAILED")) return snprintf(o, n, "Touch screen setup failed");
  if (starts(d, "LCD SCREENS OFF"))
    return snprintf(o, n, "Cabinet screen off%s", strstr(d, "PSRAM") || strstr(d, "memory") ? ": no extra memory" : "");
  return labelled(o, n, "Cabinet screen", d);
}

int supply12v(char* o, size_t n, const char* d) {
  float below, low;
  unsigned long ms, more;
  if (starts(d, kEvDip12v) && numberAfter(d, "below ", &below)) return snprintf(o, n, "12V supply dipped below %.1fV", (double)below);
  if (starts(d, kEvRecovered12v)) {
    const char* p = strstr(d, "after ");
    if (p && sscanf(p + 6, "%lu", &ms) == 1 && numberAfter(d, "lowest ", &low))
      return snprintf(o, n, "12V supply back after %.1f%ss, lowest %.1fV", ms / 1000.0, kNbsp, (double)low);
  }
  if (sscanf(d, "%lu more 12V dips", &more) == 1) return snprintf(o, n, "%lu more 12V dips this hour", more);
  return labelled(o, n, "12V supply", d);
}

int wifi(char* o, size_t n, const char* d) {
  if (starts(d, kEvWifiLost)) return snprintf(o, n, "WiFi lost");
  unsigned long s;
  if (starts(d, kEvWifiBack) && sscanf(d + strlen(kEvWifiBack), "%lu", &s) == 1) return snprintf(o, n, "WiFi back after %lu%ss", s, kNbsp);
  if (sscanf(d, "%lu more WiFi outages", &s) == 1) return snprintf(o, n, "%lu more WiFi drops this hour", s);
  return labelled(o, n, "WiFi", d);
}

int door(char* o, size_t n, const char* d) {
  if (starts(d, kEvDoorTest)) {
    const char* note = d + strlen(kEvDoorTest);
    if (starts(note, ": ")) note += 2;
    return note[0] ? snprintf(o, n, "Door test: %s", note) : snprintf(o, n, "Door test");
  }
  return labelled(o, n, "Door test", d);
}

// ---------------------------------------------------------------- updates
// The update's outcome as logged (ota_update.cpp otaOutcomeText) -> its words ("upload stalled").
const char* outcomeWords(const char* t) {
  struct W { const char* text; const char* word; };
  static const W k[] = {
    {"no data for", "upload stalled"}, {"the upload stopped", "upload cut off"}, {"the image's own check", "file damaged"},
    {"a flash write", "couldn't write it"}, {"the writing stopped", "writing stalled"},
  };
  for (const W& w : k)
    if (starts(t, w.text)) return w.word;
  return nullptr;
}

// A refusal's code (and image code) as logged -> words ("compressor on").
const char* refusalWords(const char* code, const char* image) {
  struct W { const char* code; const char* word; };
  static const W kImage[] = {
    {"too_short", "not a firmware file"}, {"not_firmware", "not a firmware file"}, {"rev_n_image", "built for another board"},
    {"wrong_chip", "built for another board"}, {"no_app_description", "not openHumidor firmware"},
    {"no_identity", "not a cabinet build"}, {"unknown_identity_layout", "too new for this firmware"},
    {"wrong_board", "built for another board"}, {"bench_build", "bench build"}, {"test_build", "test build"},
  };
  static const W kCode[] = {
    {"compressor_running", "compressor on"}, {"window_closed", "updates closed"}, {"already_updating", "already updating"},
    {"updating", "updating"}, {"being_checked", "last update still being checked"}, {"too_big", "file too big"},
    {"wrong_image", "not a cabinet build"}, {"busy", "board busy"}, {"low_memory", "board short of memory"},
    {"stalled", "upload stalled"}, {"corrupt_image", "file damaged"}, {"flash_failed", "couldn't write it"},
  };
  if (image && image[0])
    for (const W& w : kImage)
      if (strcmp(image, w.code) == 0) return w.word;
  for (const W& w : kCode)
    if (strcmp(code, w.code) == 0) return w.word;
  return nullptr;
}

int ota(char* o, size_t n, const char* d) {
  char v[40] = "", old[40] = "";
  unsigned long more;
  if (starts(d, kEvOtaStarted)) {
    // "OTA UPDATE STARTED: 2.0.0 (1413978 bytes), replacing 1.9.0; loads off"
    sscanf(afterColon(d), "%39[^ (,;]", v);
    const char* r = strstr(d, "replacing ");
    if (r) sscanf(r + 10, "%39[^ ;,]", old);
    return old[0] ? snprintf(o, n, "Update started: %s (was %s)", v, old) : snprintf(o, n, "Update started: %s", v);
  }
  if (starts(d, kEvOtaInstalled)) {
    sscanf(afterColon(d), "%39[^ ,;]", v);
    return snprintf(o, n, "Update installed: %s. Restarting", v);
  }
  if (starts(d, kEvOtaAbandoned) || starts(d, kEvOtaFailed)) {
    // "...: 2.0.0, no data for 60 s; half-written image discarded, normal control"
    const bool stopped = starts(d, kEvOtaAbandoned);
    const char* c = strstr(afterColon(d), ", ");
    const char* w = c ? outcomeWords(c + 2) : nullptr;
    return snprintf(o, n, "Update %s%s%s. Nothing changed", stopped ? "stopped" : "failed", w ? ": " : "", w ? w : "");
  }
  if (starts(d, kEvOtaInterrupted)) {
    const char* s = strstr(d, "still ");
    if (s) sscanf(s + 6, "%39[^ ,;]", v);
    return v[0] ? snprintf(o, n, "Update cut off by a restart: still %s", v) : snprintf(o, n, "Update cut off by a restart");
  }
  if (starts(d, kEvOtaRefused)) {
    // "OTA upload refused: wrong_image bench_build (message)"
    char code[40] = "", image[40] = "";
    sscanf(afterColon(d), "%39[^ (] %39[^ (]", code, image);
    const char* w = refusalWords(code, image);
    return w ? snprintf(o, n, "Update refused: %s", w) : snprintf(o, n, "Update refused");
  }
  if (starts(d, kEvOtaWaiting)) {
    // "OTA: new image running: 2.0.0, waiting to start properly (...)"
    sscanf(d + strlen(kEvOtaWaiting), ": %39[^ ,;]", v);
    return v[0] ? snprintf(o, n, "Checking new firmware %s", v) : snprintf(o, n, "Checking new firmware");
  }
  if (starts(d, kEvOtaGood)) {
    sscanf(d + strlen(kEvOtaGood), ": %39[^ ,;]", v);
    return v[0] ? snprintf(o, n, "Update done: %s", v) : snprintf(o, n, "Update done");
  }
  if (starts(d, kEvOtaRolledBack)) {
    // "OTA ROLLED BACK from 2.0.0 to 1.9.0: it didn't start properly"
    const char* f = strstr(d, "from ");
    if (f && sscanf(f, "from %39s to %39[^ :;,]", v, old) == 2)
      return snprintf(o, n, "Update rolled back: %s didn't start; back on %s", v, old);
    return snprintf(o, n, "Update rolled back");
  }
  if (starts(d, kEvOtaSelfRollback)) return snprintf(o, n, "New firmware didn't start: going back");
  if (starts(d, kEvOtaRollbackTest)) return labelled(o, n, "Rollback test image", afterColon(d));
  if (strstr(d, kEvOtaRefusedMore) && sscanf(d, "%lu", &more) == 1) return snprintf(o, n, "%lu more refused updates this hour", more);
  return labelled(o, n, "Update", d);
}

// ---------------------------------------------------------------- blowers and the ion generator
int blowers(char* o, size_t n, const char* d) {
  if (starts(d, kEvBlowersBoost)) {
    float t, rh;
    if (numberAfter(d, "spread ", &t) && numberAfter(d, " C, ", &rh))
      return snprintf(o, n, "Blowers to boost: spread %.1f%sC, %.1f%%", (double)t, kDeg, (double)rh);
    if (strstr(d, "after boot")) return snprintf(o, n, "Blowers to boost after start");
    if (strstr(d, "after the update")) return snprintf(o, n, "Blowers to boost after the update");
    if (strstr(d, "after full speed")) return snprintf(o, n, "Blowers to boost after full speed");
    return snprintf(o, n, "Blowers to boost");
  }
  if (starts(d, kEvBlowersIdle)) return snprintf(o, n, "Blowers back to idle");
  if (starts(d, kEvBlowersFullEnd)) return snprintf(o, n, "Blowers' full speed ended");
  if (starts(d, kEvBlowersFull)) {
    const char* why = d + strlen(kEvBlowersFull);
    while (*why == ' ') why++;
    return why[0] ? snprintf(o, n, "Blowers at full speed: %s", blowerWhyWords(why)) : snprintf(o, n, "Blowers at full speed");
  }
  return labelled(o, n, "Blowers", d);
}

int ion(char* o, size_t n, const char* d) {
  unsigned long s = 0, m = 0;
  char t[40], why[48] = "";
  if (starts(d, kEvIonStart)) {
    // "ION RUN START: 2 min (slot 12:00 UTC)[; shortened: ...]": the slot isn't said (the event's own time is UK)
    const char* p = afterColon(d);
    if (sscanf(p, "%lu min %lu s", &m, &s) == 2) duration(t, sizeof(t), m * 60 + s);
    else if (sscanf(p, "%lu min", &m) == 1) duration(t, sizeof(t), m * 60);
    else if (sscanf(p, "%lu s", &s) == 1) duration(t, sizeof(t), s);
    else t[0] = 0;
    const char* cut = strstr(d, "shortened") ? " (10-minute limit)" : "";
    return t[0] ? snprintf(o, n, "Ion generator on: %s%s", t, cut) : snprintf(o, n, "Ion generator on%s", cut);
  }
  if (starts(d, kEvIonEnd)) {
    if (onFor(d, &s)) { duration(t, sizeof(t), s); return snprintf(o, n, "Ion generator off after %s", t); }
    return snprintf(o, n, "Ion generator off");
  }
  if (starts(d, kEvIonCutShort)) {
    sscanf(afterColon(d), "%47[^;(]", why);
    const char* w = ionWhyWords(why);
    if (onFor(d, &s)) { duration(t, sizeof(t), s); return snprintf(o, n, "Ion generator stopped early: %s, after %s", w, t); }
    return snprintf(o, n, "Ion generator stopped early: %s", w);
  }
  if (starts(d, kEvIonSkipped)) {
    sscanf(afterColon(d), "%47[^;(]", why);
    const size_t L = strlen(why);
    if (L && why[L - 1] == ' ') why[L - 1] = 0;
    return snprintf(o, n, "Ion run skipped: %s", ionWhyWords(why));
  }
  if (starts(d, kEvIonStopped)) {
    if (onFor(d, &s)) { duration(t, sizeof(t), s); return snprintf(o, n, "Ion generator switched off after %s", t); }
    return snprintf(o, n, "Ion generator switched off");
  }
  if (starts(d, kEvIonRecordFailed)) return snprintf(o, n, "Ion generator paused: %s", ionWhyWords("cap record can't be saved"));
  if (starts(d, kEvIonRecordSaved)) return snprintf(o, n, "Ion generator record saved: runs resume");
  return labelled(o, n, "Ion generator", d);
}

int scenario(char* o, size_t n, const char* d) {
  const char* p = strstr(d, "] ");
  return labelled(o, n, "Test scenario", p ? p + 2 : d);
}

struct Row { const char* type; Maker make; };
const Row kMakers[] = {
  {kEvTypeCompressor, compressor}, {kEvTypeHeater, heater}, {kEvTypeHumidifier, humidifier},
  {kEvTypeInterlock, interlock}, {kEvTypeHarvest, harvest}, {kEvTypeGuard, guard}, {kEvTypeCrc, crc},
  {kEvTypeTimeout, timeoutS}, {kEvTypeSensor, sensor}, {kEvTypeFan, fan}, {kEvTypeSettings, settings},
  {kEvTypeBoot, boot}, {kEvTypeSd, sd}, {kEvTypeI2c, i2c}, {kEvTypeRtc, rtc}, {kEvTypeNtp, ntp},
  {kEvTypeCrossover, crossover}, {kEvTypeStrip, strip}, {kEvTypeHumidify, humidify}, {kEvTypeExercise, exercise},
  {kEvTypeIneffective, ineffective}, {kEvTypeDisturbance, disturbance}, {kEvTypeHeap, heap}, {kEvTypePsram, psram},
  {kEvTypeHwdog, hwdog}, {kEvTypeLcd, lcd}, {kEvTypeSupply12v, supply12v}, {kEvTypeWifi, wifi}, {kEvTypeDoor, door},
  {kEvTypeOta, ota}, {kEvTypeBlowers, blowers}, {kEvTypeIon, ion}, {kEvTypeScenario, scenario},
};

const Row* find(const char* type) {
  for (const Row& r : kMakers)
    if (type && strcmp(r.type, type) == 0) return &r;
  return nullptr;
}

}  // namespace

bool hasEventSentence(const char* type) { return find(type) != nullptr; }

int eventSentence(char* out, size_t n, const char* type, const char* detail) {
  if (n == 0) return 0;
  out[0] = 0;
  const char* d = detail ? detail : "";
  const Row* r = find(type);
  const int k = r ? r->make(out, n, d) : labelled(out, n, type && *type ? type : "Event", d);
  return k < 0 ? 0 : (k >= (int)n ? (int)n - 1 : k);
}

}  // namespace wineador
