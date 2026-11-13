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
#include "wineador/web_app.h"
#include "wineador/web_mail.h"
#include "wineador/ota_update.h"
#include <algorithm>

using namespace wineador;

static sim::Board gBoard;
static std::string gPageFile;        // --page

// ---------------------------------------------------------------- HTTP, the smallest that works
struct Request {
  std::string method, path, query, contentType;
  uint64_t contentLength = 0;
  std::string pending;               // body bytes that came in with the headers
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
    case 422: return "Unprocessable Entity"; case 429: return "Too Many Requests"; case 501: return "Not Implemented"; case 503: return "Service Unavailable";
  }
  return "Error";
}

// Every reply closes its connection (5.5), and carries no CORS header: no other site may write.
static void reply(int fd, int code, const char* type, const void* body, size_t n, const char* extra = "") {
  char h[600];
  const int k = snprintf(h, sizeof(h),
                         "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n"
                         "Cache-Control: no-store\r\n%s\r\n", code, statusText(code), type, n, extra);
  sendAll(fd, h, (size_t)k);
  if (n) sendAll(fd, body, n);
}

static bool readHead(int fd, Request* r) {
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
  r->pending = buf.substr(headerEnd + 4);
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
      if (k == "content-length") r->contentLength = strtoull(v.c_str(), nullptr, 10);
      if (k == "content-type") r->contentType = v;
    }
    at = next;
  }
  return true;
}

// The body, from what came with the headers and then the socket (never past Content-Length).
class SocketBody : public BodyReader {
 public:
  SocketBody(int fd, Request& r) : fd_(fd), r_(r) {}
  int read(char* p, size_t n, uint32_t waitMs) override {
    if (got_ >= r_.contentLength) return 0;
    if (n > r_.contentLength - got_) n = (size_t)(r_.contentLength - got_);
    if (!r_.pending.empty()) {
      const size_t k = std::min(n, r_.pending.size());
      memcpy(p, r_.pending.data(), k);
      r_.pending.erase(0, k);
      got_ += k;
      return (int)k;
    }
    pollfd pf{fd_, POLLIN, 0};
    if (poll(&pf, 1, (int)waitMs) <= 0) return -1;
    const ssize_t k = recv(fd_, p, n, 0);
    if (k <= 0) return -1;
    got_ += (uint64_t)k;
    return (int)k;
  }
 private:
  int fd_;
  Request& r_;
  uint64_t got_ = 0;
};

// The reply, kept whole and sent with its length (the board sends it in chunks as it goes).
class BufferSink : public ReplySink {
 public:
  explicit BufferSink(int fd) : fd_(fd) {}
  void begin(int status, const char* type, const char* name, const char* value) override {
    status_ = status;
    type_ = type;
    extra_ = name ? std::string(name) + ": " + value + "\r\n" : "";
  }
  bool write(const char* p, size_t n) override { body_.append(p, n); return true; }
  void end() override { reply(fd_, status_, type_.c_str(), body_.data(), body_.size(), extra_.c_str()); }
  void abort() override {}
 private:
  int fd_;
  int status_ = 500;
  std::string type_, extra_, body_;
};

// ---------------------------------------------------------------- OTA into memory (review only)
class MemFlash : public OtaFlash {
 public:
  bool begin(uint32_t bytes) override { img_.clear(); img_.reserve(bytes); return true; }
  bool write(const uint8_t* p, size_t n) override { img_.insert(img_.end(), p, p + n); return true; }
  End end() override { return imageChecksumOk(img_.data(), img_.size()) ? End::Ok : End::Corrupt; }
  void abort() override { img_.clear(); }
  bool setBoot() override { return true; }
 private:
  std::vector<uint8_t> img_;
};

// One piece per "pass": the simulated board moves on between pieces, as the loop does.
class SimPacer : public OtaPacer {
 public:
  bool nextPass() override { return true; }
  void progress(uint32_t bytes) override { gBoard.otaProgress(bytes); }
};

// ---------------------------------------------------------------- the simulated board, as the server sees it
class SimBackend : public WebBackend {
 public:
  void status(StatusSnapshot* out) override { gBoard.fillStatus(out); }
  void settings(UserSettings* u, TuningSettings* t) override { *u = gBoard.user(); *t = gBoard.tuning(); }
  void updateState(UpdateState* out) override { *out = gBoard.updateState(); }
  History& history() override { return gBoard.history(); }
  void lockData() override {}
  void unlockData() override {}
  int recentEvents(EventRow* out, int max) override { return gBoard.recent().copyNewest(out, max); }
  bool card(CardFacts* c) override {
    c->ok = gBoard.sdOk();
    c->freeBytes = 59200000000ULL;
    c->totalBytes = 59700000000ULL;
    return c->ok;
  }
  int listLogs(LogFileEntry* out, int max) override {
    if (!gBoard.sdOk()) return -1;
    const std::vector<LogFileEntry> f = gBoard.files();
    int n = 0;
    for (const LogFileEntry& e : f) if (n < max) out[n++] = e;
    return n;
  }
  int readLog(const char* path, uint64_t off, char* buf, size_t n, uint64_t* size) override {
    if (!gBoard.sdOk()) return -1;
    const std::string* f = gBoard.file(path);
    if (!f) return -2;
    if (size) *size = f->size();
    if (off >= f->size()) return 0;
    const size_t k = std::min(n, (size_t)(f->size() - off));
    memcpy(buf, f->data() + off, k);
    return (int)k;
  }
  // the loop's half, as the board's loop does it: decided against the real settings, then applied
  bool ask(MailRequest* r, MailAnswer* a) override {
    if (!box_.post(r)) return false;
    MailRequest taken;
    box_.take(&taken);
    if (taken.kind == MailKind::UpdateBegin || taken.kind == MailKind::UpdateEnd) {
      box_.answer(gBoard.otaMail(taken));
      return box_.collect(r->id, a);
    }
    MailContext c;
    c.user = gBoard.user();
    c.updating = gBoard.updating();
    c.windowOpen = gBoard.windowOpen();
    c.compressorRunning = gBoard.compressorRunning();
    c.slotBytes = 3342336;
    c.checkingLeftS = gBoard.checkingLeftS();
    c.nowMs = (uint32_t)(time(nullptr) * 1000);
    c.limit = &limit_;
    const MailAnswer ans = decideMail(taken, c);
    if (ans.result.ok()) {
      if ((taken.kind == MailKind::Settings || taken.kind == MailKind::Exclude) && !sameUserSettings(ans.user, gBoard.user())) {
        gBoard.applyUser(ans.user, "dashboard");
        limit_.saved(c.nowMs);
      }
      if (taken.kind == MailKind::DoorTest) gBoard.doorTest(ans.note);
    }
    box_.answer(ans);
    return box_.collect(r->id, a);
  }
  Refusal startTransfer(TransferKind k) override { return gate_.tryStart(k, 200000, 0); }
  void endTransfer() override { gate_.finish(); }
  bool transferBusy() override { return gate_.busy(); }
  // the same install as the board (lib/core ota_update.h), into a flash held in memory: the image's
  // own checksum is checked at the end, as ESP-IDF checks it (with its SHA-256) on the board
  ApiResult install(const ImageFacts& f, const uint8_t* head, size_t headN, uint32_t total, BodyReader& body,
                    uint64_t* left, uint8_t* piece) override {
    MemFlash flash;
    SimPacer pacer;
    return runOtaInstall(*this, f, head, headN, total, body, left, piece, flash, pacer);
  }
  void noteRefused(const ApiResult& r) override { gBoard.otaRefused(r); }
 private:
  Mailbox box_;
  TransferGate gate_;
  SaveLimit limit_;
};

static SimBackend gBackend;
static WebApp gApp(gBackend, webui::kPageGz, webui::kPageGzLen);
static WebWork gWork;

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
       "window)</p><h2>The Update page</h2><ul>";
  for (int i = 0; i < sim::Board::kOtaSceneCount; i++) {
    h += "<li><a href='/sim/ota?name=";
    h += sim::Board::kOtaScenes[i];
    h += "'>";
    h += sim::Board::kOtaSceneLabels[i];
    h += "</a></li>";
  }
  h += "</ul>";
  reply(fd, 200, "text/html; charset=utf-8", h.data(), h.size());
}

static void back(int fd) { reply(fd, 302, "text/plain", "", 0, "Location: /sim\r\n"); }

static void handle(int fd) {
  Request rq;
  if (!readHead(fd, &rq)) return;
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
  if (rq.path == "/sim/ota") {
    char v[40];
    if (queryValue(rq.query.c_str(), "name", v, sizeof(v))) gBoard.otaScene(v);
    back(fd);
    return;
  }
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
  // --page: the page from a file (working on web/), not the built-in one
  if (rq.path == "/" && rq.method == "GET" && !gPageFile.empty()) {
    FILE* f = fopen(gPageFile.c_str(), "rb");
    if (!f) { reply(fd, 404, "text/plain", "page file not found", 19); return; }
    std::string s;
    char b[4096];
    size_t k;
    while ((k = fread(b, 1, sizeof(b), f)) > 0) s.append(b, k);
    fclose(f);
    reply(fd, 200, "text/html; charset=utf-8", s.data(), s.size());
    return;
  }
  // the dashboard: the same request handling as the board (lib/core web_app)
  WebRequest w;
  w.known = rq.method == "GET" || rq.method == "POST";
  w.method = rq.method == "POST" ? Method::Post : Method::Get;
  w.path = rq.path.c_str();
  w.query = rq.query.c_str();
  w.contentType = rq.contentType.c_str();
  w.contentLength = rq.contentLength;
  SocketBody body(fd, rq);
  BufferSink out(fd);
  gApp.handle(w, body, out, gWork);
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
