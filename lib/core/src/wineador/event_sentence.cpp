#include "wineador/event_sentence.h"
#include "wineador/event_text.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

namespace wineador {

namespace {

constexpr const char* kDeg = "\xC2\xB0";      // °
constexpr const char* kNbsp = "\xC2\xA0";     // a non-breaking space: a unit stays with its number

bool starts(const char* s, const char* p) { return strncmp(s, p, strlen(p)) == 0; }

const char* afterColon(const char* s) {
  const char* c = strstr(s, ": ");
  return c ? c + 2 : s;
}

// Copies s with "20.5 C" as "20.5 °C", " -> " as " → ", " <= " / " >= " as ≤ / ≥.
int plain(char* out, size_t n, const char* s) {
  size_t k = 0;
  auto put = [&](const char* t) { for (; *t && k + 1 < n; t++) out[k++] = *t; };
  for (const char* c = s; *c && k + 1 < n; c++) {
    if (*c == ' ' && c[1] == 'C' && c > s && isdigit((unsigned char)c[-1]) &&
        (c[2] == 0 || c[2] == ' ' || c[2] == ',' || c[2] == ')' || c[2] == ';' || c[2] == '.')) {
      put(kNbsp); put(kDeg); put("C");
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

// "compressor ON (temperature call)" -> "Compressor started: temperature call"
int loadSentence(char* out, size_t n, const char* name, const char* onText, const char* offText, const char* d) {
  char why[160];
  inParens(d, why, sizeof(why));
  if (starts(d, onText)) return snprintf(out, n, "%s started%s%s", name, why[0] ? ": " : "", why);
  if (starts(d, offText)) return snprintf(out, n, "%s stopped%s%s", name, why[0] ? ": " : "", why);
  return labelled(out, n, name, d);
}

// The value after `key` in d, as a float. False if not there.
bool numberAfter(const char* d, const char* key, float* v) {
  const char* p = strstr(d, key);
  return p && sscanf(p + strlen(key), "%f", v) == 1;
}

typedef int (*Maker)(char* out, size_t n, const char* d);

int compressor(char* o, size_t n, const char* d) { return loadSentence(o, n, "Compressor", kEvCompressorOn, kEvCompressorOff, d); }
int heater(char* o, size_t n, const char* d) { return loadSentence(o, n, "Heater", kEvHeaterOn, kEvHeaterOff, d); }
int humidifier(char* o, size_t n, const char* d) { return loadSentence(o, n, "Humidifier", kEvHumidifierOn, kEvHumidifierOff, d); }

int interlock(char* o, size_t n, const char* d) {
  const char* extra = "";
  if (strstr(d, "relay exercise")) extra = " (relay exercise)";
  if (starts(d, kEvK1Heating)) return snprintf(o, n, "Interlock moved to the heating side%s", extra);
  if (starts(d, kEvK1Cooling)) return snprintf(o, n, "Interlock moved to the cooling side%s", extra);
  return labelled(o, n, "Interlock", d);
}

int harvest(char* o, size_t n, const char* d) {
  const char* kind = strstr(d, "(heating)") ? "heating" : "cooling";
  char label[48];
  if (starts(d, "HARVEST START")) snprintf(label, sizeof(label), "Harvest after %s started", kind);
  else if (starts(d, "HARVEST END")) snprintf(label, sizeof(label), "Harvest after %s ended", kind);
  else return labelled(o, n, "Harvest", d);
  return labelled(o, n, label, afterColon(d));
}

int guard(char* o, size_t n, const char* d) {
  // "humidifier blocked: sensors" / "heater block cleared (started; was crossover)"
  char b[240];
  snprintf(b, sizeof(b), "%s", d);
  char* x = strstr(b, " blocked:");
  if (x) {
    *x = 0;
    if (b[0]) b[0] = (char)toupper((unsigned char)b[0]);
    return snprintf(o, n, "%s held back:%s", b, x + 9);
  }
  x = strstr(b, " block cleared");
  if (x) {
    char why[120];
    inParens(x, why, sizeof(why));
    *x = 0;
    if (b[0]) b[0] = (char)toupper((unsigned char)b[0]);
    return snprintf(o, n, "%s no longer held back%s%s", b, why[0] ? ": " : "", why);
  }
  return labelled(o, n, "Held back", d);
}

int crc(char* o, size_t n, const char* d) { return labelled(o, n, "Sensor read failed its checksum", d); }
int timeoutS(char* o, size_t n, const char* d) { return labelled(o, n, "Sensor read timed out", d); }

int sensor(char* o, size_t n, const char* d) {
  // "ch3 mid_rear excluded: absent" / "ch3 mid_rear recovered"
  int ch = -1;
  char pos[32] = "";
  if (sscanf(d, "ch%d %31s", &ch, pos) == 2) {
    if (strstr(d, "excluded")) return snprintf(o, n, "Sensor ch%d (%s) left out: %s", ch, pos, afterColon(d));
    if (strstr(d, "recovered")) return snprintf(o, n, "Sensor ch%d (%s) back in use", ch, pos);
  }
  return labelled(o, n, "Sensor", d);
}

int fan(char* o, size_t n, const char* d) { return labelled(o, n, "Plenum fan", d); }

int settings(char* o, size_t n, const char* d) {
  // "settings changed (dashboard): target_temp_c 20.0 -> 20.5"
  if (starts(d, kEvSettingsChanged)) {
    char from[64];
    inParens(d, from, sizeof(from));
    char label[96];
    snprintf(label, sizeof(label), "Settings changed%s%s", from[0] ? " from " : "", from);
    return labelled(o, n, label, afterColon(d));
  }
  return labelled(o, n, "Settings", d);
}

int boot(char* o, size_t n, const char* d) {
  // "BOOT: reason POWERON; firmware 2.0.0; ..."
  if (starts(d, kEvBootLine)) {
    char reason[40] = "", fw[40] = "";
    sscanf(d + strlen(kEvBootLine), " %39[^;]", reason);
    const char* f = strstr(d, "firmware ");
    if (f) sscanf(f + 9, "%39[^;]", fw);
    return snprintf(o, n, "Board started (%s)%s%s", reason, fw[0] ? ", firmware " : "", fw);
  }
  return labelled(o, n, "Board", d);
}

int sd(char* o, size_t n, const char* d) {
  if (starts(d, kEvSdFlushFailure)) return snprintf(o, n, "SD card failed: the logs are kept in memory until it is back");
  if (starts(d, kEvSdCardBack)) return snprintf(o, n, "SD card back: what was kept is being written to it");
  if (starts(d, kEvSdRetention)) return labelled(o, n, "Old readings deleted to free card space", d + strlen(kEvSdRetention));
  if (starts(d, kEvSdRetriedOk)) return snprintf(o, n, "A card write failed once and worked on the retry");
  if (starts(d, kEvSdPowerCycled)) return snprintf(o, n, "SD card switched off and on again to recover it");
  if (starts(d, kEvSdFailNo)) return snprintf(o, n, "A card write attempt failed");
  if (starts(d, kEvSdStressStart) || starts(d, kEvSdStressDone) || starts(d, kEvSdStressChecks) || starts(d, "SD STRESS"))
    return labelled(o, n, "SD stress test", afterColon(d));
  return labelled(o, n, "SD card", d);
}

int i2c(char* o, size_t n, const char* d) { return labelled(o, n, "Sensor bus fault", d); }

int rtc(char* o, size_t n, const char* d) {
  if (starts(d, kEvRtcLostTime)) return snprintf(o, n, "The clock lost its time while unpowered; internet time will correct it");
  return labelled(o, n, "Clock", d);
}

int ntp(char* o, size_t n, const char* d) {
  long long s = 0;
  const char* p = strstr(d, " was ");
  if (starts(d, kEvNtpCorrection) && p && sscanf(p + 5, "%lld", &s) == 1)
    return snprintf(o, n, "Clock corrected from internet time (it was %+lld s out)", s);
  if (starts(d, "NTP SYNC")) return snprintf(o, n, "Clock checked against internet time");
  return labelled(o, n, "Internet time", d);
}

int crossover(char* o, size_t n, const char* d) {
  if (strstr(d, "cooling started")) return snprintf(o, n, "Crossover: cooling started after heating");
  if (strstr(d, "heating started")) return snprintf(o, n, "Crossover: heating started after cooling");
  return labelled(o, n, "Crossover", d);
}

int strip(char* o, size_t n, const char* d) {
  float rh, trig;
  if (starts(d, kEvStripStart) && numberAfter(d, "mean RH ", &rh) && numberAfter(d, ">= ", &trig))
    return snprintf(o, n, "RH strip started: RH %.1f%%, above %.1f%%", (double)rh, (double)trig);
  if (starts(d, kEvStripStop)) {
    char why[120] = "";
    sscanf(afterColon(d), "%119[^.]", why);
    return snprintf(o, n, "RH strip ended%s%s", why[0] ? ": " : "", why);
  }
  return labelled(o, n, "RH strip", d);
}

int humidify(char* o, size_t n, const char* d) {
  float rh, below;
  if (starts(d, kEvHumidifyStart) && numberAfter(d, "mean RH ", &rh) && numberAfter(d, "<= ", &below))
    return snprintf(o, n, "Humidifier called: RH %.1f%%, below %.1f%%", (double)rh, (double)below);
  if (starts(d, kEvHumidifyEnd) && numberAfter(d, "mean RH ", &rh))
    return snprintf(o, n, "Humidifier call ended: RH %.1f%% reached the setpoint", (double)rh);
  return labelled(o, n, "Humidify call", d);
}

int exercise(char* o, size_t n, const char* d) { return labelled(o, n, "Relay exercise", d); }

int ineffective(char* o, size_t n, const char* d) {
  // "HEATER INEFFECTIVE: chamber changed ..." -> "Heater ineffective: chamber changed ..."
  char head[48] = "";
  const char* c = strstr(d, ": ");
  if (c && (size_t)(c - d) < sizeof(head)) {
    for (size_t i = 0; i < (size_t)(c - d); i++) head[i] = (char)(i == 0 ? d[i] : tolower((unsigned char)d[i]));
    head[c - d] = 0;
    return labelled(o, n, head, c + 2);
  }
  return labelled(o, n, "Ineffective check", d);
}

int disturbance(char* o, size_t n, const char* d) { return labelled(o, n, "Disturbance detected", d); }
int heap(char* o, size_t n, const char* d) { (void)d; return snprintf(o, n, "Free memory went below its 30 KB floor"); }
int psram(char* o, size_t n, const char* d) { return labelled(o, n, "Extra memory (PSRAM)", d); }
int hwdog(char* o, size_t n, const char* d) { return labelled(o, n, "Hardware watchdog", d); }
int lcd(char* o, size_t n, const char* d) { return labelled(o, n, "Cabinet screen", d); }

int supply12v(char* o, size_t n, const char* d) {
  float below, low;
  unsigned long ms;
  if (starts(d, kEvDip12v) && numberAfter(d, "below ", &below)) return snprintf(o, n, "12%sV supply dipped below %.1f%sV", kNbsp, (double)below, kNbsp);
  if (starts(d, kEvRecovered12v)) {
    const char* p = strstr(d, "after ");
    if (p && sscanf(p + 6, "%lu", &ms) == 1 && numberAfter(d, "lowest ", &low))
      return snprintf(o, n, "12%sV supply back after %.1f%ss; lowest %.2f%sV", kNbsp, ms / 1000.0, kNbsp, (double)low, kNbsp);
  }
  return labelled(o, n, "12 V supply", d);
}

int wifi(char* o, size_t n, const char* d) {
  if (starts(d, kEvWifiLost)) return snprintf(o, n, "WiFi lost");
  unsigned long s;
  if (starts(d, kEvWifiBack) && sscanf(d + strlen(kEvWifiBack), "%lu", &s) == 1) return snprintf(o, n, "WiFi back after %lu%ss", s, kNbsp);
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

int ota(char* o, size_t n, const char* d) {
  struct P { const char* text; const char* say; };
  const P k[] = {
    {kEvOtaStarted, "Update started"}, {kEvOtaInstalled, "Update installed"}, {kEvOtaAbandoned, "Update abandoned"},
    {kEvOtaFailed, "Update failed"}, {kEvOtaInterrupted, "Update interrupted"}, {kEvOtaRefused, "Update refused"},
    {kEvOtaWaiting, "New firmware running, being checked"}, {kEvOtaGood, "New firmware started properly"},
    {kEvOtaRolledBack, "Update rolled back"}, {kEvOtaSelfRollback, "New firmware didn't start properly in 4 min 30 s"},
    {kEvOtaRollbackTest, "Rollback test image"},
  };
  for (const P& p : k) {
    if (!starts(d, p.text)) continue;
    const char* rest = d + strlen(p.text);
    while (*rest == ':' || *rest == ' ') rest++;
    return rest[0] ? snprintf(o, n, "%s: %s", p.say, rest) : snprintf(o, n, "%s", p.say);
  }
  if (strstr(d, kEvOtaRefusedMore)) return labelled(o, n, "Updates refused", d);
  return labelled(o, n, "Update", d);
}

// "BLOWERS BOOST: spread ..." -> "Blowers to boost (full speed): spread ..."
int blowers(char* o, size_t n, const char* d) {
  struct P { const char* text; const char* say; };
  const P k[] = {
    {kEvBlowersBoost, "Blowers to boost (full speed)"}, {kEvBlowersIdle, "Blowers back to idle"},
    {kEvBlowersFullEnd, "Blowers' full speed ended"}, {kEvBlowersFull, "Blowers at full speed"},
  };
  for (const P& p : k) {
    if (!starts(d, p.text)) continue;
    const char* rest = d + strlen(p.text);
    while (*rest == ':' || *rest == ' ') rest++;
    if (!rest[0]) return snprintf(o, n, "%s", p.say);
    return p.text == kEvBlowersFullEnd ? snprintf(o, n, "%s %s", p.say, rest) : labelled(o, n, p.say, rest);
  }
  return labelled(o, n, "Blowers", d);
}

// "ION RUN START: 2 min (slot 12:00 UTC)" -> "Ion generator run started: 2 min (slot 12:00 UTC)"
int ion(char* o, size_t n, const char* d) {
  struct P { const char* text; const char* say; };
  const P k[] = {
    {kEvIonStart, "Ion generator run started"}, {kEvIonEnd, "Ion generator run ended"},
    {kEvIonCutShort, "Ion generator run cut short"}, {kEvIonSkipped, "Ion generator run skipped"},
    {kEvIonStopped, "Ion generator run stopped"},
    {kEvIonRecordFailed, "Ion generator's cap record not saved"}, {kEvIonRecordSaved, "Ion generator's cap record saved again"},
  };
  for (const P& p : k) {
    if (!starts(d, p.text)) continue;
    const char* rest = d + strlen(p.text);
    while (*rest == ':' || *rest == ' ') rest++;
    return labelled(o, n, p.say, rest);
  }
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
