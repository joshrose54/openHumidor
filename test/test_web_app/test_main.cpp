// The web server's request handling (lib/core web_app), the same on the board and in the review
// build, against a fake board: a card in memory read a piece at a time, a mailbox the "loop"
// answers, the transfer gate. Errors are JSON with the documented codes; events page back through
// the files and come from RAM while the card is out; downloads go in pieces and are cut if the card
// fails; one long transfer at a time; uploads are gated by the loop, checked by identity, refused
// with not_built in stage Q5c, and always read to their end so the browser sees the reason.
#include <unity.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>
#include "wineador/web_app.h"
#include "wineador/event_text.h"
#include "../json_check.h"
#include "../web_rows.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

static const int64_t T0 = 1794571200;
static const uint8_t kPage[] = {0x1f, 0x8b, 1, 2, 3};

struct Fake : WebBackend {
  std::map<std::string, std::string> files;
  bool cardOk = true;
  int failReadAfter = -1;          // the card fails after this many reads
  int reads = 0, locks = 0, unlocks = 0;
  bool answers = true;             // the loop answers
  MailContext ctx;
  int applied = 0;
  std::string doorNote;
  Mailbox box;
  TransferGate gate;
  uint32_t freeInternal = 200000;
  HistPoint raw[400], day[1440], hourly[744];
  History hist;
  EventRow recent[50];
  RecentEvents recentRing;
  ApiResult installResult = refuse(Refusal::NotBuilt);
  Fake() {
    History::Storage s;
    s.raw = raw; s.rawCap = 400; s.day = day; s.dayCap = 1440; s.hourly = hourly; s.hourlyCap = 744;
    hist.begin(s);
    recentRing.begin(recent, 50);
    ctx.slotBytes = 3342336;
  }
  void status(StatusSnapshot* s) override { *s = StatusSnapshot{}; s->utcNowS = T0 + 600; s->build.version = "2.0.0"; s->bootReason = "POWERON"; }
  void settings(UserSettings* u, TuningSettings* t) override { *u = ctx.user; *t = TuningSettings{}; }
  void updateState(UpdateState* u) override { *u = UpdateState{}; }
  History& history() override { return hist; }
  void lockData() override { locks++; }
  void unlockData() override { unlocks++; }
  int recentEvents(EventRow* out, int max) override { return recentRing.copyNewest(out, max); }
  bool card(CardFacts* c) override { c->ok = cardOk; c->freeBytes = 1000; c->totalBytes = 2000; return cardOk; }
  int listLogs(LogFileEntry* out, int max) override {
    if (!cardOk) return -1;
    int n = 0;
    for (auto& kv : files) {
      if (n >= max) break;
      snprintf(out[n].path, sizeof(out[n].path), "%s", kv.first.c_str());
      out[n].bytes = kv.second.size();
      n++;
    }
    return n;
  }
  int readLog(const char* path, uint64_t off, char* buf, size_t n, uint64_t* size) override {
    TEST_ASSERT_TRUE(n <= kWebPiece);              // never more than one piece at a time
    if (!cardOk || (failReadAfter >= 0 && reads >= failReadAfter)) return -1;
    reads++;
    auto it = files.find(path);
    if (it == files.end()) return -2;
    if (size) *size = it->second.size();
    if (off >= it->second.size()) return 0;
    const size_t k = std::min(n, (size_t)(it->second.size() - off));
    memcpy(buf, it->second.data() + off, k);
    return (int)k;
  }
  bool ask(MailRequest* r, MailAnswer* a) override {
    if (!box.post(r)) return false;
    if (!answers) { TEST_ASSERT_TRUE(box.withdraw(r->id)); return false; }
    MailRequest t;
    box.take(&t);
    const MailAnswer ans = decideMail(t, ctx);
    if (ans.result.ok() && (t.kind == MailKind::Settings || t.kind == MailKind::Exclude)) { ctx.user = ans.user; applied++; }
    if (ans.result.ok() && t.kind == MailKind::DoorTest) doorNote = ans.note;
    box.answer(ans);
    return box.collect(r->id, a);
  }
  Refusal startTransfer(TransferKind k) override { return gate.tryStart(k, freeInternal, 8000); }
  void endTransfer() override { gate.finish(); }
  bool transferBusy() override { return gate.busy(); }
  ApiResult install(const ImageFacts&, BodyReader&, uint64_t*) override { return installResult; }
};

struct Body : BodyReader {
  std::string data;
  size_t at = 0;
  explicit Body(const std::string& d = "") : data(d) {}
  int read(char* p, size_t n, uint32_t) override {
    if (at >= data.size()) return 0;
    const size_t k = std::min(n, data.size() - at);
    memcpy(p, data.data() + at, k);
    at += k;
    return (int)k;
  }
};

struct Sink : ReplySink {
  int status = 0;
  std::string type, extraName, extraValue, body;
  bool ended = false, aborted = false;
  int pieces = 0;
  void begin(int s, const char* t, const char* n, const char* v) override {
    status = s; type = t; extraName = n ? n : ""; extraValue = v ? v : "";
  }
  bool write(const char* p, size_t n) override { TEST_ASSERT_TRUE(n <= kWebPiece); body.append(p, n); pieces++; return true; }
  void end() override { ended = true; }
  void abort() override { aborted = true; }
};

static WebWork gW;

static Sink run(Fake& f, Method m, const char* path, const char* query = "", const std::string& body = "",
                const char* type = "application/json", bool known = true, Body* bodyOut = nullptr) {
  WebApp app(f, kPage, sizeof(kPage));
  WebRequest rq;
  rq.known = known;
  rq.method = m;
  rq.path = path;
  rq.query = query;
  rq.contentType = type;
  rq.contentLength = body.size();
  Body b(body);
  Sink s;
  app.handle(rq, b, s, gW);
  if (bodyOut) *bodyOut = b;
  return s;
}

static bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

void test_errors_are_json_with_the_documented_codes() {
  Fake f;
  Sink s = run(f, Method::Get, "/nope");
  TEST_ASSERT_EQUAL_INT(404, s.status);
  TEST_ASSERT_TRUE(has(s.body, "\"error\":\"not_found\""));
  s = run(f, Method::Get, "/api/settings", "", "", "", false);   // PUT, DELETE ...
  TEST_ASSERT_EQUAL_INT(405, s.status);
  s = run(f, Method::Post, "/api/status");
  TEST_ASSERT_EQUAL_INT(405, s.status);
  s = run(f, Method::Post, "/api/settings", "", "{\"target_temp_c\":20}", "text/plain");
  TEST_ASSERT_EQUAL_INT(415, s.status);                         // writes are JSON only
  s = run(f, Method::Get, "/api/history", "range=year");
  TEST_ASSERT_TRUE(has(s.body, "\"error\":\"bad_query\""));
  s = run(f, Method::Get, "/");
  TEST_ASSERT_EQUAL_INT(200, s.status);
  TEST_ASSERT_EQUAL_STRING("Content-Encoding", s.extraName.c_str());
  TEST_ASSERT_EQUAL_INT((int)sizeof(kPage), (int)s.body.size());
}

void test_writes_go_through_the_loop() {
  Fake f;
  f.ctx.user.targetTempC = 19.0f;
  Sink s = run(f, Method::Post, "/api/settings", "", "{\"target_temp_c\":20.5}");
  TEST_ASSERT_EQUAL_INT(200, s.status);
  TEST_ASSERT_TRUE(jsoncheck::valid(s.body));
  TEST_ASSERT_TRUE(has(s.body, "\"value\":20.5"));
  TEST_ASSERT_EQUAL_INT(1, f.applied);
  // refused while Updating: nothing applied
  f.ctx.updating = true;
  s = run(f, Method::Post, "/api/settings", "", "{\"target_temp_c\":21}");
  TEST_ASSERT_EQUAL_INT(409, s.status);
  TEST_ASSERT_TRUE(has(s.body, "\"error\":\"updating\""));
  TEST_ASSERT_EQUAL_INT(1, f.applied);
  f.ctx.updating = false;
  // bad JSON is refused before the loop is asked
  s = run(f, Method::Post, "/api/settings", "", "{\"target_temp_c\":");
  TEST_ASSERT_TRUE(has(s.body, "\"error\":\"bad_json\""));
  TEST_ASSERT_TRUE(f.box.state() == Mailbox::State::Empty);
  // too long
  s = run(f, Method::Post, "/api/settings", "", "{\"a\":\"" + std::string(1100, 'x') + "\"}");
  TEST_ASSERT_TRUE(has(s.body, "\"error\":\"bad_json\""));
  // exclude and door test
  s = run(f, Method::Post, "/api/sensors/exclude", "", "{\"channel\":2,\"excluded\":true}");
  TEST_ASSERT_TRUE(has(s.body, "\"ch\":2"));
  TEST_ASSERT_EQUAL_HEX8(0x04, f.ctx.user.manualExcludeMask);
  s = run(f, Method::Post, "/api/door-test", "", "{\"note\":\"open 30 s\"}");
  TEST_ASSERT_TRUE(has(s.body, "\"logged\":true"));
  TEST_ASSERT_EQUAL_STRING("open 30 s", f.doorNote.c_str());
  // the loop doesn't answer: no_answer, and the request was withdrawn (never applied later)
  f.answers = false;
  s = run(f, Method::Post, "/api/settings", "", "{\"target_temp_c\":21}");
  TEST_ASSERT_EQUAL_INT(503, s.status);
  TEST_ASSERT_TRUE(has(s.body, "\"error\":\"no_answer\""));
  TEST_ASSERT_TRUE(f.box.state() == Mailbox::State::Empty);
}

static std::string eventsFile(int64_t t0, int n, const char* what) {
  std::string s = "# events header\ntimestamp,time_trusted,uptime_ms,type,detail\n";
  char d[80];
  for (int i = 0; i < n; i++) {
    snprintf(d, sizeof(d), "%s %d", what, i);
    s += webrows::eventRow(t0 + 60 * i, kEvTypeDoor, d);
  }
  return s;
}

static std::vector<std::string> details(const std::string& body) {
  std::vector<std::string> out;
  size_t at = 0;
  while ((at = body.find("\"detail\":\"", at)) != std::string::npos) {
    at += 10;
    out.push_back(body.substr(at, body.find('"', at) - at));
  }
  return out;
}

static std::string field(const std::string& body, const char* key) {
  std::string k = std::string("\"") + key + "\":\"";
  const size_t at = body.find(k);
  if (at == std::string::npos) return "";
  const size_t s = at + k.size();
  return body.substr(s, body.find('"', s) - s);
}

// The save limit's wait goes out as a Retry-After header too (5.5), for tools on the Mac.
void test_too_soon_carries_retry_after() {
  Fake f;
  SaveLimit limit;
  f.ctx.limit = &limit;
  f.ctx.nowMs = 1000;
  limit.saved(0);
  Sink s = run(f, Method::Post, "/api/settings", "", "{\"target_temp_c\":21}");
  TEST_ASSERT_EQUAL_INT(429, s.status);
  TEST_ASSERT_TRUE(has(s.body, "\"error\":\"too_soon\""));
  TEST_ASSERT_EQUAL_STRING("Retry-After", s.extraName.c_str());
  TEST_ASSERT_EQUAL_STRING("9", s.extraValue.c_str());
  TEST_ASSERT_EQUAL_INT(0, f.applied);
}

void test_events_page_back_through_the_files_in_pieces() {
  Fake f;
  f.files["/logs/2026/2026-10-events-s2.csv"] = eventsFile(T0 - 40 * 86400, 70, "oct");
  f.files["/logs/2026/2026-11-events-s2.csv"] = eventsFile(T0 - 86400, 200, "nov");   // several pieces
  TEST_ASSERT_TRUE(f.files["/logs/2026/2026-11-events-s2.csv"].size() > kWebPiece);   // more than one piece
  std::vector<std::string> all;
  std::string query = "";
  for (int page = 0; page < 10; page++) {
    Sink s = run(f, Method::Get, "/api/events", query.c_str());
    TEST_ASSERT_EQUAL_INT(200, s.status);
    TEST_ASSERT_TRUE(jsoncheck::valid(s.body));
    for (auto& d : details(s.body)) all.push_back(d);
    const std::string older = field(s.body, "older");
    if (older.empty()) break;
    query = "before=" + older;
  }
  TEST_ASSERT_EQUAL_INT(270, (int)all.size());        // every row once
  TEST_ASSERT_EQUAL_STRING("nov 199", all[0].c_str());  // newest first
  TEST_ASSERT_EQUAL_STRING("nov 0", all[199].c_str());
  TEST_ASSERT_EQUAL_STRING("oct 69", all[200].c_str()); // into the older file
  TEST_ASSERT_EQUAL_STRING("oct 0", all[269].c_str());
}

void test_new_events_after_a_cursor_and_into_a_new_month() {
  Fake f;
  f.files["/logs/2026/2026-10-events-s2.csv"] = eventsFile(T0 - 40 * 86400, 5, "oct");
  Sink s = run(f, Method::Get, "/api/events", "n=5");
  const std::string newest = field(s.body, "newest");
  TEST_ASSERT_TRUE(has(newest, "2026-10-events"));
  f.files["/logs/2026/2026-10-events-s2.csv"] += webrows::eventRow(T0 - 39 * 86400, kEvTypeDoor, "late oct");
  f.files["/logs/2026/2026-11-events-s2.csv"] = eventsFile(T0, 2, "nov");
  s = run(f, Method::Get, "/api/events", ("after=" + newest).c_str());
  std::vector<std::string> d = details(s.body);
  TEST_ASSERT_EQUAL_INT(1, (int)d.size());
  TEST_ASSERT_EQUAL_STRING("late oct", d[0].c_str());
  const std::string next = field(s.body, "newest");
  TEST_ASSERT_EQUAL_STRING("/logs/2026/2026-11-events-s2.csv@0", next.c_str());   // the new month's file next
  s = run(f, Method::Get, "/api/events", ("after=" + next).c_str());
  d = details(s.body);
  TEST_ASSERT_EQUAL_INT(2, (int)d.size());
  TEST_ASSERT_EQUAL_STRING("nov 1", d[0].c_str());
}

void test_events_from_ram_while_the_card_is_out() {
  Fake f;
  f.cardOk = false;
  EventRow e;
  snprintf(e.type, sizeof(e.type), "door");
  snprintf(e.detail, sizeof(e.detail), "kept in RAM");
  f.recentRing.add(e);
  Sink s = run(f, Method::Get, "/api/events");
  TEST_ASSERT_EQUAL_INT(200, s.status);
  TEST_ASSERT_TRUE(has(s.body, "\"source\":\"ram\""));
  TEST_ASSERT_TRUE(has(s.body, "kept in RAM"));
  TEST_ASSERT_EQUAL_INT(0, f.reads);                  // the card wasn't touched
  s = run(f, Method::Get, "/api/logs");
  TEST_ASSERT_TRUE(has(s.body, "\"error\":\"card_unavailable\""));
}

void test_downloads_in_pieces_one_long_transfer_at_a_time() {
  Fake f;
  std::string big;
  for (int i = 0; i < 1000; i++) big += webrows::readingsRow(T0 + 15 * i, 20.0f, false);
  f.files["/logs/2026/2026-11-readings-s2.csv"] = big;
  Sink s = run(f, Method::Get, "/api/logs/file", "path=/logs/2026/2026-11-readings-s2.csv");
  TEST_ASSERT_EQUAL_INT(200, s.status);
  TEST_ASSERT_TRUE(s.ended);
  TEST_ASSERT_TRUE(s.body == big);
  TEST_ASSERT_TRUE(s.pieces > 10);
  TEST_ASSERT_TRUE(has(s.extraValue, "2026-11-readings-s2.csv"));
  TEST_ASSERT_FALSE(f.gate.busy());                   // released
  // another transfer running: busy; near the RAM floor: low_memory
  f.gate.tryStart(TransferKind::Update, 200000, 0);
  s = run(f, Method::Get, "/api/logs/file", "path=/logs/2026/2026-11-readings-s2.csv");
  TEST_ASSERT_TRUE(has(s.body, "\"error\":\"busy\""));
  f.gate.finish();
  f.freeInternal = 33000;
  s = run(f, Method::Get, "/api/logs/file", "path=/logs/2026/2026-11-readings-s2.csv");
  TEST_ASSERT_TRUE(has(s.body, "\"error\":\"low_memory\""));
  f.freeInternal = 200000;
  // not a log file; no such file (the gate is released)
  s = run(f, Method::Get, "/api/logs/file", "path=/etc/passwd");
  TEST_ASSERT_TRUE(has(s.body, "\"error\":\"bad_path\""));
  s = run(f, Method::Get, "/api/logs/file", "path=/logs/2026/2026-01-readings-s2.csv");
  TEST_ASSERT_EQUAL_INT(404, s.status);
  TEST_ASSERT_FALSE(f.gate.busy());
  // the card fails partway: the connection is cut (the browser sees a failed download, not a short file)
  f.reads = 0;
  f.failReadAfter = 3;
  s = run(f, Method::Get, "/api/logs/file", "path=/logs/2026/2026-11-readings-s2.csv");
  TEST_ASSERT_TRUE(s.aborted);
  TEST_ASSERT_FALSE(s.ended);
  TEST_ASSERT_FALSE(f.gate.busy());
}

void test_the_graphs_are_read_under_the_lock_and_let_go() {
  Fake f;
  for (int i = 0; i < 20; i++) {
    const std::string r = webrows::readingsRow(T0 + 15 * i, 20.0f, false);
    f.hist.addReadingsLine(r.data(), r.size());
  }
  Sink s = run(f, Method::Get, "/api/history", "range=hour");
  TEST_ASSERT_EQUAL_INT(200, s.status);
  TEST_ASSERT_TRUE(jsoncheck::valid(s.body));
  TEST_ASSERT_EQUAL_INT(f.locks, f.unlocks);
  TEST_ASSERT_TRUE(f.locks >= 3);                     // made, each piece, let go
  TEST_ASSERT_FALSE(f.hist.replyOpen());              // the rings move again
}

static std::string image(const char* board, const char* profile, bool test, size_t n = 2000, uint16_t chip = kChipEsp32S3) {
  std::string b(n, '\0');
  b[0] = (char)0xE9;
  b[12] = (char)(chip & 0xFF);
  b[13] = (char)(chip >> 8);
  b[0x20] = 0x32; b[0x21] = 0x54; b[0x22] = (char)0xCD; b[0x23] = (char)0xAB;
  ImageIdentityBlock id;
  makeIdentityBlock(&id, board, strcmp(profile, "bench") == 0, test, "2.0.0", 2);
  memcpy(&b[kIdentityOffset], &id, sizeof(id));
  return b;
}

void test_uploads_gated_by_the_loop_checked_and_not_built_yet() {
  Fake f;
  Body b;
  const char* bin = "application/octet-stream";
  // window closed: refused before the image is looked at, the body still read to its end
  Sink s = run(f, Method::Post, "/api/update", "", image("rev-q", "real", false), bin, true, &b);
  TEST_ASSERT_EQUAL_INT(403, s.status);
  TEST_ASSERT_TRUE(has(s.body, "window_closed"));
  TEST_ASSERT_EQUAL_INT((int)b.data.size(), (int)b.at);
  f.ctx.windowOpen = true;
  f.ctx.compressorRunning = true;
  s = run(f, Method::Post, "/api/update", "", image("rev-q", "real", false), bin);
  TEST_ASSERT_TRUE(has(s.body, "compressor_running"));
  f.ctx.compressorRunning = false;
  // a download running: busy
  f.gate.tryStart(TransferKind::Download, 200000, 0);
  s = run(f, Method::Post, "/api/update", "", image("rev-q", "real", false), bin);
  TEST_ASSERT_TRUE(has(s.body, "\"error\":\"busy\""));
  f.gate.finish();
  // the wrong builds
  s = run(f, Method::Post, "/api/update", "", image("rev-q", "bench", false), bin, true, &b);
  TEST_ASSERT_EQUAL_INT(422, s.status);
  TEST_ASSERT_TRUE(has(s.body, "\"image\":\"bench_build\""));
  TEST_ASSERT_EQUAL_INT((int)b.data.size(), (int)b.at);
  s = run(f, Method::Post, "/api/update", "", image("rev-q", "real", false, 2000, kChipEsp32), bin);
  TEST_ASSERT_TRUE(has(s.body, "\"image\":\"rev_n_image\""));
  s = run(f, Method::Post, "/api/update", "", std::string(100, 'x'), bin);
  TEST_ASSERT_TRUE(has(s.body, "\"image\":\"too_short\""));
  // a Rev Q cabinet build: not built yet (Q5c), read to its end, the gate released
  s = run(f, Method::Post, "/api/update", "", image("rev-q", "real", false), bin, true, &b);
  TEST_ASSERT_EQUAL_INT(501, s.status);
  TEST_ASSERT_TRUE(has(s.body, "\"error\":\"not_built\""));
  TEST_ASSERT_EQUAL_INT((int)b.data.size(), (int)b.at);
  TEST_ASSERT_FALSE(f.gate.busy());
  // and when installed (the review build)
  f.installResult = ApiResult{};
  s = run(f, Method::Post, "/api/update", "", image("rev-q", "real", false), bin);
  TEST_ASSERT_TRUE(has(s.body, "\"installed\":true"));
}

void test_only_downloads_and_uploads_are_long() {
  WebRequest r;
  r.path = "/api/logs/file";
  TEST_ASSERT_TRUE(WebApp::isLong(r));
  r.method = Method::Post;
  r.path = "/api/update";
  TEST_ASSERT_TRUE(WebApp::isLong(r));
  r.method = Method::Get;
  for (const char* p : {"/", "/api/status", "/api/events", "/api/logs", "/api/update", "/api/history"}) {
    r.path = p;
    TEST_ASSERT_FALSE(WebApp::isLong(r));
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_errors_are_json_with_the_documented_codes);
  RUN_TEST(test_writes_go_through_the_loop);
  RUN_TEST(test_too_soon_carries_retry_after);
  RUN_TEST(test_events_page_back_through_the_files_in_pieces);
  RUN_TEST(test_new_events_after_a_cursor_and_into_a_new_month);
  RUN_TEST(test_events_from_ram_while_the_card_is_out);
  RUN_TEST(test_downloads_in_pieces_one_long_transfer_at_a_time);
  RUN_TEST(test_the_graphs_are_read_under_the_lock_and_let_go);
  RUN_TEST(test_uploads_gated_by_the_loop_checked_and_not_built_yet);
  RUN_TEST(test_only_downloads_and_uploads_are_long);
  return UNITY_END();
}
