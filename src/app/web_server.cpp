#include "web_server.h"
#include <stdio.h>

#if defined(WINEADOR_BOARD_REVQ)
#include <Arduino.h>
#include <new>
#include <string.h>
#include <unistd.h>
#include "esp_http_server.h"
#include "mdns.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "board.h"
#include "drivers/sd_log.h"
#include "drivers/heap.h"
#include "drivers/time_sync.h"
#include "web_link.h"
#include "webui/page.h"
#include "wineador/web_app.h"
#include "wineador/web_feed.h"

namespace web {

using namespace wineador;

namespace {

constexpr int kConnections = kMaxBrowserConnections;   // 2 (5.5)
constexpr uint32_t kSdWaitMs = 5000;                   // a card piece through the SD task
constexpr size_t kServerStack = 8192;                  // the server's task (internal RAM)
constexpr size_t kLongStack = 6144;                    // the long-transfer task (internal RAM)

// One server task's own space, in PSRAM: the request's text, the state copy, the work area.
struct Conn {
  char uri[CONFIG_HTTPD_MAX_URI_LEN + 1];
  char query[CONFIG_HTTPD_MAX_URI_LEN + 1];
  char contentType[96];
};
struct Area {
  WebWork work;
  weblink::State state;
  Conn conn;
};

httpd_handle_t gServer = nullptr;
bool gRunning = false;
const char* gProblem = "not started";
bool gMdns = false;

// What the SD task feeds (PSRAM), under gData.
WebFeed* gFeed = nullptr;
SemaphoreHandle_t gData = nullptr;
BootRefill* gRefill = nullptr;
LogFileEntry* gRefillList = nullptr;
char* gRefillPiece = nullptr;
bool gRefillListed = false, gRefillDone = false, gHavePiece = false;
int gRefillGot = 0;

// The connection count (open_fn / close_fn, both on the server's task) and the counters.
portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
int gFds[kConnections] = {-1, -1};
int gOpen = 0;
int gRefused[2] = {-1, -1};             // third connections, being closed
uint32_t gThirdClosed = 0, gRequests = 0, gLongRequests = 0, gBusyRefused = 0, gWorstMs = 0;
TransferGate gGate;                     // under gMux (the server's task and the long task)
volatile bool gLongBusy = false;
TaskHandle_t gServerTask = nullptr, gLongTask = nullptr;
QueueHandle_t gLongQ = nullptr;

// One object in PSRAM, made in place (once, at boot; never freed).
template <typename T, typename... A> T* inPsram(A&&... args) {
  void* p = heap_caps_malloc(sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? new (p) T(static_cast<A&&>(args)...) : nullptr;
}
template <typename T> T* arrayInPsram(int n) {
  T* p = (T*)heap_caps_malloc((size_t)n * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p) for (int i = 0; i < n; i++) new (&p[i]) T();
  return p;
}

// ---------------------------------------------------------------- the board, as the server sees it
class BoardBackend : public WebBackend {
 public:
  explicit BoardBackend(Area* a) : a_(a) {}
  void status(StatusSnapshot* out) override {
    weblink::copy(&a_->state);
    *out = a_->state.status;
    lockData();
    gFeed->week(out->utcNowS, out);      // the week's counts, from the feed
    unlockData();
    out->wifiRssi = timesync::rssi();     // here, not in the loop: the WiFi driver's call can wait
  }
  void settings(UserSettings* u, TuningSettings* t) override {
    weblink::copy(&a_->state);
    *u = a_->state.user;
    *t = a_->state.tuning;
  }
  void updateState(UpdateState* out) override {
    weblink::copy(&a_->state);
    *out = a_->state.update;
  }
  History& history() override { return gFeed->history(); }
  void lockData() override { xSemaphoreTake(gData, portMAX_DELAY); }   // the SD task holds it for one line or piece
  void unlockData() override { xSemaphoreGive(gData); }
  int recentEvents(EventRow* out, int max) override {
    lockData();
    const int n = gFeed->recentEvents(out, max);
    unlockData();
    return n;
  }
  bool card(CardFacts* c) override {
    const sdlog::Stats s = sdlog::stats();
    c->ok = s.mounted && !s.failed;
    c->freeBytes = s.freeBytes;
    c->totalBytes = s.totalBytes;
    return c->ok;
  }
  int listLogs(LogFileEntry* out, int max) override { return sdlog::listLogs(out, max, kSdWaitMs); }
  int readLog(const char* path, uint64_t off, char* buf, size_t n, uint64_t* size) override {
    return sdlog::readPiece(path, off, buf, n, size, kSdWaitMs);
  }
  bool ask(MailRequest* r, MailAnswer* a) override { return weblink::ask(r, a); }
  Refusal startTransfer(TransferKind k) override {
    const uint32_t freeInternal = heap::freeNow();
    portENTER_CRITICAL(&gMux);
    const Refusal r = gGate.tryStart(k, freeInternal, board::kTransferMarginBytes);
    portEXIT_CRITICAL(&gMux);
    return r;
  }
  void endTransfer() override {
    portENTER_CRITICAL(&gMux);
    gGate.finish();
    portEXIT_CRITICAL(&gMux);
  }
  bool transferBusy() override {
    portENTER_CRITICAL(&gMux);
    const bool b = gGate.busy();
    portEXIT_CRITICAL(&gMux);
    return b;
  }
  // Stage Q5c: the flash writes come with Q5d. Nothing is read or written here.
  ApiResult install(const ImageFacts&, BodyReader&, uint64_t*) override { return refuse(Refusal::NotBuilt); }

 private:
  Area* a_;
};

Area* gMainArea = nullptr;
Area* gLongArea = nullptr;
BoardBackend* gMainBackend = nullptr;
BoardBackend* gLongBackend = nullptr;
WebApp* gMainApp = nullptr;
WebApp* gLongApp = nullptr;

// ---------------------------------------------------------------- the connection, for web_app
const char* reasonOf(int code) {
  switch (code) {
    case 200: return "200 OK"; case 400: return "400 Bad Request"; case 403: return "403 Forbidden";
    case 404: return "404 Not Found"; case 405: return "405 Method Not Allowed"; case 408: return "408 Request Timeout";
    case 409: return "409 Conflict"; case 413: return "413 Payload Too Large"; case 415: return "415 Unsupported Media Type";
    case 422: return "422 Unprocessable Entity"; case 429: return "429 Too Many Requests"; case 501: return "501 Not Implemented";
    case 503: return "503 Service Unavailable";
  }
  return "500 Internal Server Error";
}

// The reply in chunks as it goes. Every header string stays here until the reply is sent.
class HttpSink : public ReplySink {
 public:
  explicit HttpSink(httpd_req_t* r) : r_(r) {}
  void begin(int status, const char* type, const char* name, const char* value) override {
    httpd_resp_set_status(r_, reasonOf(status));
    snprintf(type_, sizeof(type_), "%s", type);
    httpd_resp_set_type(r_, type_);
    httpd_resp_set_hdr(r_, "Connection", "close");       // every reply closes its connection (5.5)
    httpd_resp_set_hdr(r_, "Cache-Control", "no-store");
    if (name && value) {                                 // no CORS header, ever: no other site may write
      snprintf(name_, sizeof(name_), "%s", name);
      snprintf(value_, sizeof(value_), "%s", value);
      httpd_resp_set_hdr(r_, name_, value_);
    }
  }
  bool write(const char* p, size_t n) override {
    if (failed_) return false;
    if (httpd_resp_send_chunk(r_, p, (ssize_t)n) != ESP_OK) failed_ = true;
    return !failed_;
  }
  void end() override { if (!failed_) httpd_resp_send_chunk(r_, nullptr, 0); }
  void abort() override { failed_ = true; }       // no last chunk: the browser sees a failed transfer
  bool failed() const { return failed_; }

 private:
  httpd_req_t* r_;
  char type_[48] = "", name_[32] = "", value_[128] = "";
  bool failed_ = false;
};

class HttpBody : public BodyReader {
 public:
  explicit HttpBody(httpd_req_t* r) : r_(r) {}
  uint64_t bytesRead() const { return read_; }
  int read(char* p, size_t n, uint32_t waitMs) override {
    const uint32_t start = millis();
    for (;;) {
      const int k = httpd_req_recv(r_, p, n);
      if (k > 0) { read_ += (uint64_t)k; return k; }
      if (k == 0) return 0;
      if (k == HTTPD_SOCK_ERR_TIMEOUT && millis() - start < waitMs) continue;   // 5 s each; up to waitMs
      return -1;
    }
  }

 private:
  httpd_req_t* r_;
  uint64_t read_ = 0;
};

void makeRequest(httpd_req_t* req, Conn& c, WebRequest* w) {
  snprintf(c.uri, sizeof(c.uri), "%s", req->uri);
  char* q = strchr(c.uri, '?');
  if (q) *q = 0;
  if (httpd_req_get_url_query_str(req, c.query, sizeof(c.query)) != ESP_OK) c.query[0] = 0;
  if (httpd_req_get_hdr_value_str(req, "Content-Type", c.contentType, sizeof(c.contentType)) != ESP_OK) c.contentType[0] = 0;
  w->known = req->method == HTTP_GET || req->method == HTTP_POST;
  w->method = req->method == HTTP_POST ? Method::Post : Method::Get;
  w->path = c.uri;
  w->query = c.query;
  w->contentType = c.contentType;
  w->contentLength = req->content_len;
}

bool refusedFd(int fd);
void cutLeftover(httpd_req_t* req, uint64_t bodyRead);

void closeAfter(httpd_req_t* req) { httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req)); }

void noteTime(uint32_t startMs) {
  const uint32_t ms = millis() - startMs;
  portENTER_CRITICAL(&gMux);
  if (ms > gWorstMs) gWorstMs = ms;
  portEXIT_CRITICAL(&gMux);
}

// Every request comes here, on the server's task.
esp_err_t onRequest(httpd_req_t* req) {
  const uint32_t start = millis();
  gServerTask = xTaskGetCurrentTaskHandle();
  if (refusedFd(httpd_req_to_sockfd(req))) return ESP_FAIL;   // a third connection: no reply, closed
  gRequests++;
  WebRequest w;
  makeRequest(req, gMainArea->conn, &w);
  if (WebApp::isLong(w)) {
    if (gLongBusy) {
      // one long transfer at a time (5.5): refused at once, never queued behind the running one
      gBusyRefused++;
      HttpSink s(req);
      char b[200];
      const int n = formatError(b, sizeof(b), refuse(Refusal::Busy));
      s.begin(503, "application/json; charset=utf-8", nullptr, nullptr);
      s.write(b, (size_t)(n > 0 ? n : 0));
      s.end();
      cutLeftover(req, 0);
      closeAfter(req);
      return ESP_OK;
    }
    httpd_req_t* copy = nullptr;
    if (httpd_req_async_handler_begin(req, &copy) == ESP_OK) {
      gLongBusy = true;
      gLongRequests++;
      if (xQueueSend(gLongQ, &copy, 0) == pdTRUE) return ESP_OK;   // the long task has it
      gLongBusy = false;
      httpd_req_async_handler_complete(copy);
    }
    // could not hand it over: refused, as busy
    HttpSink s(req);
    char b[200];
    const int n = formatError(b, sizeof(b), refuse(Refusal::Busy));
    s.begin(503, "application/json; charset=utf-8", nullptr, nullptr);
    s.write(b, (size_t)(n > 0 ? n : 0));
    s.end();
    cutLeftover(req, 0);
    closeAfter(req);
    return ESP_OK;
  }
  HttpBody body(req);
  HttpSink out(req);
  gMainApp->handle(w, body, out, gMainArea->work);
  cutLeftover(req, body.bytesRead());
  closeAfter(req);
  noteTime(start);
  return out.failed() ? ESP_FAIL : ESP_OK;
}

// Other methods (PUT, DELETE, OPTIONS ...): the JSON errors of docs/api.md, not the server's text.
esp_err_t onWrongMethod(httpd_req_t* req, httpd_err_code_t) {
  WebRequest w;
  makeRequest(req, gMainArea->conn, &w);
  w.known = false;
  HttpBody body(req);
  HttpSink out(req);
  gMainApp->handle(w, body, out, gMainArea->work);
  cutLeftover(req, body.bytesRead());
  closeAfter(req);
  return ESP_OK;
}

// The long requests (a log download, a firmware upload), one at a time, on their own task.
void longTask(void*) {
  gLongTask = xTaskGetCurrentTaskHandle();
  for (;;) {
    httpd_req_t* req = nullptr;
    if (xQueueReceive(gLongQ, &req, portMAX_DELAY) != pdTRUE || !req) continue;
    const uint32_t start = millis();
    WebRequest w;
    makeRequest(req, gLongArea->conn, &w);
    HttpBody body(req);
    HttpSink out(req);
    gLongApp->handle(w, body, out, gLongArea->work);
    cutLeftover(req, body.bytesRead());   // beyond the 4 MB read away (too big): cut, not read
    closeAfter(req);
    httpd_req_async_handler_complete(req);
    noteTime(start);
    gLongBusy = false;
  }
}

// At most 2 connections: a third is closed at once, never queued (5.5). Both hooks run on the
// server's task. The third is let in and then closed by the server itself (httpd_sess_trigger_close):
// refusing it here instead would have the server close the socket twice (its own close after
// "session creation failed", and this close hook). A request that arrives on it first is cut.
bool refusedFd(int fd) {
  portENTER_CRITICAL(&gMux);
  const bool r = gRefused[0] == fd || gRefused[1] == fd;
  portEXIT_CRITICAL(&gMux);
  return r;
}

esp_err_t onOpen(httpd_handle_t hd, int fd) {
  bool ok = false, refused = false;
  portENTER_CRITICAL(&gMux);
  for (int i = 0; i < kConnections && !ok; i++)
    if (gFds[i] < 0) { gFds[i] = fd; ok = true; }
  if (ok) {
    gOpen++;
  } else {
    gThirdClosed++;
    for (int i = 0; i < 2 && !refused; i++)
      if (gRefused[i] < 0) { gRefused[i] = fd; refused = true; }
  }
  portEXIT_CRITICAL(&gMux);
  if (!ok) httpd_sess_trigger_close(hd, fd);   // closed on the server's next turn, by the server
  return ESP_OK;
}

void onClose(httpd_handle_t, int fd) {
  portENTER_CRITICAL(&gMux);
  for (int i = 0; i < kConnections; i++)
    if (gFds[i] == fd) { gFds[i] = -1; gOpen--; }
  for (int i = 0; i < 2; i++)
    if (gRefused[i] == fd) gRefused[i] = -1;
  portEXIT_CRITICAL(&gMux);
  close(fd);                         // with close_fn set, closing the socket is ours to do (once)
}

// A body the request left unread: the server would read it away on its own task before closing
// (up to a whole firmware image). Instead the socket's reads now fail, so the server closes the
// connection at once; the reply has already gone.
int cutRecv(httpd_handle_t, int, char*, size_t, int) { return HTTPD_SOCK_ERR_FAIL; }

void cutLeftover(httpd_req_t* req, uint64_t bodyRead) {
  if (bodyRead < req->content_len) httpd_sess_set_recv_override(req->handle, httpd_req_to_sockfd(req), cutRecv);
}

// ---------------------------------------------------------------- the SD task's hooks
void onLine(int kind, const char* line, size_t n) {
  xSemaphoreTake(gData, portMAX_DELAY);
  gFeed->line((FeedLine)kind, line, n);
  xSemaphoreGive(gData);
}

void refillFinished() {
  xSemaphoreTake(gData, portMAX_DELAY);
  gFeed->weekCounted();
  xSemaphoreGive(gData);
  gRefillDone = true;
}

// One piece of the boot refill a pass, on the SD task, after its writes. The read is made without
// the data lock; the lines are fed under it (and held back while a graph reply is being sent).
bool refillStep() {
  if (gRefillDone) return false;
  if (!gRefillListed) {
    const int n = sdlog::listHere(gRefillList, kMaxLogFiles);
    if (n < 0) return false;          // the card isn't working: tried again later
    gRefill->begin(gRefillList, n);
    gRefillListed = true;
    xSemaphoreTake(gData, portMAX_DELAY);
    gFeed->countLiveFromNow();        // what came before is on the card: the refill counts it there
    xSemaphoreGive(gData);
    if (gRefill->done()) refillFinished();
    return !gRefillDone;
  }
  if (!gHavePiece) {
    const char* path;
    uint64_t off;
    size_t k;
    if (!gRefill->next(&path, &off, &k, kWebPiece)) { refillFinished(); return false; }
    gRefillGot = sdlog::readHere(path, off, gRefillPiece, k, nullptr);
    gHavePiece = true;
  }
  xSemaphoreTake(gData, portMAX_DELAY);
  const bool fed = gRefill->feed(*gFeed, gRefillPiece, gRefillGot);
  xSemaphoreGive(gData);
  if (fed) gHavePiece = false;
  if (gRefill->done()) refillFinished();
  return !gRefillDone;
}

bool fail(const char* why) {
  gProblem = why;
  return false;
}

}  // namespace

bool begin() {
  // BOOT ONLY. Everything big in PSRAM: the graphs (227 KB), the events kept in RAM, two work areas.
  HistPoint* raw = arrayInPsram<HistPoint>(400);
  HistPoint* day = arrayInPsram<HistPoint>(1440);
  HistPoint* hourly = arrayInPsram<HistPoint>(744);
  EventRow* recent = arrayInPsram<EventRow>(kEventsPage);
  gFeed = inPsram<WebFeed>();
  gRefill = inPsram<BootRefill>();
  gRefillList = arrayInPsram<LogFileEntry>(kMaxLogFiles);
  gRefillPiece = arrayInPsram<char>((int)kWebPiece);
  gMainArea = inPsram<Area>();
  gLongArea = inPsram<Area>();
  if (!raw || !day || !hourly || !recent || !gFeed || !gRefill || !gRefillList || !gRefillPiece || !gMainArea || !gLongArea)
    return fail("no PSRAM for the dashboard");
  History::Storage st;
  st.raw = raw; st.rawCap = 400;
  st.day = day; st.dayCap = 1440;
  st.hourly = hourly; st.hourlyCap = 744;
  gFeed->begin(st, recent, kEventsPage);
  gData = xSemaphoreCreateMutex();
  if (!gData || !weblink::begin()) return fail("no memory for the dashboard's locks or state copy");
  gMainBackend = inPsram<BoardBackend>(gMainArea);
  gLongBackend = inPsram<BoardBackend>(gLongArea);
  if (!gMainBackend || !gLongBackend) return fail("no PSRAM for the dashboard");
  gMainApp = inPsram<WebApp>(*gMainBackend, webui::kPageGz, webui::kPageGzLen);
  gLongApp = inPsram<WebApp>(*gLongBackend, webui::kPageGz, webui::kPageGzLen);
  if (!gMainApp || !gLongApp) return fail("no PSRAM for the dashboard");

  // The SD task feeds the graphs from here on (it writes nothing until the clock is set).
  sdlog::setHooks(onLine, refillStep);

  // The long-transfer task: core 0, below the server.
  gLongQ = xQueueCreate(1, sizeof(httpd_req_t*));
  if (!gLongQ || xTaskCreatePinnedToCore(longTask, "weblong", kLongStack, nullptr, 4, nullptr, 0) != pdPASS)
    return fail("could not start the long-transfer task");

  httpd_config_t c = HTTPD_DEFAULT_CONFIG();
  c.core_id = 0;                      // the control loop is on core 1
  c.task_priority = 5;
  c.stack_size = kServerStack;
  c.max_open_sockets = kConnections + 1;   // the third is seen, and closed by onOpen (5.5)
  c.lru_purge_enable = false;              // never close an open connection to make room
  c.backlog_conn = 2;
  c.max_uri_handlers = 2;
  c.uri_match_fn = httpd_uri_match_wildcard;
  c.open_fn = onOpen;
  c.close_fn = onClose;
  c.recv_wait_timeout = 5;
  c.send_wait_timeout = 5;
  if (httpd_start(&gServer, &c) != ESP_OK) return fail("the web server didn't start");
  httpd_uri_t get = {};
  get.uri = "/*";
  get.method = HTTP_GET;
  get.handler = onRequest;
  httpd_uri_t post = get;
  post.method = HTTP_POST;
  httpd_register_uri_handler(gServer, &get);
  httpd_register_uri_handler(gServer, &post);
  httpd_register_err_handler(gServer, HTTPD_405_METHOD_NOT_ALLOWED, onWrongMethod);
  httpd_register_err_handler(gServer, HTTPD_404_NOT_FOUND, onWrongMethod);

  // wineador.local (boot only: adding the service can wait up to 2 s)
  gMdns = mdns_init() == ESP_OK && mdns_hostname_set("wineador") == ESP_OK;
  if (gMdns) {
    mdns_instance_name_set("openHumidor");
    mdns_service_add("openHumidor", "_http", "_tcp", 80, nullptr, 0);
  }
  gRunning = true;
  gProblem = gMdns ? nullptr : "mDNS didn't start: use the board's address, not wineador.local";
  return true;
}

bool running() { return gRunning; }
const char* problem() { return gProblem; }

void formatStatus(char* out, size_t n) {
  if (!gRunning) { snprintf(out, n, "web OFF (%s)", gProblem ? gProblem : "?"); return; }
  portENTER_CRITICAL(&gMux);
  const int open = gOpen;
  const uint32_t third = gThirdClosed, worst = gWorstMs;
  const TransferKind k = gGate.kind();
  portEXIT_CRITICAL(&gMux);
  const unsigned serverFree = gServerTask ? (unsigned)uxTaskGetStackHighWaterMark(gServerTask) : 0;
  const unsigned longFree = gLongTask ? (unsigned)uxTaskGetStackHighWaterMark(gLongTask) : 0;
  snprintf(out, n,
           "web %s | connections %d/%d, third closed %lu | requests %lu (long %lu, busy refused %lu), worst %lu ms | "
           "transfer %s | refill %s | stack left: server %u B, long %u B, SD task %lu B | internal RAM free %lu B (lowest %lu, largest %lu)",
           gMdns ? "wineador.local" : "(no mDNS)", open, kConnections, (unsigned long)third, (unsigned long)gRequests,
           (unsigned long)gLongRequests, (unsigned long)gBusyRefused, (unsigned long)worst,
           k == TransferKind::Download ? "download" : k == TransferKind::Update ? "upload" : "none",
           gRefillDone ? "done" : gRefillListed ? "running" : "waiting for the card",
           serverFree, longFree, (unsigned long)sdlog::stackLeft(), (unsigned long)heap::freeNow(), (unsigned long)heap::lowest(),
           (unsigned long)heap::largestBlock());
}

}  // namespace web

#else   // Rev N: no dashboard

namespace web {
bool begin() { return false; }
bool running() { return false; }
const char* problem() { return "no dashboard on this board"; }
void formatStatus(char* out, size_t n) { snprintf(out, n, "web: no dashboard on this board (Rev Q only)"); }
}  // namespace web

#endif
