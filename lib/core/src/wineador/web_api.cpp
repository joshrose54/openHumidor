#include "wineador/web_api.h"
#include "wineador/commands.h"
#include "wineador/sensor_table.h"
#include "wineador/event_text.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

namespace wineador {

static constexpr const char* kDeg = "\xC2\xB0";   // UTF-8 degree sign

// ---------------------------------------------------------------- endpoints
const Endpoint kEndpoints[] = {
  {Method::Get,  "/",                    Route::Page,          false},
  {Method::Get,  "/api/info",            Route::Info,          true},
  {Method::Get,  "/api/status",          Route::Status,        true},
  {Method::Get,  "/api/history",         Route::History,       true},
  {Method::Get,  "/api/events",          Route::Events,        true},
  {Method::Get,  "/api/settings",        Route::Settings,      true},
  {Method::Get,  "/api/logs",            Route::Logs,          true},
  {Method::Get,  "/api/logs/file",       Route::LogFile,       false},
  {Method::Get,  "/api/update",          Route::UpdateState,   true},
  {Method::Post, "/api/settings",        Route::SettingsWrite, true},
  {Method::Post, "/api/sensors/exclude", Route::Exclude,       true},
  {Method::Post, "/api/door-test",       Route::DoorTest,      true},
  {Method::Post, "/api/update",          Route::UpdateUpload,  true},
};
const int kEndpointCount = (int)(sizeof(kEndpoints) / sizeof(kEndpoints[0]));

const char* methodName(Method m) { return m == Method::Post ? "POST" : "GET"; }

const Endpoint* findEndpoint(Method m, const char* path, bool* pathKnown) {
  if (pathKnown) *pathKnown = false;
  for (int i = 0; i < kEndpointCount; i++) {
    if (strcmp(kEndpoints[i].path, path) != 0) continue;
    if (kEndpoints[i].method == m) return &kEndpoints[i];
    if (pathKnown) *pathKnown = true;
  }
  return nullptr;
}

// ---------------------------------------------------------------- refusals
struct RefusalInfo { int status; const char* code; const char* text; };
static const RefusalInfo kRefusals[(int)Refusal::Count] = {
  {200, "ok",                 "Done."},
  {404, "not_found",          "Not found."},
  {405, "method_not_allowed", "That request isn't allowed here."},
  {415, "not_json",           "Changes must be sent as JSON."},
  {400, "bad_json",           "The request isn't one flat JSON object."},
  {400, "empty",              "Nothing to change."},
  {400, "unknown_field",      "That can't be changed here."},
  {400, "read_only",          "Tuning settings are read only."},
  {400, "wrong_type",         "That value is the wrong kind (a number, on/off or text)."},
  {400, "out_of_bounds",      "Out of range."},
  {400, "off_step",           "That value isn't one of the setting's steps."},
  {400, "unknown_channel",    "No such sensor."},
  {400, "too_long",           "That text is too long."},
  {400, "bad_query",          "A value in the address is missing or wrong."},
  {400, "bad_path",           "Not a log file."},
  {409, "updating",           "Updating. Try again when it has finished."},
  {409, "compressor_running", "Compressor on. Try again when it stops."},
  {409, "already_updating",   "Already updating."},
  {403, "window_closed",      "Updates closed. Press Allow update on the cabinet screen."},
  {503, "busy",               "Board busy. Try again when the download or update has finished."},
  {503, "low_memory",         "Board short of memory. Try again in a minute."},
  {413, "too_big",            "File too big for this board."},
  {422, "wrong_image",        "Not a cabinet build."},
  {408, "stalled",            "Upload stalled. Nothing changed; try again."},
  {503, "card_unavailable",   "SD card not available."},
  {503, "no_answer",          "No answer from the board. Try again."},
  {409, "being_checked",      "Last update still being checked. Try again shortly."},
  {422, "corrupt_image",      "File damaged. Nothing changed; send it again."},
  {500, "flash_failed",       "Couldn't write the update. Nothing changed."},
  {429, "too_soon",           "Too soon. Try again shortly."},
  {400, "ion_too_much",       "The ion generator can run at most 10 min a day."},
};

int httpStatus(Refusal r) { return r < Refusal::Count ? kRefusals[(int)r].status : 500; }
const char* refusalCode(Refusal r) { return r < Refusal::Count ? kRefusals[(int)r].code : "error"; }
const char* refusalText(Refusal r) { return r < Refusal::Count ? kRefusals[(int)r].text : "Something went wrong."; }

ApiResult refuse(Refusal r, const char* field, const char* message) {
  ApiResult a;
  a.refusal = r;
  if (field) snprintf(a.field, sizeof(a.field), "%s", field);
  snprintf(a.message, sizeof(a.message), "%s", message ? message : refusalText(r));
  return a;
}

int formatError(char* out, size_t n, const ApiResult& r) {
  JsonWriter w;
  w.setBuffer(out, n > 0 ? n - 1 : 0);
  w.beginObject();
  w.kInt("api", kApiVersion);
  w.kStr("error", refusalCode(r.refusal));
  if (r.field[0]) w.kStr("field", r.field);
  w.kStr("message", r.message[0] ? r.message : refusalText(r.refusal));
  if (r.hasLimits) {
    w.kNum("min", r.min, 2);
    w.kNum("max", r.max, 2);
    w.kNum("step", r.step, 2);
  }
  if (r.image) w.kStr("image", r.image);
  w.endObject();
  if (w.overflowed() || n == 0) return -1;
  out[w.length()] = 0;
  return (int)w.length();
}

// ---------------------------------------------------------------- requests
bool isJsonContentType(const char* ct) {
  if (!ct) return false;
  while (*ct == ' ') ct++;
  const char* want = "application/json";
  const size_t k = strlen(want);
  for (size_t i = 0; i < k; i++) {
    char c = ct[i];
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (c != want[i]) return false;
  }
  const char after = ct[k];
  return after == 0 || after == ';' || after == ' ';
}

static int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool queryValue(const char* q, const char* name, char* out, size_t n) {
  if (!q || n == 0) return false;
  const size_t nl = strlen(name);
  const char* p = q;
  while (*p) {
    const char* amp = strchr(p, '&');
    const char* end = amp ? amp : p + strlen(p);
    const char* eq = (const char*)memchr(p, '=', (size_t)(end - p));
    const size_t kl = eq ? (size_t)(eq - p) : (size_t)(end - p);
    if (kl == nl && strncmp(p, name, nl) == 0) {
      size_t len = 0;
      for (const char* v = eq ? eq + 1 : end; v < end; v++) {
        char c = *v;
        if (c == '+') c = ' ';
        else if (c == '%' && end - v >= 3 && hexDigit(v[1]) >= 0 && hexDigit(v[2]) >= 0) {
          c = (char)(hexDigit(v[1]) * 16 + hexDigit(v[2]));
          v += 2;
        }
        if (c == 0 || len + 1 >= n) return false;
        out[len++] = c;
      }
      out[len] = 0;
      return true;
    }
    if (!amp) break;
    p = amp + 1;
  }
  return false;
}

// ---------------------------------------------------------------- writes
static ApiResult fromJsonError(const FlatJson& j) {
  switch (j.error()) {
    case FlatJson::Error::Empty:         return refuse(Refusal::BadJson, nullptr, "The request body is empty.");
    case FlatJson::Error::StringTooLong: return refuse(Refusal::TooLong);
    default: {
      char m[120];
      snprintf(m, sizeof(m), "The request isn't one flat JSON object: %s.", jsonErrorText(j.error()));
      return refuse(Refusal::BadJson, nullptr, m);
    }
  }
}

static ApiResult limits(Refusal r, const char* field, const char* message, float lo, float hi, float step) {
  ApiResult a = refuse(r, field, message);
  a.hasLimits = true;
  a.min = lo;
  a.max = hi;
  a.step = step;
  return a;
}

ApiResult parseWriteBody(const char* body, size_t n, FlatJson* j) {
  if (!j->parse(body, n)) return fromJsonError(*j);
  return ApiResult{};
}

ApiResult checkSettingsWrite(const char* body, size_t n, const UserSettings& now, bool updating, UserSettings* out) {
  FlatJson j;
  const ApiResult p = parseWriteBody(body, n, &j);
  return p.ok() ? checkSettingsFields(j, now, updating, out) : p;
}

ApiResult checkSettingsFields(const FlatJson& j, const UserSettings& now, bool updating, UserSettings* out) {
  if (updating) return refuse(Refusal::Updating);
  if (j.count() == 0) return refuse(Refusal::Empty);
  UserSettings u = now;
  char m[120];
  for (int i = 0; i < j.count(); i++) {
    const JsonField& f = j.at(i);
    if (strcmp(f.key, "target_temp_c") == 0 || strcmp(f.key, "rh_setpoint_pct") == 0) {
      const bool temp = f.key[0] == 't';
      if (f.kind != JsonField::Kind::Number) return refuse(Refusal::WrongType, f.key, "That setting takes a number.");
      const float lo = temp ? kTargetTempMinC : kRhSetpointMinPct, hi = temp ? kTargetTempMaxC : kRhSetpointMaxPct;
      const float st = temp ? kTargetTempStepC : kRhSetpointStepPct;
      const float v = (float)f.number;
      if (!(v >= lo && v <= hi)) {
        if (temp) snprintf(m, sizeof(m), "Target temperature: %.0f\xE2\x80\x93%.0f%sC.", (double)lo, (double)hi, kDeg);
        else      snprintf(m, sizeof(m), "Target humidity: %.0f\xE2\x80\x93%.0f%%.", (double)lo, (double)hi);
        return limits(Refusal::OutOfBounds, f.key, m, lo, hi, st);
      }
      if (!onStep(v, st)) {
        if (temp) snprintf(m, sizeof(m), "Target temperature goes in steps of %.1f%sC.", (double)st, kDeg);
        else      snprintf(m, sizeof(m), "Target humidity is a whole number.");
        return limits(Refusal::OffStep, f.key, m, lo, hi, st);
      }
      const bool ok = temp ? setTargetTemp(u, v) : setRhSetpoint(u, v);
      if (!ok) return limits(Refusal::OutOfBounds, f.key, nullptr, lo, hi, st);   // belt and braces
    } else if (strcmp(f.key, "heater_enable") == 0 || strcmp(f.key, "humidifier_enable") == 0 ||
               strcmp(f.key, "ion_enable") == 0) {
      if (f.kind != JsonField::Kind::Bool) return refuse(Refusal::WrongType, f.key, "That setting takes true or false.");
      if (f.key[0] == 'i')      u.ionEnable = f.boolean;
      else if (f.key[1] == 'e') u.heaterEnable = f.boolean;
      else                      u.humidifierEnable = f.boolean;
    } else if (strcmp(f.key, "ion_runs_per_day") == 0 || strcmp(f.key, "ion_minutes_per_run") == 0) {
      const bool runs = f.key[4] == 'r';
      const float hi = runs ? kIonRunsMax : kIonMinutesMax;
      if (f.kind != JsonField::Kind::Number) return refuse(Refusal::WrongType, f.key, "That setting takes a number.");
      if (!(f.number >= 0 && f.number <= hi)) {
        snprintf(m, sizeof(m), runs ? "Runs per day: 0\xE2\x80\x93%.0f." : "Minutes per run: 0\xE2\x80\x93%.0f.", (double)hi);
        return limits(Refusal::OutOfBounds, f.key, m, 0, hi, 1);
      }
      if (f.number != floor(f.number)) {
        return limits(Refusal::OffStep, f.key, runs ? "Runs per day is a whole number." :
                                                      "Minutes per run is a whole number.", 0, hi, 1);
      }
      if (runs) u.ionRunsPerDay = (uint8_t)f.number;
      else      u.ionMinutesPerRun = (uint8_t)f.number;
    } else if (isTuningName(f.key)) {
      return refuse(Refusal::ReadOnly, f.key);
    } else {
      return refuse(Refusal::UnknownField, f.key);   // safety values are never named, so "unknown"
    }
  }
  // 5.5 / 4.6c: runs x minutes at most 10, judged on the settings as they would be.
  if (!ionRunsOk(u.ionRunsPerDay, u.ionMinutesPerRun)) {
    snprintf(m, sizeof(m), "%u min a day. The limit is %.0f min.", (unsigned)(u.ionRunsPerDay * u.ionMinutesPerRun),
             (double)kIonMinutesPerDayMax);
    const JsonField* a = j.find("ion_minutes_per_run");
    return refuse(Refusal::IonTooMuch, a ? "ion_minutes_per_run" : "ion_runs_per_day", m);
  }
  *out = u;
  return ApiResult{};
}

ApiResult checkExcludeWrite(const char* body, size_t n, const UserSettings& now, bool updating, int* channel,
                            UserSettings* out) {
  FlatJson j;
  const ApiResult p = parseWriteBody(body, n, &j);
  return p.ok() ? checkExcludeFields(j, now, updating, channel, out) : p;
}

ApiResult checkExcludeFields(const FlatJson& j, const UserSettings& now, bool updating, int* channel, UserSettings* out) {
  if (updating) return refuse(Refusal::Updating);
  const JsonField* c = j.find("channel");
  const JsonField* e = j.find("excluded");
  for (int i = 0; i < j.count(); i++)
    if (&j.at(i) != c && &j.at(i) != e) return refuse(Refusal::UnknownField, j.at(i).key);
  if (!c || !e) return refuse(Refusal::Empty, c ? "excluded" : "channel", "Give both channel and excluded.");
  int ch = -2;
  if (c->kind == JsonField::Kind::Number) {
    if (c->number == floor(c->number) && c->number >= 0 && c->number < kNumSensors) ch = (int)c->number;
  } else if (c->kind == JsonField::Kind::String) {
    ch = channelWord(c->text);
  } else {
    return refuse(Refusal::WrongType, "channel", "channel is a number 0-5, a position name or a probe label.");
  }
  if (ch < 0 || ch >= kNumSensors) return refuse(Refusal::UnknownChannel, "channel");
  if (e->kind != JsonField::Kind::Bool) return refuse(Refusal::WrongType, "excluded", "excluded takes true or false.");
  UserSettings u = now;
  if (e->boolean) u.manualExcludeMask = (uint8_t)(u.manualExcludeMask | (1u << ch));
  else            u.manualExcludeMask = (uint8_t)(u.manualExcludeMask & ~(1u << ch));
  *channel = ch;
  *out = u;
  return ApiResult{};
}

ApiResult checkDoorTest(const char* body, size_t n, char* note, size_t noteSize) {
  if (noteSize) note[0] = 0;
  FlatJson j;
  const ApiResult p = parseWriteBody(body, n, &j);
  return p.ok() ? checkDoorTestFields(j, note, noteSize) : p;
}

ApiResult checkDoorTestFields(const FlatJson& j, char* note, size_t noteSize) {
  if (noteSize) note[0] = 0;
  for (int i = 0; i < j.count(); i++) {
    const JsonField& f = j.at(i);
    if (strcmp(f.key, "note") != 0) return refuse(Refusal::UnknownField, f.key);
    if (f.kind == JsonField::Kind::Null) continue;
    if (f.kind != JsonField::Kind::String) return refuse(Refusal::WrongType, "note", "note is text.");
    if (strlen(f.text) > kMaxDoorNote) return refuse(Refusal::TooLong, "note", "The note can be at most 120 characters.");
    for (const unsigned char* c = (const unsigned char*)f.text; *c; c++)
      if (*c < 0x20 || *c == 0x7F) return refuse(Refusal::WrongType, "note", "The note is one line of plain text.");
    snprintf(note, noteSize, "%s", f.text);
  }
  return ApiResult{};
}

// Appends to out (always NUL-terminated); *len is the text so far.
static void appendf(char* out, size_t n, size_t* len, const char* fmt, ...) __attribute__((format(printf, 4, 5)));
static void appendf(char* out, size_t n, size_t* len, const char* fmt, ...) {
  if (*len + 1 >= n) return;
  va_list a;
  va_start(a, fmt);
  const int k = vsnprintf(out + *len, n - *len, fmt, a);
  va_end(a);
  if (k > 0) *len += (size_t)k;
  if (*len >= n) *len = n - 1;
}

int describeUserChange(char* out, size_t n, const UserSettings& a, const UserSettings& b) {
  if (n == 0) return 0;
  out[0] = 0;
  size_t len = 0;
  auto sep = [&]() { if (len) appendf(out, n, &len, ", "); };
  auto onOff = [](bool x) { return x ? "on" : "off"; };
  if (a.targetTempC != b.targetTempC) {
    sep();
    appendf(out, n, &len, "target_temp_c %.1f%s%.1f", (double)a.targetTempC, kEvChangeArrow, (double)b.targetTempC);
  }
  if (a.rhSetpointPct != b.rhSetpointPct) {
    sep();
    appendf(out, n, &len, "rh_setpoint_pct %.0f%s%.0f", (double)a.rhSetpointPct, kEvChangeArrow, (double)b.rhSetpointPct);
  }
  if (a.heaterEnable != b.heaterEnable) {
    sep();
    appendf(out, n, &len, "heater_enable %s%s%s", onOff(a.heaterEnable), kEvChangeArrow, onOff(b.heaterEnable));
  }
  if (a.humidifierEnable != b.humidifierEnable) {
    sep();
    appendf(out, n, &len, "humidifier_enable %s%s%s", onOff(a.humidifierEnable), kEvChangeArrow, onOff(b.humidifierEnable));
  }
  if (a.ionEnable != b.ionEnable) {
    sep();
    appendf(out, n, &len, "ion_enable %s%s%s", onOff(a.ionEnable), kEvChangeArrow, onOff(b.ionEnable));
  }
  if (a.ionRunsPerDay != b.ionRunsPerDay) {
    sep();
    appendf(out, n, &len, "ion_runs_per_day %u%s%u", (unsigned)a.ionRunsPerDay, kEvChangeArrow, (unsigned)b.ionRunsPerDay);
  }
  if (a.ionMinutesPerRun != b.ionMinutesPerRun) {
    sep();
    appendf(out, n, &len, "ion_minutes_per_run %u%s%u", (unsigned)a.ionMinutesPerRun, kEvChangeArrow, (unsigned)b.ionMinutesPerRun);
  }
  for (int i = 0; i < kNumSensors; i++) {
    const bool x = a.manualExcludeMask & (1u << i), y = b.manualExcludeMask & (1u << i);
    if (x != y) {
      sep();
      appendf(out, n, &len, "ch%d %s manual exclude %s", i, kSensors[i].position, onOff(y));
    }
  }
  return (int)len;
}

// ---------------------------------------------------------------- write replies
static int finish(JsonWriter& w, char* out, size_t n) {
  if (w.overflowed() || n == 0) return -1;
  out[w.length()] = 0;
  return (int)w.length();
}

int formatExcludeReply(char* out, size_t n, int ch, bool excluded) {
  JsonWriter w;
  w.setBuffer(out, n > 0 ? n - 1 : 0);
  w.beginObject();
  w.kInt("api", kApiVersion);
  w.kInt("ch", ch);
  w.kStr("name", ch >= 0 && ch < kNumSensors ? kSensors[ch].position : "?");
  w.kStr("probe", ch >= 0 && ch < kNumSensors ? kSensors[ch].label : "?");
  w.kBool("excluded", excluded);
  w.endObject();
  return finish(w, out, n);
}

int formatDoorTestReply(char* out, size_t n) {
  JsonWriter w;
  w.setBuffer(out, n > 0 ? n - 1 : 0);
  w.beginObject();
  w.kInt("api", kApiVersion);
  w.kBool("logged", true);
  w.endObject();
  return finish(w, out, n);
}

int formatInstalledReply(char* out, size_t n, const char* version) {
  JsonWriter w;
  w.setBuffer(out, n > 0 ? n - 1 : 0);
  w.beginObject();
  w.kInt("api", kApiVersion);
  w.kBool("installed", true);
  w.kStr("version", version);
  w.kBool("restarting", true);
  w.endObject();
  return finish(w, out, n);
}

// ---------------------------------------------------------------- small replies
void writeBuild(JsonWriter& w, const BuildFacts& b) {
  w.key("build");
  w.beginObject();
  w.kStr("board", b.board);
  w.kStr("profile", b.profile == Profile::Bench ? "bench" : "real");
  w.kBool("test", b.testBuild);
  w.kStr("version", b.version);
  w.endObject();
}

int formatInfo(char* out, size_t n, const BuildFacts& b, uint64_t uptimeS, const char* bootReason) {
  JsonWriter w;
  w.setBuffer(out, n > 0 ? n - 1 : 0);
  w.beginObject();
  w.kInt("api", kApiVersion);
  w.kStr("name", "openHumidor");
  writeBuild(w, b);
  w.kInt("uptime_s", (int64_t)uptimeS);
  w.kStr("boot_reason", bootReason ? bootReason : "");
  w.endObject();
  if (w.overflowed() || n == 0) return -1;
  out[w.length()] = 0;
  return (int)w.length();
}

// The tuning table, in the spec 5.5 order. Shown read only; refused as a write.
int tuningItemCount() { return 39; }

TuningItem tuningItem(const TuningSettings& t, int i) {
  auto mins = [](uint32_t ms) { return ms / 60000.0; };
  auto secs = [](uint32_t ms) { return ms / 1000.0; };
  switch (i) {
    case 0:  return {"cooling_hysteresis_c", "Cooling starts at target +", "\xC2\xB0" "C", t.coolingHysteresisC, 1, false};
    case 1:  return {"heating_deadband_c", "Heating starts at target \xE2\x88\x92", "\xC2\xB0" "C", t.heatingDeadbandC, 1, false};
    case 2:  return {"heating_stop_below_c", "Heating stops at target \xE2\x88\x92", "\xC2\xB0" "C", t.heatingStopBelowSetpointC, 1, false};
    case 3:  return {"crossover_delay_min", "Changeover wait", "min", mins(t.crossoverDelayMs), 0, false};
    case 4:  return {"fan_idle_pct", "Fan speed, idle", "%", (double)t.fanIdlePct, 0, false};
    case 5:  return {"fan_compressor_pct", "Fan speed, compressor on", "%", (double)t.fanCompressorPct, 0, false};
    case 6:  return {"fan_cool_harvest_start_pct", "Fan speed, run-on after cooling, start", "%", (double)t.fanCoolHarvestStartPct, 0, false};
    case 7:  return {"fan_cool_harvest_end_pct", "Fan speed, run-on after cooling, end", "%", (double)t.fanCoolHarvestEndPct, 0, false};
    case 8:  return {"fan_heating_pct", "Fan speed, heating and run-on", "%", (double)t.fanHeatingPct, 0, false};
    case 9:  return {"harvest_convergence_c", "Run-on ends within", "\xC2\xB0" "C", t.harvestConvergenceC, 1, false};
    case 10: return {"harvest_backstop_min", "Run-on at most", "min", mins(t.harvestBackstopMs), 0, false};
    case 11: return {"dosed_control", "Timed cooling runs", "", t.dosedControl ? 1.0 : 0.0, 0, true};
    case 12: return {"rh_strip_enable", "Drying", "", t.rhStripEnable ? 1.0 : 0.0, 0, true};
    case 13: return {"rh_strip_offset_pct", "Drying starts at target +", "%", t.rhStripOffsetPct, 0, false};
    case 14: return {"rh_strip_persist_min", "Drying starts after", "min", mins(t.rhStripPersistMs), 0, false};
    case 15: return {"rh_strip_floor_c", "Drying stops below target \xE2\x88\x92", "\xC2\xB0" "C", t.rhStripFloorBelowSetpointC, 1, false};
    case 16: return {"humidify_band_pts", "Humidifying starts at target \xE2\x88\x92", "%", t.humidifyBandPct, 0, false};
    case 17: return {"humidify_persist_min", "Humidifying starts after", "min", mins(t.humidifyPersistMs), 0, false};
    case 18: return {"humidify_during_cooling", "Humidify while cooling", "", t.humidifyDuringCooling ? 1.0 : 0.0, 0, true};
    case 19: return {"heater_pwm_cap_pct", "Heater power limit", "%", (double)t.heaterPwmCapPct, 0, false};
    case 20: return {"heater_ineffective_rise_c", "Heater check: rise needed", "\xC2\xB0" "C", t.heaterIneffectiveRiseC, 1, false};
    case 21: return {"heater_ineffective_window_min", "Heater check: within", "min", mins(t.heaterIneffectiveWindowMs), 0, false};
    case 22: return {"heater_ineffective_retry_min", "Heater check: retry after", "min", mins(t.heaterIneffectiveRetryMs), 0, false};
    case 23: return {"cooling_ineffective_fall_c", "Cooling check: fall needed", "\xC2\xB0" "C", t.coolingIneffectiveFallC, 1, false};
    case 24: return {"cooling_ineffective_window_min", "Cooling check: within", "min", mins(t.coolingIneffectiveWindowMs), 0, false};
    case 25: return {"humid_ineffective_rise_pts", "Humidifier check: rise needed", "%", t.humidIneffectiveRisePct, 1, false};
    case 26: return {"humid_ineffective_window_min", "Humidifier check: within", "min", mins(t.humidIneffectiveWindowMs), 0, false};
    case 27: return {"humid_ineffective_retry_min", "Humidifier check: retry after", "min", mins(t.humidIneffectiveRetryMs), 0, false};
    case 28: return {"disturbance_detection", "Disturbance detection", "", t.disturbanceDetection ? 1.0 : 0.0, 0, true};
    case 29: return {"log_steady_s", "Log every, idle", "s", secs(t.logSteadyMs), 0, false};
    case 30: return {"log_active_s", "Log every, running", "s", secs(t.logActiveMs), 0, false};
    case 31: return {"heater_pwm_period_s", "Heater cycle", "s", secs(t.heaterPwmPeriodMs), 0, false};
    case 32: return {"sensor_read_s", "Sensors read every", "s", secs(t.sensorReadMs), 0, false};
    // Rev Q, 4.6b: the blowers (the idle duty is full speed until BP2 test 22 chooses it)
    case 33: return {"blower_idle_pct", "Blower speed, idle",
                     "%", (double)t.blowerIdlePct, 0, false};
    case 34: return {"blower_boost_above_c", "Blowers boost above", "\xC2\xB0" "C", t.blowerBoostAboveC, 1, false};
    case 35: return {"blower_boost_above_rh_pts", "or humidity spread above", "%", t.blowerBoostAboveRhPct, 0, false};
    case 36: return {"blower_idle_below_c", "Blowers idle below", "\xC2\xB0" "C", t.blowerIdleBelowC, 1, false};
    case 37: return {"blower_idle_below_rh_pts", "and humidity spread below", "%", t.blowerIdleBelowRhPct, 0, false};
    default: return {"blower_idle_hold_min", "for", "min", mins(t.blowerIdleHoldMs), 0, false};
  }
}

// Names refused as read-only: every tuning setting, by its name in the table above.
bool isTuningName(const char* name) {
  TuningSettings t;
  for (int i = 0; i < tuningItemCount(); i++)
    if (strcmp(tuningItem(t, i).name, name) == 0) return true;
  return false;
}

bool SettingsReply::step(JsonWriter& w) {
  // 0 open + user temp, 1 rh, 2 heater, 3 humidifier, 4 manual exclude, 5 tuning open,
  // 6.. one tuning item each, then close.
  const int nT = tuningItemCount();
  const int i = i_++;
  if (i == 0) {
    w.beginObject();
    w.kInt("api", kApiVersion);
    w.key("user");
    w.beginObject();
    w.key("target_temp_c");
    w.beginObject();
    w.kNum("value", u_.targetTempC, 1);
    w.kNum("min", kTargetTempMinC, 1);
    w.kNum("max", kTargetTempMaxC, 1);
    w.kNum("step", kTargetTempStepC, 1);
    w.kStr("unit", "\xC2\xB0" "C");
    w.endObject();
    return true;
  }
  if (i == 1) {
    w.key("rh_setpoint_pct");
    w.beginObject();
    w.kNum("value", u_.rhSetpointPct, 0);
    w.kNum("min", kRhSetpointMinPct, 0);
    w.kNum("max", kRhSetpointMaxPct, 0);
    w.kNum("step", kRhSetpointStepPct, 0);
    w.kStr("unit", "%");
    w.endObject();
    return true;
  }
  if (i == 2) { w.key("heater_enable"); w.beginObject(); w.kBool("value", u_.heaterEnable); w.endObject(); return true; }
  if (i == 3) {
    w.key("humidifier_enable");
    w.beginObject();
    w.kBool("value", u_.humidifierEnable);
    w.endObject();
    // 5.5 / 4.6c: off stops every ion run (the run settings keep their values)
    w.key("ion_enable");
    w.beginObject();
    w.kBool("value", u_.ionEnable);
    w.endObject();
    w.key("ion_runs_per_day");
    w.beginObject();
    w.kInt("value", u_.ionRunsPerDay);
    w.kInt("min", 0);
    w.kInt("max", kIonRunsMax);
    w.kInt("step", 1);
    w.endObject();
    w.key("ion_minutes_per_run");
    w.beginObject();
    w.kInt("value", u_.ionMinutesPerRun);
    w.kInt("min", 0);
    w.kInt("max", kIonMinutesMax);
    w.kInt("step", 1);
    w.kStr("unit", "min");
    w.endObject();
    w.kInt("ion_minutes_per_day_max", kIonMinutesPerDayMax);   // runs x minutes at most this (5.5)
    w.endObject();   // user
    return true;
  }
  if (i == 4) {
    w.key("manual_exclude");
    w.beginArray();
    for (int c = 0; c < kNumSensors; c++)
      if (u_.manualExcludeMask & (1u << c)) w.integer(c);
    w.endArray();
    return true;
  }
  if (i == 5) { w.key("tuning"); w.beginArray(); return true; }
  if (i < 6 + nT) {
    const TuningItem t = tuningItem(t_, i - 6);
    w.beginObject();
    w.kStr("name", t.name);
    w.kStr("label", t.label);
    if (t.onOff) w.kBool("value", t.value != 0);
    else         w.kNum("value", t.value, t.decimals);
    w.kStr("unit", t.unit);
    w.endObject();
    return true;
  }
  if (i == 6 + nT) {
    w.endArray();
    w.endObject();
    return true;
  }
  return false;
}

}  // namespace wineador
