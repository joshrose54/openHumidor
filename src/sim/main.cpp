// The Mac review build of the web dashboard (stage Q5b): serves the same compressed page file the
// board will serve, and answers the API with the same lib/core code, from a simulated board (the
// cabinet model run by the real control logic, sim_board.h). Not firmware: never on the board.
//
//   pio run -e dashboard_sim -t exec            then open http://localhost:8080
//   on a phone on the same WiFi:                http://<this Mac's name>.local:8080
//   review states and "press Allow update":     http://localhost:8080/sim
//
// Options (after `--` with pio, or running the program directly):
//   --port N        another port (default 8080)
//   --page FILE     serve this page file instead of the built-in one (for working on web/)
//   --shots         every 30 s cuts to 0.5 s so screenshots can be taken quickly (never on the board)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "sim_board.h"
#include "webui/page.h"
#include "wineador/web_api.h"
#include "wineador/web_status.h"
#include "wineador/web_lists.h"
#include "wineador/history.h"
#include "wineador/log_read.h"
#include "wineador/ota_rules.h"
#include "wineador/image_identity.h"

using namespace wineador;

static sim::Board gBoard;
static std::string gPageFile;        // --page
static TransferGate gTransfers;

// ---------------------------------------------------------------- HTTP, the smallest that works
struct Request {
  std::string method, path, query, contentType;
  size_t contentLength = 0;
  std::string body;                  // writes (at most kMaxJsonBody + a little)
  std::vector<uint8_t> head;         // an upload's first bytes (the identity check)
  size_t uploaded = 0;
};

static bool sendAll(int fd, const void* p, size_t n) {
  const char* c = (const char*)p;
  while (n > 0) {
    const ssize_t k = send(fd, c, n, 0);
    if (k <= 0) { if (errno == EINTR) continue; return false; }
    c += k;
    n -= (size_t)k;
  }
  return true;
}

static const char* statusText(int code) {
  switch (code) {
    case 200: return "OK"; case 302: return "Found"; case 400: return "Bad Request"; case 403: return "Forbidden";
    case 404: return "Not Found"; case 405: return "Method Not Allowed"; case 408: return "Request Timeout";
    case 409: return "Conflict"; case 413: return "Payload Too Large"; case 415: return "Unsupported Media Type";
    case 422: return "Unprocessable Entity"; case 503: return "Service Unavailable";
  }
  return "Error";
}

// Every reply closes its connection (5.5), and carries no CORS header: no other site may write.
static void reply(int fd, int code, const char* type, const void* body, size_t n, const char* extra = "") {
  char h[512];
  const int k = snprintf(h, sizeof(h),
                         "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n"
                         "Cache-Control: no-store\r\n%s\r\n", code, statusText(code), type, n, extra);
  sendAll(fd, h, (size_t)k);
  if (n) sendAll(fd, body, n);
}

static void replyJson(int fd, int code, const std::string& j) { reply(fd, code, "application/json; charset=utf-8", j.data(), j.size()); }

static void replyError(int fd, const ApiResult& r) {
  char b[600];
  const int n = formatError(b, sizeof(b), r);
  replyJson(fd, httpStatus(r.refusal), std::string(b, n > 0 ? (size_t)n : 0));
}

// A reply written in pieces, as the board sends it (4 KB at a time).
static std::string pump(JsonPump& p) {
  std::string out;
  static char piece[4096];
  while (!p.done()) out.append(piece, p.fill(piece, sizeof(piece)));
  return out;
}

static bool readRequest(int fd, Request* r) {
  std::string buf;
  char c[4096];
  size_t headerEnd = std::string::npos;
  while (headerEnd == std::string::npos) {
    const ssize_t k = recv(fd, c, sizeof(c), 0);
    if (k <= 0) return false;
    buf.append(c, (size_t)k);
    headerEnd = buf.find("\r\n\r\n");
    if (buf.size() > 16384) return false;
  }
  const std::string head = buf.substr(0, headerEnd);
  std::string rest = buf.substr(headerEnd + 4);
  const size_t sp1 = head.find(' '), sp2 = head.find(' ', sp1 + 1);
  if (sp1 == std::string::npos || sp2 == std::string::npos) return false;
  r->method = head.substr(0, sp1);
  std::string target = head.substr(sp1 + 1, sp2 - sp1 - 1);
  const size_t q = target.find('?');
  r->path = q == std::string::npos ? target : target.substr(0, q);
  r->query = q == std::string::npos ? "" : target.substr(q + 1);
  size_t at = head.find("\r\n");
  while (at != std::string::npos && at < head.size()) {
    const size_t next = head.find("\r\n", at + 2);
    const std::string line = head.substr(at + 2, (next == std::string::npos ? head.size() : next) - at - 2);
    const size_t colon = line.find(':');
    if (colon != std::string::npos) {
      std::string k = line.substr(0, colon), v = line.substr(colon + 1);
      for (auto& ch : k) ch = (char)tolower(ch);
      while (!v.empty() && v[0] == ' ') v.erase(0, 1);
      if (k == "content-length") r->contentLength = (size_t)strtoull(v.c_str(), nullptr, 10);
      if (k == "content-type") r->contentType = v;
    }
    at = next;
  }
  // the body: a write is kept whole; an upload keeps only its first bytes (the review build
  // never writes an image anywhere) and is read to its end, in pieces
  const bool upload = r->path == "/api/update" && r->method == "POST";
  auto take = [&](const char* p, size_t n) {
    if (upload) {
      for (size_t i = 0; i < n && r->head.size() < kImageCheckBytes; i++) r->head.push_back((uint8_t)p[i]);
      r->uploaded += n;
    } else if (r->body.size() < kMaxJsonBody + 64) {
      r->body.append(p, std::min(n, kMaxJsonBody + 64 - r->body.size()));
    }
  };
  take(rest.data(), rest.size());
  size_t got = rest.size();
  while (got < r->contentLength) {
    pollfd pf{fd, POLLIN, 0};
    if (poll(&pf, 1, (int)kUploadStallMs) <= 0) return true;   // stalled: what arrived is what we have
    const ssize_t k = recv(fd, c, sizeof(c), 0);
    if (k <= 0) break;
    take(c, (size_t)k);
    got += (size_t)k;
  }
  return true;
}

// ---------------------------------------------------------------- the review-only /sim page
static void simPage(int fd) {
  std::string h = "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
                  "<title>openHumidor review states</title><style>body{font:16px -apple-system,sans-serif;"
                  "background:#14110F;color:#F3EDE7;padding:24px;max-width:640px}a{color:#D9A55B}"
                  "li{margin:10px 0}.on{font-weight:700}</style><h1>Review states</h1><p>Review build only: "
                  "never on the board. <a href='/'>Open the dashboard</a></p><ul>";
  for (int i = 0; i < (int)sim::State::Count; i++) {
    const sim::State s = (sim::State)i;
    h += "<li class='";
    h += gBoard.state() == s ? "on" : "";
    h += "'><a href='/sim/state?name=";
    h += sim::stateKey(s);
    h += "'>";
    h += sim::stateLabel(s);
    h += "</a></li>";
  }
  h += "</ul><p><a href='/sim/allow-update'>Press Allow update</a> (as on the cabinet's LCD: opens the 5-minute "
       "window)</p>";
  reply(fd, 200, "text/html; charset=utf-8", h.data(), h.size());
}

static void back(int fd) { reply(fd, 302, "text/plain", "", 0, "Location: /sim\r\n"); }

// ---------------------------------------------------------------- the API
static void serveEvents(int fd, const Request& rq) {
  char v[160];
  int n = kEventsPage;
  if (queryValue(rq.query.c_str(), "n", v, sizeof(v))) {
    n = atoi(v);
    if (n < 1 || n > kEventsPage) { replyError(fd, refuse(Refusal::BadQuery, "n", "n is 1 to 50.")); return; }
  }
  static EventRow rows[kEventsPage];
  if (!gBoard.sdOk()) {   // the card is out: the newest kept in RAM, marked (5.5)
    const int k = gBoard.recent().copyNewest(rows, n);
    EventsReply r(rows, k, nullptr, nullptr, true);
    replyJson(fd, 200, pump(r));
    return;
  }
  const std::vector<LogFileEntry> files = gBoard.files();
  char path[48];
  uint64_t end = 0;
  bool after = false;
  if (queryValue(rq.query.c_str(), "before", v, sizeof(v))) {
    if (!parseCursor(v, path, sizeof(path), &end)) { replyError(fd, refuse(Refusal::BadQuery, "before")); return; }
  } else if (queryValue(rq.query.c_str(), "after", v, sizeof(v))) {
    if (!parseCursor(v, path, sizeof(path), &end)) { replyError(fd, refuse(Refusal::BadQuery, "after")); return; }
    after = true;
  } else {
    if (!newestEventsFile(files.data(), (int)files.size(), path, sizeof(path))) {
      EventsReply r(rows, 0, nullptr, nullptr);
      replyJson(fd, 200, pump(r));
      return;
    }
    end = gBoard.file(path)->size();
  }
  struct Ctx { EventRow* rows; int n, max; uint64_t oldest; };
  Ctx c{rows, 0, n, 0};
  char newest[80] = "";
  const std::string* f = gBoard.file(path);
  if (!f) { replyError(fd, refuse(Refusal::NotFound)); return; }
  if (after) {
    // new rows since the cursor, newest first
    std::vector<EventRow> got;
    LineReader lr;
    struct A { std::vector<EventRow>* v; };
    A a{&got};
    lr.begin(end, [](const char* line, size_t len, uint64_t off, void* p) -> bool {
      EventRow e;
      if (parseEventRow(line, len, &e)) { e.offset = off; ((A*)p)->v->push_back(e); }
      return true;
    }, &a);
    if (end < f->size()) lr.feed(f->data() + end, f->size() - end);
    lr.finish();
    int k = 0;
    for (int i = (int)got.size() - 1; i >= 0 && k < n; i--) rows[k++] = got[(size_t)i];
    formatCursor(newest, sizeof(newest), path, f->size());
    EventsReply r(rows, k, nullptr, newest);
    replyJson(fd, 200, pump(r));
    return;
  }
  // newest first, back from `end`, in 4 KB pieces as the SD task reads them; into older files too
  formatCursor(newest, sizeof(newest), path, f->size());
  std::string cur = path;
  uint64_t curEnd = end;
  bool more = true;
  while (c.n < n) {
    const std::string* ff = gBoard.file(cur.c_str());
    BackwardLineReader br;
    br.begin([](const char* line, size_t len, uint64_t off, void* p) -> bool {
      Ctx* x = (Ctx*)p;
      EventRow e;
      if (!parseEventRow(line, len, &e)) return true;
      e.offset = off;
      x->rows[x->n++] = e;
      x->oldest = off;
      return x->n < x->max;
    }, &c);
    uint64_t e2 = curEnd;
    bool stopped = false;
    while (e2 > 0 && !stopped) {
      const uint64_t s2 = e2 > 4096 ? e2 - 4096 : 0;
      stopped = !br.feed(ff->data() + s2, (size_t)(e2 - s2), s2);
      e2 = s2;
    }
    if (!stopped) br.finish();
    if (c.n >= n) break;
    char older[48];
    if (!olderEventsFile(files.data(), (int)files.size(), cur.c_str(), older, sizeof(older))) { more = false; break; }
    cur = older;
    curEnd = gBoard.file(older)->size();
    c.oldest = curEnd;
  }
  char olderCursor[80] = "";
  if (more && c.n > 0) formatCursor(olderCursor, sizeof(olderCursor), cur.c_str(), c.oldest);
  EventsReply r(rows, c.n, olderCursor, newest);
  replyJson(fd, 200, pump(r));
}

static void serveUpload(int fd, Request& rq) {
  UpdateGateInputs g;
  g.windowOpen = gBoard.windowOpen();
  g.compressorRunning = gBoard.compressorRunning();
  g.updating = gBoard.updating();
  g.transferBusy = gTransfers.busy();
  g.length = (uint32_t)rq.contentLength;
  g.slotBytes = 3342336;
  const Refusal r = checkUpdateStart(g);
  if (r != Refusal::None) { replyError(fd, refuse(r)); return; }
  if (rq.uploaded < rq.contentLength) { replyError(fd, refuse(Refusal::Stalled)); return; }
  const ImageFacts facts = readImageFacts(rq.head.data(), rq.head.size());
  const ImageVerdict v = judgeImage(facts);
  if (v != ImageVerdict::Accept) { replyError(fd, refuseImage(v)); return; }
  if (!gBoard.startUpdate(facts.version)) { replyError(fd, refuse(Refusal::WindowClosed)); return; }
  char b[200];
  formatInstalledReply(b, sizeof(b), facts.version);
  replyJson(fd, 200, b);
}

static void handle(int fd) {
  Request rq;
  if (!readRequest(fd, &rq)) return;
  const int64_t now = (int64_t)time(nullptr);
  gBoard.tick(now);
  // review-only pages
  if (rq.path == "/sim") { simPage(fd); return; }
  if (rq.path == "/sim/state") {
    char v[40];
    sim::State s;
    if (queryValue(rq.query.c_str(), "name", v, sizeof(v)) && sim::stateFromKey(v, &s)) gBoard.setState(s, now);
    back(fd);
    return;
  }
  if (rq.path == "/sim/allow-update") { gBoard.pressAllowUpdate(); back(fd); return; }
  if (rq.path == "/sim/frame") {   // the dashboard at a phone's width, for screenshots (headless Chrome won't go below 500 px)
    char w[8] = "390", h[8] = "2300", p[24] = "overview";
    queryValue(rq.query.c_str(), "w", w, sizeof(w));
    queryValue(rq.query.c_str(), "h", h, sizeof(h));
    queryValue(rq.query.c_str(), "page", p, sizeof(p));
    char f[400];
    const int n = snprintf(f, sizeof(f), "<!doctype html><body style='margin:0;background:#14110F'><iframe src='/#%s' "
                                          "style='border:0;display:block;width:%dpx;height:%dpx'></iframe>", p, atoi(w), atoi(h));
    reply(fd, 200, "text/html; charset=utf-8", f, (size_t)n);
    return;
  }
  // the dashboard
  const Method m = rq.method == "POST" ? Method::Post : Method::Get;
  bool known = false;
  const Endpoint* ep = (rq.method == "GET" || rq.method == "POST") ? findEndpoint(m, rq.path.c_str(), &known) : nullptr;
  if (!ep) {
    if (!known) known = findEndpoint(Method::Get, rq.path.c_str()) || findEndpoint(Method::Post, rq.path.c_str());
    replyError(fd, refuse(known ? Refusal::MethodNotAllowed : Refusal::NotFound));
    return;
  }
  if (m == Method::Post && ep->route != Route::UpdateUpload && !isJsonContentType(rq.contentType.c_str())) {
    replyError(fd, refuse(Refusal::NotJson));
    return;
  }
  char b[1024];
  switch (ep->route) {
    case Route::Page: {
      if (!gPageFile.empty()) {
        FILE* f = fopen(gPageFile.c_str(), "rb");
        if (!f) { reply(fd, 404, "text/plain", "page file not found", 19); return; }
        std::string s;
        size_t k;
        while ((k = fread(b, 1, sizeof(b), f)) > 0) s.append(b, k);
        fclose(f);
        reply(fd, 200, "text/html; charset=utf-8", s.data(), s.size());
      } else {
        reply(fd, 200, "text/html; charset=utf-8", webui::kPageGz, webui::kPageGzLen, "Content-Encoding: gzip\r\n");
      }
      return;
    }
    case Route::Info: {
      const int n = formatInfo(b, sizeof(b), gBoard.build(), 7200, "POWERON");
      replyJson(fd, 200, std::string(b, (size_t)n));
      return;
    }
    case Route::Status: {
      static StatusSnapshot s;
      gBoard.fillStatus(&s);
      StatusReply r(s);
      replyJson(fd, 200, pump(r));
      return;
    }
    case Route::History: {
      char v[32];
      HistRange range;
      if (!queryValue(rq.query.c_str(), "range", v, sizeof(v)) || !parseHistRange(v, &range)) {
        replyError(fd, refuse(Refusal::BadQuery, "range", "range is hour, day, week or month."));
        return;
      }
      int64_t since = 0;
      if (queryValue(rq.query.c_str(), "since", v, sizeof(v))) since = strtoll(v, nullptr, 10);
      HistoryReply r(gBoard.history(), range, gBoard.utcNow(), since);
      replyJson(fd, 200, pump(r));
      return;
    }
    case Route::Events: serveEvents(fd, rq); return;
    case Route::Settings: {
      SettingsReply r(gBoard.user(), gBoard.tuning());
      replyJson(fd, 200, pump(r));
      return;
    }
    case Route::Logs: {
      if (!gBoard.sdOk()) { replyError(fd, refuse(Refusal::CardUnavailable)); return; }
      std::vector<LogFileEntry> f = gBoard.files();
      CardFacts c;
      c.ok = true;
      c.freeBytes = 59200000000ULL;
      c.totalBytes = 59700000000ULL;
      LogsReply r(c, f.data(), (int)f.size());
      replyJson(fd, 200, pump(r));
      return;
    }
    case Route::LogFile: {
      char v[96];
      LogName ln;
      if (!queryValue(rq.query.c_str(), "path", v, sizeof(v)) || !parseLogPath(v, &ln)) {
        replyError(fd, refuse(Refusal::BadPath));
        return;
      }
      if (!gBoard.sdOk()) { replyError(fd, refuse(Refusal::CardUnavailable)); return; }
      const std::string* f = gBoard.file(v);
      if (!f) { replyError(fd, refuse(Refusal::NotFound)); return; }
      const Refusal t = gTransfers.tryStart(TransferKind::Download, 200000, 0);
      if (t != Refusal::None) { replyError(fd, refuse(t)); return; }
      const char* name = strrchr(v, '/') + 1;
      char extra[160];
      snprintf(extra, sizeof(extra), "Content-Disposition: attachment; filename=\"%s\"\r\n", name);
      reply(fd, 200, "text/csv; charset=utf-8", f->data(), f->size(), extra);
      gTransfers.finish();
      return;
    }
    case Route::UpdateState: {
      const UpdateState u = gBoard.updateState();
      const int n = formatUpdateState(b, sizeof(b), u);
      replyJson(fd, 200, std::string(b, (size_t)n));
      return;
    }
    case Route::SettingsWrite: {
      UserSettings out;
      const ApiResult r = checkSettingsWrite(rq.body.data(), rq.body.size(), gBoard.user(), gBoard.updating(), &out);
      if (!r.ok()) { replyError(fd, r); return; }
      gBoard.applyUser(out, "dashboard");
      SettingsReply sr(gBoard.user(), gBoard.tuning());
      replyJson(fd, 200, pump(sr));
      return;
    }
    case Route::Exclude: {
      UserSettings out;
      int ch = -1;
      const ApiResult r = checkExcludeWrite(rq.body.data(), rq.body.size(), gBoard.user(), gBoard.updating(), &ch, &out);
      if (!r.ok()) { replyError(fd, r); return; }
      gBoard.applyUser(out, "dashboard");
      const int n = formatExcludeReply(b, sizeof(b), ch, (out.manualExcludeMask >> ch) & 1);
      replyJson(fd, 200, std::string(b, (size_t)n));
      return;
    }
    case Route::DoorTest: {
      char note[160];
      const ApiResult r = checkDoorTest(rq.body.data(), rq.body.size(), note, sizeof(note));
      if (!r.ok()) { replyError(fd, r); return; }
      gBoard.doorTest(note);
      const int n = formatDoorTestReply(b, sizeof(b));
      replyJson(fd, 200, std::string(b, (size_t)n));
      return;
    }
    case Route::UpdateUpload: serveUpload(fd, rq); return;
    default: replyError(fd, refuse(Refusal::NotFound)); return;
  }
}

int main(int argc, char** argv) {
  int port = 8080;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--page") && i + 1 < argc) gPageFile = argv[++i];
  }
  signal(SIGPIPE, SIG_IGN);
  printf("openHumidor dashboard, review build (made-up data from the cabinet model and the real control logic)\n");
  gBoard.begin((int64_t)time(nullptr));
  const int srv = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons((uint16_t)port);
  a.sin_addr.s_addr = htonl(INADDR_ANY);   // the phone on the same WiFi can reach it
  if (bind(srv, (sockaddr*)&a, sizeof(a)) != 0) { perror("bind"); return 1; }
  listen(srv, kMaxBrowserConnections);
  char host[256] = "this-mac";
  gethostname(host, sizeof(host));
  printf("\n  On this Mac:   http://localhost:%d\n  On a phone:    http://%s%s:%d  (same WiFi)\n"
         "  Review states: http://localhost:%d/sim\n  Page: %s\n  Stop: Ctrl-C\n\n",
         port, host, strstr(host, ".local") ? "" : ".local", port, port,
         gPageFile.empty() ? "built in (compressed, as on the board)" : gPageFile.c_str());
  fflush(stdout);
  for (;;) {
    pollfd pf{srv, POLLIN, 0};
    if (poll(&pf, 1, 500) > 0) {
      const int c = accept(srv, nullptr, nullptr);
      if (c >= 0) {
        timeval tv{70, 0};
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        handle(c);
        shutdown(c, SHUT_WR);
        close(c);
      }
    }
    gBoard.tick((int64_t)time(nullptr));
  }
}
