#include "wineador/event_text.h"
#include "wineador/json_out.h"
#include <stdio.h>
#include <string.h>

namespace wineador {

#define WINEADOR_EV_TEXT_ROW(name, type, text, match) {#name, kEvType##type, text, EvMatch::match},
const EventText kEventTexts[] = {WINEADOR_EVENT_TEXTS(WINEADOR_EV_TEXT_ROW)};
#undef WINEADOR_EV_TEXT_ROW
const int kEventTextCount = (int)(sizeof(kEventTexts) / sizeof(kEventTexts[0]));

#define WINEADOR_EV_TYPE_ROW(name, text) {#name, text},
const EventType kEventTypes[] = {WINEADOR_EVENT_TYPES(WINEADOR_EV_TYPE_ROW)};
#undef WINEADOR_EV_TYPE_ROW
const int kEventTypeCount = (int)(sizeof(kEventTypes) / sizeof(kEventTypes[0]));

bool eventIs(const char* type, const char* detail, const EventText& t) {
  if (!type || !detail || strcmp(type, t.type) != 0) return false;
  if (t.match == EvMatch::Starts) return strncmp(detail, t.text, strlen(t.text)) == 0;
  return strstr(detail, t.text) != nullptr;
}

// Appends `s` to out (len so far); false once it doesn't fit.
static bool put(char* out, size_t n, size_t* len, const char* s) {
  const size_t k = strlen(s);
  if (*len + k + 1 > n) return false;
  memcpy(out + *len, s, k);
  *len += k;
  out[*len] = 0;
  return true;
}

// A JSON string ("..." with escapes) into piece.
static const char* quoted(char* piece, size_t n, const char* s) {
  JsonWriter w;
  w.setBuffer(piece, n - 1);
  w.str(s);
  piece[w.length()] = 0;
  return piece;
}

// One entry per line, so a change of wording shows plainly in a diff.
int formatEventTextJson(char* out, size_t n) {
  if (n == 0) return -1;
  size_t len = 0;
  out[0] = 0;
  char q[160];
  bool ok = put(out, n, &len, "{\n  \"comment\": \"Made by tools/make_event_text.py from "
                                "lib/core/src/wineador/event_text.h. Do not edit.\",\n  \"types\": {\n");
  for (int i = 0; i < kEventTypeCount && ok; i++) {
    ok = put(out, n, &len, "    ") && put(out, n, &len, quoted(q, sizeof(q), kEventTypes[i].name)) &&
         put(out, n, &len, ": ") && put(out, n, &len, quoted(q, sizeof(q), kEventTypes[i].type)) &&
         put(out, n, &len, i + 1 < kEventTypeCount ? ",\n" : "\n");
  }
  ok = ok && put(out, n, &len, "  },\n  \"texts\": {\n");
  for (int i = 0; i < kEventTextCount && ok; i++) {
    const EventText& t = kEventTexts[i];
    ok = put(out, n, &len, "    ") && put(out, n, &len, quoted(q, sizeof(q), t.name)) &&
         put(out, n, &len, ": {\"type\": ") && put(out, n, &len, quoted(q, sizeof(q), t.type)) &&
         put(out, n, &len, ", \"text\": ") && put(out, n, &len, quoted(q, sizeof(q), t.text)) &&
         put(out, n, &len, t.match == EvMatch::Starts ? ", \"match\": \"starts\"}" : ", \"match\": \"contains\"}") &&
         put(out, n, &len, i + 1 < kEventTextCount ? ",\n" : "\n");
  }
  ok = ok && put(out, n, &len, "  }\n}\n");
  return ok ? (int)len : -1;
}

int formatRecovered12v(char* out, size_t n, float belowV, uint32_t durationMs, float lowestV) {
  return snprintf(out, n, "%s: back above %.1f V after %lu ms; lowest %.2f V", kEvRecovered12v, (double)belowV,
                  (unsigned long)durationMs, (double)lowestV);
}

int formatWifiBack(char* out, size_t n, uint32_t seconds) {
  return snprintf(out, n, "%s %lu s", kEvWifiBack, (unsigned long)seconds);
}

int formatSdStressLine(char* out, size_t n, bool done, const SdStressFigures& f) {
  return snprintf(out, n,
                  "%s %lu kHz, retry %s: %lu of %lu writes, %lu failures, %lu retries (first attempt failed at: "
                  "folder %lu, open %lu, header %lu, data %lu, verify %lu); worst write %lu ms",
                  done ? kEvSdStressDone : "SD STRESS progress:", (unsigned long)f.khz, f.retry ? "on" : "off",
                  (unsigned long)f.writes, (unsigned long)f.target, (unsigned long)f.failures, (unsigned long)f.retries,
                  (unsigned long)f.atFolder, (unsigned long)f.atOpen, (unsigned long)f.atHeader, (unsigned long)f.atData,
                  (unsigned long)f.atVerify, (unsigned long)f.worstMs);
}

int formatSdStressChecks(char* out, size_t n, uint32_t sizeMismatches, uint32_t heapMin, uint32_t heapMinLargest,
                         uint32_t heapMinEver) {
  return snprintf(out, n,
                  "%s size mismatches %lu; heap lowest after a write %lu (largest block then %lu), lowest since boot "
                  "%lu",
                  kEvSdStressChecks, (unsigned long)sizeMismatches, (unsigned long)heapMin,
                  (unsigned long)heapMinLargest, (unsigned long)heapMinEver);
}

const char* loadEventType(int load) {
  return load == 0 ? kEvTypeCompressor : load == 1 ? kEvTypeHeater : kEvTypeHumidifier;
}

int formatLoadEvent(char* out, size_t n, int load, bool on, const char* why) {
  const char* t = load == 0 ? (on ? kEvCompressorOn : kEvCompressorOff)
                : load == 1 ? (on ? kEvHeaterOn : kEvHeaterOff)
                            : (on ? kEvHumidifierOn : kEvHumidifierOff);
  return snprintf(out, n, "%s (%s)", t, why ? why : "");
}

}  // namespace wineador
