#pragma once
// The dashboard's JSON API (spec 5.5), the plain-logic half: the version, the list of
// endpoints, what each write accepts and refuses, and the small replies (info, settings,
// errors). docs/api.md documents every endpoint; test_web_api fails if the document and this
// file disagree. The web server (src/, stage Q5c) and the Mac review server (Q5b) both use this
// code, so the board and the review answer alike.
//
// Writes never change anything here: a check turns a request body into the new user settings
// (or a refusal); the control loop applies it on its next pass and logs the event.

#include <stdint.h>
#include <stddef.h>
#include "wineador/settings.h"
#include "wineador/json_in.h"
#include "wineador/json_out.h"

namespace wineador {

constexpr int kApiVersion = 2;                 // docs/api.md "API version 2"; changes with any endpoint
constexpr int kMaxBrowserConnections = 2;      // spec 5.5
constexpr size_t kMaxDoorNote = 120;           // the door-test note, in bytes (docs/api.md)

// ---------------------------------------------------------------- endpoints
enum class Method : uint8_t { Get, Post };
enum class Route : uint8_t {
  Page, Info, Status, History, Events, Settings, Logs, LogFile, UpdateState,
  SettingsWrite, Exclude, DoorTest, UpdateUpload,
  Count
};
struct Endpoint {
  Method method;
  const char* path;
  Route route;
  bool json;      // the reply is JSON (the page and the log files are not)
};
extern const Endpoint kEndpoints[];
extern const int kEndpointCount;
const char* methodName(Method m);
// The endpoint for a method and path (the path without its ?query). nullptr: no such path.
// *pathKnown is set when the path exists for another method (405 rather than 404).
const Endpoint* findEndpoint(Method m, const char* path, bool* pathKnown = nullptr);

// ---------------------------------------------------------------- refusals
// Every reason a request can be refused, with its HTTP status (docs/api.md, "Errors").
enum class Refusal : uint8_t {
  None,
  NotFound,          // 404 no such endpoint, or no such log file
  MethodNotAllowed,  // 405
  NotJson,           // 415 a write not sent as application/json
  BadJson,           // 400 not one flat JSON object
  Empty,             // 400 no fields
  UnknownField,      // 400 not a field this endpoint takes
  ReadOnly,          // 400 a tuning setting (changed only on the B6 tuning dashboard)
  WrongType,         // 400 e.g. text where a number belongs
  OutOfBounds,       // 400 outside the setting's bounds
  OffStep,           // 400 not a whole number of the setting's steps
  UnknownChannel,    // 400
  TooLong,           // 400 a text value too long
  BadQuery,          // 400 a missing or wrong query value
  BadPath,           // 400 not a log file path
  Updating,          // 409 an OTA update is running
  CompressorRunning, // 409 OTA: an update never cuts a cooling run short (5.2)
  AlreadyUpdating,   // 409
  WindowClosed,      // 403 OTA outside the LCD's Allow update window
  Busy,              // 503 another long transfer is running (5.5)
  LowMemory,         // 503 free internal RAM near the floor (5.5)
  TooBig,            // 413 the image is larger than the app slot
  WrongImage,        // 422 not a Rev Q cabinet build (image_identity.h gives the reason)
  Stalled,           // 408 no upload data for 60 s (5.2)
  CardUnavailable,   // 503 the SD card is failed or out
  NoAnswer,          // 503 the control loop didn't answer in time (it answers within a pass; never expected)
  NotBuilt,          // 501 stage Q5c: updates from the dashboard come with stage Q5d
  TooSoon,           // 429 a saved dashboard change less than 10 s after the last (5.5)
  Count
};
int httpStatus(Refusal r);
const char* refusalCode(Refusal r);    // "out_of_bounds" — stable, for tools
const char* refusalText(Refusal r);    // a plain sentence for the page

// The outcome of a check: None = accepted.
struct ApiResult {
  Refusal refusal = Refusal::None;
  char field[32] = "";       // the field it was about, if any
  char message[120] = "";    // plain words, e.g. "Target temperature must be 15 to 22 °C"
  bool hasLimits = false;    // OutOfBounds / OffStep: the bounds and step
  float min = 0, max = 0, step = 0;
  const char* image = nullptr;   // WrongImage: why (imageVerdictCode(), docs/api.md)
  uint32_t retryS = 0;           // TooSoon: seconds to wait, also sent as a Retry-After header
  bool ok() const { return refusal == Refusal::None; }
};
ApiResult refuse(Refusal r, const char* field = nullptr, const char* message = nullptr);

// The error body: {"api":1,"error":"out_of_bounds","field":"...","message":"...","min":..,"max":..,"step":..}
// (and "image":"bench_build" with wrong_image).
int formatError(char* out, size_t n, const ApiResult& r);

// ---------------------------------------------------------------- requests
// A write must say it is JSON (5.5): "application/json", parameters allowed. Browsers can't send
// that to another site without the site's permission, which the board never gives.
bool isJsonContentType(const char* contentType);

// A value from a query string ("range=day&since=1700000000"), %XX and + decoded. False if absent
// or too long for out.
bool queryValue(const char* query, const char* name, char* out, size_t n);

// ---------------------------------------------------------------- writes
// POST /api/settings: any of target_temp_c, rh_setpoint_pct, heater_enable, humidifier_enable, ion_enable.
// All or nothing: on a refusal `out` is unchanged. Same bounds and steps as the LCD and serial.
ApiResult checkSettingsWrite(const char* body, size_t n, const UserSettings& now, bool updating, UserSettings* out);

// POST /api/sensors/exclude: {"channel": 3 | "mid_rear" | "A1C5" | "ch3", "excluded": true|false}.
// *channel gets 0-5; `out` the new settings (the manual exclude mask). Refused while Updating.
ApiResult checkExcludeWrite(const char* body, size_t n, const UserSettings& now, bool updating, int* channel,
                            UserSettings* out);

// POST /api/door-test: {} or {"note": "..."} (at most kMaxDoorNote bytes). note gets the text ("" if none).
ApiResult checkDoorTest(const char* body, size_t n, char* note, size_t noteSize);

// The same three checks split in two, for the board: the web server reads the body into a FlatJson
// (in its PSRAM work area), and the control loop then checks the fields against its own real
// settings and Updating state (the mailbox, web_mail.h) without a 1.7 KB copy on its stack.
// parseWriteBody refuses what isn't one flat object (bad_json, too_long); the *Fields checks do
// everything else, in the same order as the checks above.
ApiResult parseWriteBody(const char* body, size_t n, FlatJson* j);
ApiResult checkSettingsFields(const FlatJson& j, const UserSettings& now, bool updating, UserSettings* out);
ApiResult checkExcludeFields(const FlatJson& j, const UserSettings& now, bool updating, int* channel, UserSettings* out);
ApiResult checkDoorTestFields(const FlatJson& j, char* note, size_t noteSize);

// The replies to the accepted writes (docs/api.md):
//   exclude:   {"api":1,"ch":3,"name":"mid_rear","probe":"A1C5","excluded":true}
//   door test: {"api":1,"logged":true}
//   update:    {"api":1,"installed":true,"version":"2.0.0","restarting":true}
int formatExcludeReply(char* out, size_t n, int channel, bool excluded);
int formatDoorTestReply(char* out, size_t n);
int formatInstalledReply(char* out, size_t n, const char* version);

// The names of the user settings that differ, for the settings event ("target_temp_c 20 -> 20.5").
int describeUserChange(char* out, size_t n, const UserSettings& was, const UserSettings& now);

// ---------------------------------------------------------------- small replies
struct BuildFacts {
  const char* board = "rev-q";       // "rev-q" / "rev-n"
  Profile profile = Profile::Real;
  bool testBuild = false;
  const char* version = "";          // "2.0.0", "2.0.0-BENCH-TEST"
};

// GET /api/info
int formatInfo(char* out, size_t n, const BuildFacts& b, uint64_t uptimeS, const char* bootReason);

// GET /api/settings: the user settings with their bounds and steps; the tuning settings, read only.
// Safety values are never written (5.5).
class SettingsReply : public JsonPump {
 public:
  SettingsReply(const UserSettings& u, const TuningSettings& t) : u_(u), t_(t) {}
  size_t maxStep() const override { return 320; }
 protected:
  bool step(JsonWriter& w) override;
 private:
  UserSettings u_;
  TuningSettings t_;
  int i_ = 0;
};

// The tuning settings as shown (read only): name, label, value, unit, decimals.
struct TuningItem {
  const char* name;      // "cooling_hysteresis_c"
  const char* label;     // "Cooling hysteresis"
  const char* unit;      // "°C", "min", "%", "s", "" (on/off)
  double value;
  int decimals;
  bool onOff;            // shown as On / Off
};
int tuningItemCount();
TuningItem tuningItem(const TuningSettings& t, int i);
bool isTuningName(const char* name);

void writeBuild(JsonWriter& w, const BuildFacts& b);   // "build":{...} (key included)

}  // namespace wineador
