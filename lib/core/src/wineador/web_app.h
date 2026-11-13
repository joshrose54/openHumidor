#pragma once
// The web server's request handling (spec 5.5, docs/api.md), the same code on the board
// (src/app/web_server.cpp, on ESP-IDF's esp_http_server) and in the Mac review build (src/sim), so
// the two can't answer differently. Plain C++, native tests (test_web_app).
//
// It never touches the control objects. Everything it needs comes through WebBackend:
//   - the state copy the control loop writes about once a second (status, settings, update);
//   - the graphs' figures and the events kept in RAM, fed by the SD task (lockData around each use);
//   - the card, a piece at a time through the SD task, between its own log writes (5.2: no card
//     read in a handler);
//   - the mailbox (web_mail.h): writes and the start of an upload are decided by the loop;
//   - the transfer gate: one long transfer (a download or an upload) at a time.
// Every reply is written in pieces (at most kWebPiece bytes each) into the server's work area
// (WebWork, in PSRAM on the board), so no reply needs one big buffer.

#include <stdint.h>
#include <stddef.h>
#include "wineador/web_api.h"
#include "wineador/web_mail.h"
#include "wineador/web_status.h"
#include "wineador/web_lists.h"
#include "wineador/history.h"
#include "wineador/log_read.h"
#include "wineador/ota_rules.h"
#include "wineador/image_identity.h"
#include "wineador/json_in.h"

namespace wineador {

constexpr size_t kWebPiece = 4096;          // one piece of a reply, and one card read
constexpr int kMaxLogFiles = 96;            // files listed on the Logs page (8 years of months)
constexpr uint32_t kBodyWaitMs = 5000;      // a write's body: each read waits at most this
constexpr int kEventsMaxPieces = 256;       // one Events request reads at most 1 MB of the card
constexpr uint64_t kMaxDrainBytes = 4u * 1024u * 1024u;   // a refused upload is read to its end up to this

struct WebRequest {
  bool known = true;                // GET or POST (anything else: method_not_allowed / not_found)
  Method method = Method::Get;
  const char* path = "";            // without the ?query
  const char* query = "";
  const char* contentType = "";
  uint64_t contentLength = 0;
};

// The request's body, from the connection.
class BodyReader {
 public:
  virtual ~BodyReader() {}
  // Bytes read (> 0); 0 at the end of the body; -1 if nothing came within waitMs or the browser went.
  virtual int read(char* p, size_t n, uint32_t waitMs) = 0;
};

// The reply, to the connection. Every reply closes its connection (5.5) and carries no CORS header.
class ReplySink {
 public:
  virtual ~ReplySink() {}
  // extraName/extraValue: one more header (Content-Encoding, Content-Disposition), or nullptr.
  virtual void begin(int status, const char* contentType, const char* extraName = nullptr,
                     const char* extraValue = nullptr) = 0;
  virtual bool write(const char* p, size_t n) = 0;   // false: the browser has gone
  virtual void end() = 0;                            // the reply is complete
  virtual void abort() = 0;                          // cut the connection: a download that failed partway
};

class WebBackend {
 public:
  virtual ~WebBackend() {}
  // The state copy (written by the loop about once a second; the week's counts added from the feed).
  virtual void status(StatusSnapshot* out) = 0;
  virtual void settings(UserSettings* u, TuningSettings* t) = 0;
  virtual void updateState(UpdateState* out) = 0;
  // The graphs and the events kept in RAM (shared with the SD task that feeds them).
  virtual History& history() = 0;
  virtual void lockData() = 0;
  virtual void unlockData() = 0;
  virtual int recentEvents(EventRow* out, int max) = 0;   // newest first; takes the lock itself
  // The card, through the SD task.
  virtual bool card(CardFacts* out) = 0;                  // false: failed or out
  virtual int listLogs(LogFileEntry* out, int max) = 0;   // files found; -1 the card failed
  // Up to n bytes of a log file from offset; *fileBytes its size now. -1 the card failed, -2 no such file.
  virtual int readLog(const char* path, uint64_t offset, char* buf, size_t n, uint64_t* fileBytes) = 0;
  // The mailbox: false if the loop didn't answer within kMailWaitMs.
  virtual bool ask(MailRequest* r, MailAnswer* a) = 0;
  // The transfer gate.
  virtual Refusal startTransfer(TransferKind k) = 0;
  virtual void endTransfer() = 0;
  virtual bool transferBusy() = 0;
  // An image that passed every check: its first headN bytes in head, the rest of its body still to
  // read (*bodyLeft bytes; lowered by what it reads), `total` bytes in all; `piece` a kWebPiece
  // buffer. Starts the update through the loop (UpdateBegin), writes it (ota_update.h), ends it
  // (UpdateEnd). On a refusal the caller reads the rest away.
  virtual ApiResult install(const ImageFacts& f, const uint8_t* head, size_t headN, uint32_t total, BodyReader& body,
                            uint64_t* bodyLeft, uint8_t* piece) = 0;
  // An upload refused (logged by the loop: 5.4, at most 10 an hour). Not for an update that started
  // and then failed: the loop logs that itself.
  virtual void noteRefused(const ApiResult& r) { (void)r; }
};

// The server's scratch space for one request at a time (one per server task; PSRAM on the board).
struct WebWork {
  StatusSnapshot status;
  UserSettings user;
  TuningSettings tuning;
  UpdateState update;
  FlatJson json;
  char body[kMaxJsonBody + 1];
  EventRow rows[kEventsPage];
  LogFileEntry files[kMaxLogFiles];
  char piece[kWebPiece];
  FileLineReader forward;
  BackwardLineReader backward;
  uint8_t head[kImageCheckBytes];
  char text[1024];
  char cursorOlder[96], cursorNewest[96];
  MailRequest mail;
  MailAnswer answer;
  alignas(16) unsigned char reply[256];   // a HistoryReply, built here while it is sent
};

class WebApp {
 public:
  WebApp(WebBackend& b, const uint8_t* pageGz, size_t pageLen) : b_(b), page_(pageGz), pageLen_(pageLen) {}
  void handle(const WebRequest& rq, BodyReader& body, ReplySink& out, WebWork& w);

  // The routes that may take minutes (the board runs them on their own task, so the server's task
  // stays free for the other connection).
  static bool isLong(const WebRequest& rq);

 private:
  void error(ReplySink& out, WebWork& w, const ApiResult& r);
  void json(ReplySink& out, const char* text, int n);
  bool pump(ReplySink& out, JsonPump& p, WebWork& w, bool lock);
  void events(const WebRequest& rq, ReplySink& out, WebWork& w);
  void logs(ReplySink& out, WebWork& w);
  void download(const WebRequest& rq, ReplySink& out, WebWork& w);
  void write(const WebRequest& rq, Route r, BodyReader& body, ReplySink& out, WebWork& w);
  void upload(const WebRequest& rq, BodyReader& body, ReplySink& out, WebWork& w);
  bool readBody(const WebRequest& rq, BodyReader& body, WebWork& w, size_t* n);
  void drain(BodyReader& body, uint64_t left, WebWork& w);
  WebBackend& b_;
  const uint8_t* page_;
  size_t pageLen_;
};

}  // namespace wineador
