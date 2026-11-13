// The dashboard's API, plain logic (spec 5.5, docs/api.md): the JSON writer and reader, what each
// write accepts and refuses (bounds, steps, all-or-nothing, read-only tuning, never a safety
// value, nothing while Updating), the JSON-only rule for writes, the query string, the settings
// and info replies, the endpoint list — and that docs/api.md and the firmware agree.
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <string>
#include <math.h>
#include "wineador/json_out.h"
#include "wineador/json_in.h"
#include "wineador/web_api.h"
#include "wineador/web_status.h"
#include "wineador/web_lists.h"
#include "wineador/history.h"
#include "wineador/image_identity.h"
#include "wineador/sensor_table.h"
#include "../json_check.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

static std::string writeWith(void (*fn)(JsonWriter&)) {
  static char b[2048];
  JsonWriter w;
  w.setBuffer(b, sizeof(b));
  fn(w);
  return std::string(b, w.length());
}

// ---------------------------------------------------------------- the writer
void test_writer_objects_arrays_and_commas() {
  const std::string s = writeWith([](JsonWriter& w) {
    w.beginObject();
    w.kInt("a", 1);
    w.key("b");
    w.beginArray();
    w.integer(1); w.integer(2);
    w.beginObject(); w.kBool("x", true); w.endObject();
    w.endArray();
    w.kNull("c");
    w.kStr("d", "hi");
    w.endObject();
  });
  TEST_ASSERT_EQUAL_STRING("{\"a\":1,\"b\":[1,2,{\"x\":true}],\"c\":null,\"d\":\"hi\"}", s.c_str());
}

void test_writer_numbers() {
  const std::string s = writeWith([](JsonWriter& w) {
    w.beginArray();
    w.num(20.5, 2); w.num(20.0, 1); w.num(-0.001, 2); w.num(NAN, 2); w.num(INFINITY, 1); w.num(19.875, 2);
    w.num(62, 0); w.num(0.2314, 3);
    w.endArray();
  });
  TEST_ASSERT_EQUAL_STRING("[20.5,20,0,null,null,19.88,62,0.231]", s.c_str());
}

void test_writer_escapes_and_utf8() {
  const std::string s = writeWith([](JsonWriter& w) {
    w.str("a\"b\\c\nd\te\x01 20.5 \xC2\xB0" "C");
  });
  TEST_ASSERT_EQUAL_STRING("\"a\\\"b\\\\c\\nd\\te\\u0001 20.5 \xC2\xB0" "C\"", s.c_str());
  TEST_ASSERT_TRUE(jsoncheck::valid(s));
}

void test_writer_overflow_is_flagged() {
  char b[8];
  JsonWriter w;
  w.setBuffer(b, sizeof(b));
  w.str("much too long for eight bytes");
  TEST_ASSERT_TRUE(w.overflowed());
  TEST_ASSERT_TRUE(w.length() <= sizeof(b));
}

// ---------------------------------------------------------------- the reader
void test_reader_takes_one_flat_object() {
  FlatJson j;
  const char* b = " { \"target_temp_c\" : 20.5, \"heater_enable\":false, \"note\":\"door \\u00b0 \\\"x\\\"\", \"z\":null } ";
  TEST_ASSERT_TRUE(j.parse(b, strlen(b)));
  TEST_ASSERT_EQUAL_INT(4, j.count());
  TEST_ASSERT_EQUAL_FLOAT(20.5f, (float)j.find("target_temp_c")->number);
  TEST_ASSERT_TRUE(j.find("heater_enable")->kind == JsonField::Kind::Bool);
  TEST_ASSERT_FALSE(j.find("heater_enable")->boolean);
  TEST_ASSERT_EQUAL_STRING("door \xC2\xB0 \"x\"", j.find("note")->text);
  TEST_ASSERT_TRUE(j.find("z")->kind == JsonField::Kind::Null);
  TEST_ASSERT_NULL(j.find("missing"));
  TEST_ASSERT_TRUE(j.parse("{}", 2));
  TEST_ASSERT_EQUAL_INT(0, j.count());
}

void test_reader_refuses_everything_else() {
  struct Case { const char* body; FlatJson::Error e; };
  const Case cases[] = {
    {"", FlatJson::Error::Empty},
    {"   ", FlatJson::Error::Empty},
    {"[1]", FlatJson::Error::NotAnObject},
    {"20.5", FlatJson::Error::NotAnObject},
    {"{\"a\":{\"b\":1}}", FlatJson::Error::Nested},
    {"{\"a\":[1]}", FlatJson::Error::Nested},
    {"{\"a\":1,\"a\":2}", FlatJson::Error::DuplicateKey},
    {"{\"a\":01}", FlatJson::Error::Syntax},
    {"{\"a\":1.}", FlatJson::Error::Syntax},
    {"{\"a\":.5}", FlatJson::Error::Syntax},
    {"{\"a\":+1}", FlatJson::Error::Syntax},
    {"{\"a\":1,}", FlatJson::Error::Syntax},
    {"{a:1}", FlatJson::Error::Syntax},
    {"{\"a\":1} x", FlatJson::Error::Syntax},
    {"{\"a\":tru}", FlatJson::Error::Syntax},
    {"{\"a\":\"\\x\"}", FlatJson::Error::Syntax},
    {"{\"a\":\"unterminated}", FlatJson::Error::Syntax},
    {"{\"a\":1", FlatJson::Error::Syntax},
    {"{\"a\":1,\"b\":2,\"c\":3,\"d\":4,\"e\":5,\"f\":6,\"g\":7,\"h\":8,\"i\":9}", FlatJson::Error::TooManyFields},
    {"{\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\":1}", FlatJson::Error::KeyTooLong},
  };
  for (const Case& c : cases) {
    FlatJson j;
    TEST_ASSERT_FALSE_MESSAGE(j.parse(c.body, strlen(c.body)), c.body);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)c.e, (int)j.error(), c.body);
  }
  // a NUL inside the body is not valid JSON
  FlatJson j;
  const char withNul[] = "{\"a\":\"x\0y\"}";
  TEST_ASSERT_FALSE(j.parse(withNul, sizeof(withNul) - 1));
  // longer than the limit
  std::string big = "{\"note\":\"" + std::string(kMaxJsonBody, 'x') + "\"}";
  TEST_ASSERT_FALSE(j.parse(big.data(), big.size()));
  TEST_ASSERT_EQUAL_INT((int)FlatJson::Error::TooLong, (int)j.error());
}

// ---------------------------------------------------------------- requests
void test_writes_must_say_json() {
  TEST_ASSERT_TRUE(isJsonContentType("application/json"));
  TEST_ASSERT_TRUE(isJsonContentType("application/json; charset=utf-8"));
  TEST_ASSERT_TRUE(isJsonContentType("Application/JSON"));
  TEST_ASSERT_FALSE(isJsonContentType("text/plain"));
  TEST_ASSERT_FALSE(isJsonContentType("application/x-www-form-urlencoded"));
  TEST_ASSERT_FALSE(isJsonContentType("multipart/form-data; boundary=x"));
  TEST_ASSERT_FALSE(isJsonContentType("application/jsonx"));
  TEST_ASSERT_FALSE(isJsonContentType(""));
  TEST_ASSERT_FALSE(isJsonContentType(nullptr));
}

void test_query_values() {
  char v[48];
  TEST_ASSERT_TRUE(queryValue("range=day&since=1700000000", "range", v, sizeof(v)));
  TEST_ASSERT_EQUAL_STRING("day", v);
  TEST_ASSERT_TRUE(queryValue("range=day&since=1700000000", "since", v, sizeof(v)));
  TEST_ASSERT_EQUAL_STRING("1700000000", v);
  TEST_ASSERT_FALSE(queryValue("range=day", "since", v, sizeof(v)));
  TEST_ASSERT_FALSE(queryValue("ranges=day", "range", v, sizeof(v)));
  TEST_ASSERT_TRUE(queryValue("path=%2Flogs%2F2026%2F2026-11-events-s2.csv%4012", "path", v, sizeof(v)));
  TEST_ASSERT_EQUAL_STRING("/logs/2026/2026-11-events-s2.csv@12", v);
  TEST_ASSERT_TRUE(queryValue("a=b+c", "a", v, sizeof(v)));
  TEST_ASSERT_EQUAL_STRING("b c", v);
  TEST_ASSERT_FALSE(queryValue("a=%00", "a", v, sizeof(v)));          // no NULs smuggled in
  TEST_ASSERT_FALSE(queryValue("a=0123456789", "a", v, 5));           // too long for out
  TEST_ASSERT_FALSE(queryValue(nullptr, "a", v, sizeof(v)));
}

void test_endpoints() {
  bool known = false;
  const Endpoint* e = findEndpoint(Method::Get, "/api/status", &known);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_TRUE(e->route == Route::Status);
  TEST_ASSERT_NULL(findEndpoint(Method::Post, "/api/status", &known));
  TEST_ASSERT_TRUE(known);                                   // the path exists: 405
  TEST_ASSERT_NULL(findEndpoint(Method::Get, "/api/nothing", &known));
  TEST_ASSERT_FALSE(known);                                  // 404
  TEST_ASSERT_TRUE(findEndpoint(Method::Post, "/api/update")->route == Route::UpdateUpload);
  TEST_ASSERT_TRUE(findEndpoint(Method::Get, "/api/update")->route == Route::UpdateState);
  TEST_ASSERT_EQUAL_INT(13, kEndpointCount);
}

// ---------------------------------------------------------------- settings writes
static ApiResult settingsWrite(const char* body, UserSettings* out, bool updating = false) {
  UserSettings now;
  *out = now;
  return checkSettingsWrite(body, strlen(body), now, updating, out);
}

void test_settings_write_accepts_any_of_the_four() {
  UserSettings u;
  ApiResult r = settingsWrite("{\"target_temp_c\":20.5}", &u);
  TEST_ASSERT_TRUE(r.ok());
  TEST_ASSERT_EQUAL_FLOAT(20.5f, u.targetTempC);
  TEST_ASSERT_EQUAL_FLOAT(62.0f, u.rhSetpointPct);
  r = settingsWrite("{\"rh_setpoint_pct\":65,\"heater_enable\":false,\"humidifier_enable\":false,\"target_temp_c\":15}", &u);
  TEST_ASSERT_TRUE(r.ok());
  TEST_ASSERT_EQUAL_FLOAT(65.0f, u.rhSetpointPct);
  TEST_ASSERT_EQUAL_FLOAT(15.0f, u.targetTempC);
  TEST_ASSERT_FALSE(u.heaterEnable);
  TEST_ASSERT_FALSE(u.humidifierEnable);
  r = settingsWrite("{\"target_temp_c\":22,\"rh_setpoint_pct\":55}", &u);   // the bounds themselves
  TEST_ASSERT_TRUE(r.ok());
  r = settingsWrite("{\"target_temp_c\":2.15e1}", &u);
  TEST_ASSERT_TRUE(r.ok());
  TEST_ASSERT_EQUAL_FLOAT(21.5f, u.targetTempC);
}

void test_settings_write_refusals() {
  UserSettings u;
  ApiResult r = settingsWrite("{\"target_temp_c\":22.5}", &u);
  TEST_ASSERT_TRUE(r.refusal == Refusal::OutOfBounds);
  TEST_ASSERT_EQUAL_STRING("target_temp_c", r.field);
  TEST_ASSERT_TRUE(r.hasLimits);
  TEST_ASSERT_EQUAL_FLOAT(15.0f, r.min);
  TEST_ASSERT_EQUAL_FLOAT(22.0f, r.max);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, r.step);
  TEST_ASSERT_EQUAL_INT(400, httpStatus(r.refusal));
  TEST_ASSERT_TRUE(settingsWrite("{\"target_temp_c\":14.5}", &u).refusal == Refusal::OutOfBounds);
  TEST_ASSERT_TRUE(settingsWrite("{\"target_temp_c\":20.3}", &u).refusal == Refusal::OffStep);
  TEST_ASSERT_TRUE(settingsWrite("{\"rh_setpoint_pct\":62.5}", &u).refusal == Refusal::OffStep);
  TEST_ASSERT_TRUE(settingsWrite("{\"rh_setpoint_pct\":71}", &u).refusal == Refusal::OutOfBounds);
  TEST_ASSERT_TRUE(settingsWrite("{\"rh_setpoint_pct\":\"62\"}", &u).refusal == Refusal::WrongType);
  TEST_ASSERT_TRUE(settingsWrite("{\"heater_enable\":1}", &u).refusal == Refusal::WrongType);
  TEST_ASSERT_TRUE(settingsWrite("{\"heater_enable\":null}", &u).refusal == Refusal::WrongType);
  TEST_ASSERT_TRUE(settingsWrite("{}", &u).refusal == Refusal::Empty);
  TEST_ASSERT_TRUE(settingsWrite("not json", &u).refusal == Refusal::BadJson);
  TEST_ASSERT_TRUE(settingsWrite("{\"target\":20}", &u).refusal == Refusal::UnknownField);
  // tuning settings are read only; safety values are never named, so simply unknown
  r = settingsWrite("{\"cooling_hysteresis_c\":0.4}", &u);
  TEST_ASSERT_TRUE(r.refusal == Refusal::ReadOnly);
  TEST_ASSERT_EQUAL_STRING("cooling_hysteresis_c", r.field);
  TEST_ASSERT_TRUE(settingsWrite("{\"log_active_s\":15}", &u).refusal == Refusal::ReadOnly);
  TEST_ASSERT_TRUE(settingsWrite("{\"compressor_min_off_ms\":0}", &u).refusal == Refusal::UnknownField);
  TEST_ASSERT_TRUE(settingsWrite("{\"rh_hard_ceiling_pct\":90}", &u).refusal == Refusal::UnknownField);
  // refused while Updating (5.5), as on the LCD
  r = settingsWrite("{\"target_temp_c\":20.5}", &u, true);
  TEST_ASSERT_TRUE(r.refusal == Refusal::Updating);
  TEST_ASSERT_EQUAL_INT(409, httpStatus(r.refusal));
}

void test_settings_write_is_all_or_nothing() {
  UserSettings now;
  now.targetTempC = 19.0f;
  UserSettings out = now;
  const char* b = "{\"target_temp_c\":20.5,\"heater_enable\":false,\"rh_setpoint_pct\":99}";
  const ApiResult r = checkSettingsWrite(b, strlen(b), now, false, &out);
  TEST_ASSERT_TRUE(r.refusal == Refusal::OutOfBounds);
  TEST_ASSERT_EQUAL_FLOAT(19.0f, out.targetTempC);   // nothing applied
  TEST_ASSERT_TRUE(out.heaterEnable);
}

// ---------------------------------------------------------------- exclude and door test
void test_exclude_write() {
  UserSettings now, out;
  int ch = -1;
  const char* bodies[] = {"{\"channel\":3,\"excluded\":true}", "{\"channel\":\"mid_rear\",\"excluded\":true}",
                          "{\"channel\":\"ch3\",\"excluded\":true}"};
  for (const char* b : bodies) {
    ApiResult r = checkExcludeWrite(b, strlen(b), now, false, &ch, &out);
    TEST_ASSERT_TRUE_MESSAGE(r.ok(), b);
    TEST_ASSERT_EQUAL_INT(3, ch);
    TEST_ASSERT_EQUAL_HEX8(0x08, out.manualExcludeMask);
  }
  now.manualExcludeMask = 0x09;
  const char* off = "{\"excluded\":false,\"channel\":0}";
  TEST_ASSERT_TRUE(checkExcludeWrite(off, strlen(off), now, false, &ch, &out).ok());
  TEST_ASSERT_EQUAL_HEX8(0x08, out.manualExcludeMask);
  struct Case { const char* b; Refusal r; };
  const Case bad[] = {
    {"{\"channel\":6,\"excluded\":true}", Refusal::UnknownChannel},
    {"{\"channel\":-1,\"excluded\":true}", Refusal::UnknownChannel},
    {"{\"channel\":2.5,\"excluded\":true}", Refusal::UnknownChannel},
    {"{\"channel\":\"attic\",\"excluded\":true}", Refusal::UnknownChannel},
    {"{\"channel\":true,\"excluded\":true}", Refusal::WrongType},
    {"{\"channel\":3,\"excluded\":\"yes\"}", Refusal::WrongType},
    {"{\"channel\":3}", Refusal::Empty},
    {"{\"channel\":3,\"excluded\":true,\"why\":\"x\"}", Refusal::UnknownField},
  };
  for (const Case& c : bad) {
    const ApiResult r = checkExcludeWrite(c.b, strlen(c.b), now, false, &ch, &out);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)c.r, (int)r.refusal, c.b);
  }
  const char* ok = "{\"channel\":3,\"excluded\":true}";
  TEST_ASSERT_TRUE(checkExcludeWrite(ok, strlen(ok), now, true, &ch, &out).refusal == Refusal::Updating);
}

void test_exclude_by_probe_label() {
  // the probe label of channel 3, from the firmware's own table
  char b[96];
  snprintf(b, sizeof(b), "{\"channel\":\"%s\",\"excluded\":true}", kSensors[3].label);
  UserSettings now, out;
  int ch = -1;
  TEST_ASSERT_TRUE(checkExcludeWrite(b, strlen(b), now, false, &ch, &out).ok());
  TEST_ASSERT_EQUAL_INT(3, ch);
}

void test_door_test() {
  char note[160];
  TEST_ASSERT_TRUE(checkDoorTest("{}", 2, note, sizeof(note)).ok());
  TEST_ASSERT_EQUAL_STRING("", note);
  const char* b = "{\"note\":\"opened 30 s\"}";
  TEST_ASSERT_TRUE(checkDoorTest(b, strlen(b), note, sizeof(note)).ok());
  TEST_ASSERT_EQUAL_STRING("opened 30 s", note);
  TEST_ASSERT_TRUE(checkDoorTest("{\"note\":null}", 13, note, sizeof(note)).ok());
  const std::string longNote = "{\"note\":\"" + std::string(121, 'x') + "\"}";
  TEST_ASSERT_TRUE(checkDoorTest(longNote.data(), longNote.size(), note, sizeof(note)).refusal == Refusal::TooLong);
  const std::string maxNote = "{\"note\":\"" + std::string(120, 'x') + "\"}";
  TEST_ASSERT_TRUE(checkDoorTest(maxNote.data(), maxNote.size(), note, sizeof(note)).ok());
  TEST_ASSERT_TRUE(checkDoorTest("{\"note\":5}", 10, note, sizeof(note)).refusal == Refusal::WrongType);
  const char* ctl = "{\"note\":\"a\\u0001b\"}";      // a control character, sent escaped
  TEST_ASSERT_TRUE(checkDoorTest(ctl, strlen(ctl), note, sizeof(note)).refusal == Refusal::WrongType);
  const char* nl = "{\"note\":\"two\\nlines\"}";
  TEST_ASSERT_TRUE(checkDoorTest(nl, strlen(nl), note, sizeof(note)).refusal == Refusal::WrongType);
  TEST_ASSERT_TRUE(checkDoorTest("{\"when\":5}", 10, note, sizeof(note)).refusal == Refusal::UnknownField);
}

void test_settings_change_description() {
  UserSettings a, b;
  b.targetTempC = 20.5f;
  b.heaterEnable = false;
  b.manualExcludeMask = 0x08;
  char t[200];
  describeUserChange(t, sizeof(t), a, b);
  TEST_ASSERT_EQUAL_STRING("target_temp_c 20.0 -> 20.5, heater_enable on -> off, ch3 mid_rear manual exclude on", t);
  describeUserChange(t, sizeof(t), a, a);
  TEST_ASSERT_EQUAL_STRING("", t);
}

// ---------------------------------------------------------------- refusals and small replies
void test_every_refusal_has_a_status_code_and_words() {
  for (int i = 1; i < (int)Refusal::Count; i++) {
    const Refusal r = (Refusal)i;
    TEST_ASSERT_TRUE(httpStatus(r) >= 400 && httpStatus(r) < 600);
    TEST_ASSERT_TRUE(strlen(refusalText(r)) > 10);
    for (int j = 1; j < i; j++) TEST_ASSERT_TRUE(strcmp(refusalCode(r), refusalCode((Refusal)j)) != 0);
  }
  char out[400];
  ApiResult r = refuse(Refusal::OffStep, "target_temp_c", "Target temperature goes in steps of 0.5 \xC2\xB0" "C.");
  r.hasLimits = true; r.min = 15; r.max = 22; r.step = 0.5f;
  TEST_ASSERT_TRUE(formatError(out, sizeof(out), r) > 0);
  TEST_ASSERT_TRUE(jsoncheck::valid(out, strlen(out)));
  TEST_ASSERT_EQUAL_STRING("{\"api\":1,\"error\":\"off_step\",\"field\":\"target_temp_c\",\"message\":\"Target "
                           "temperature goes in steps of 0.5 \xC2\xB0" "C.\",\"min\":15,\"max\":22,\"step\":0.5}", out);
  TEST_ASSERT_EQUAL_INT(-1, formatError(out, 10, r));
}

void test_info_reply() {
  BuildFacts b;
  b.version = "2.0.0";
  char out[300];
  TEST_ASSERT_TRUE(formatInfo(out, sizeof(out), b, 3725, "POWERON") > 0);
  TEST_ASSERT_EQUAL_STRING("{\"api\":1,\"name\":\"openHumidor\",\"build\":{\"board\":\"rev-q\",\"profile\":\"real\","
                           "\"test\":false,\"version\":\"2.0.0\"},\"uptime_s\":3725,\"boot_reason\":\"POWERON\"}", out);
}

void test_the_write_replies() {
  char out[200], want[200];
  TEST_ASSERT_TRUE(formatExcludeReply(out, sizeof(out), 3, true) > 0);
  snprintf(want, sizeof(want), "{\"api\":1,\"ch\":3,\"name\":\"%s\",\"probe\":\"%s\",\"excluded\":true}",
           kSensors[3].position, kSensors[3].label);
  TEST_ASSERT_EQUAL_STRING(want, out);
  TEST_ASSERT_TRUE(formatDoorTestReply(out, sizeof(out)) > 0);
  TEST_ASSERT_EQUAL_STRING("{\"api\":1,\"logged\":true}", out);
  TEST_ASSERT_TRUE(formatInstalledReply(out, sizeof(out), "2.0.0") > 0);
  TEST_ASSERT_EQUAL_STRING("{\"api\":1,\"installed\":true,\"version\":\"2.0.0\",\"restarting\":true}", out);
}

void test_settings_reply_shows_user_and_tuning_never_safety() {
  UserSettings u;
  u.manualExcludeMask = 0x22;
  TuningSettings t;
  SettingsReply a(u, t), b(u, t);
  const std::string whole = jsoncheck::whole(a);
  const std::string pieces = jsoncheck::inPieces(b, b.maxStep() + 1);
  TEST_ASSERT_TRUE(jsoncheck::valid(whole));
  TEST_ASSERT_EQUAL_STRING(whole.c_str(), pieces.c_str());
  TEST_ASSERT_TRUE(whole.find("\"target_temp_c\":{\"value\":20,\"min\":15,\"max\":22,\"step\":0.5") != std::string::npos);
  TEST_ASSERT_TRUE(whole.find("\"rh_setpoint_pct\":{\"value\":62,\"min\":55,\"max\":70,\"step\":1") != std::string::npos);
  TEST_ASSERT_TRUE(whole.find("\"manual_exclude\":[1,5]") != std::string::npos);
  TEST_ASSERT_EQUAL_INT(tuningItemCount(), jsoncheck::count(whole, "\"label\":"));
  TEST_ASSERT_TRUE(whole.find("\"name\":\"crossover_delay_min\",\"label\":\"Crossover delay\",\"value\":10") != std::string::npos);
  // no safety value appears (5.5): none of the safety names, nor the word
  const char* safety[] = {"min_off", "min_on", "max_run", "ceiling", "boot_hold", "safety", "watchdog", "kickstart"};
  for (const char* s : safety) TEST_ASSERT_TRUE_MESSAGE(whole.find(s) == std::string::npos, s);
  // every tuning name is refused as a write
  for (int i = 0; i < tuningItemCount(); i++) TEST_ASSERT_TRUE(isTuningName(tuningItem(t, i).name));
}

// ---------------------------------------------------------------- docs/api.md and the firmware
static std::string readDoc() {
  FILE* f = fopen("docs/api.md", "rb");
  if (!f) return "";
  std::string s;
  char b[4096];
  size_t k;
  while ((k = fread(b, 1, sizeof(b), f)) > 0) s.append(b, k);
  fclose(f);
  return s;
}

void test_api_document_matches_the_firmware() {
  const std::string doc = readDoc();
  TEST_ASSERT_TRUE_MESSAGE(!doc.empty(), "docs/api.md not found (run the tests from the project folder)");
  // the version
  char v[32];
  snprintf(v, sizeof(v), "API version %d", kApiVersion);
  TEST_ASSERT_TRUE_MESSAGE(doc.find(v) != std::string::npos, "docs/api.md doesn't say the firmware's API version");
  for (int other = 0; other < 100; other++) {
    if (other == kApiVersion) continue;
    char o[32];
    snprintf(o, sizeof(o), "API version %d\n", other);
    TEST_ASSERT_TRUE_MESSAGE(doc.find(o) == std::string::npos, "docs/api.md names another API version");
  }
  // every endpoint is documented ...
  for (int i = 0; i < kEndpointCount; i++) {
    char e[64];
    snprintf(e, sizeof(e), "`%s %s`", methodName(kEndpoints[i].method), kEndpoints[i].path);
    TEST_ASSERT_TRUE_MESSAGE(doc.find(e) != std::string::npos, e);
  }
  // ... and every endpoint the document names exists
  for (const char* m : {"`GET /", "`POST /"}) {
    for (size_t at = doc.find(m); at != std::string::npos; at = doc.find(m, at + 1)) {
      const size_t end = doc.find('`', at + 1);
      std::string ep = doc.substr(at + 1, end - at - 1);           // "GET /api/status"
      const size_t q = ep.find('?');
      if (q != std::string::npos) ep = ep.substr(0, q);
      const bool post = ep[0] == 'P';
      const std::string path = ep.substr(post ? 5 : 4);
      TEST_ASSERT_NOT_NULL_MESSAGE(findEndpoint(post ? Method::Post : Method::Get, path.c_str()), ep.c_str());
    }
  }
  // every refusal code, image verdict, history column and update result is documented
  for (int i = 1; i < (int)Refusal::Count; i++) {
    char c[40];
    snprintf(c, sizeof(c), "`%s`", refusalCode((Refusal)i));
    TEST_ASSERT_TRUE_MESSAGE(doc.find(c) != std::string::npos, c);
  }
  for (int i = 1; i <= (int)ImageVerdict::TestBuild; i++) {
    char c[40];
    snprintf(c, sizeof(c), "`%s`", imageVerdictCode((ImageVerdict)i));
    TEST_ASSERT_TRUE_MESSAGE(doc.find(c) != std::string::npos, c);
  }
  for (int i = 0; i < kHistFields; i++) {
    char c[40];
    snprintf(c, sizeof(c), "`%s`", histFieldName(i));
    TEST_ASSERT_TRUE_MESSAGE(doc.find(c) != std::string::npos, c);
  }
  for (int i = 0; i <= (int)UpdateResult::Failed; i++) {
    char c[40];
    snprintf(c, sizeof(c), "`%s`", updateResultKey((UpdateResult)i));
    TEST_ASSERT_TRUE_MESSAGE(doc.find(c) != std::string::npos, c);
  }
  for (int m = 0; m <= (int)ModeIcon::Fault; m++) {
    char c[40];
    snprintf(c, sizeof(c), "`%s`", modeKey((ModeIcon)m));
    TEST_ASSERT_TRUE_MESSAGE(doc.find(c) != std::string::npos, c);
  }
  TuningSettings t;
  for (int i = 0; i < tuningItemCount(); i++) {
    char c[48];
    snprintf(c, sizeof(c), "`%s`", tuningItem(t, i).name);
    TEST_ASSERT_TRUE_MESSAGE(doc.find(c) != std::string::npos, c);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_writer_objects_arrays_and_commas);
  RUN_TEST(test_writer_numbers);
  RUN_TEST(test_writer_escapes_and_utf8);
  RUN_TEST(test_writer_overflow_is_flagged);
  RUN_TEST(test_reader_takes_one_flat_object);
  RUN_TEST(test_reader_refuses_everything_else);
  RUN_TEST(test_writes_must_say_json);
  RUN_TEST(test_query_values);
  RUN_TEST(test_endpoints);
  RUN_TEST(test_settings_write_accepts_any_of_the_four);
  RUN_TEST(test_settings_write_refusals);
  RUN_TEST(test_settings_write_is_all_or_nothing);
  RUN_TEST(test_exclude_write);
  RUN_TEST(test_exclude_by_probe_label);
  RUN_TEST(test_door_test);
  RUN_TEST(test_settings_change_description);
  RUN_TEST(test_every_refusal_has_a_status_code_and_words);
  RUN_TEST(test_info_reply);
  RUN_TEST(test_the_write_replies);
  RUN_TEST(test_settings_reply_shows_user_and_tuning_never_safety);
  RUN_TEST(test_api_document_matches_the_firmware);
  return UNITY_END();
}
