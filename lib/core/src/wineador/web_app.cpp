#include "wineador/web_app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <new>

namespace wineador {

static constexpr const char* kJson = "application/json; charset=utf-8";

bool WebApp::isLong(const WebRequest& rq) {
  if (!rq.known) return false;
  const Endpoint* e = findEndpoint(rq.method, rq.path);
  return e && (e->route == Route::LogFile || e->route == Route::UpdateUpload);
}

void WebApp::json(ReplySink& out, const char* text, int n) {
  out.begin(200, kJson);
  if (n > 0) out.write(text, (size_t)n);
  out.end();
}

void WebApp::error(ReplySink& out, WebWork& w, const ApiResult& r) {
  const int n = formatError(w.text, sizeof(w.text), r);
  if (r.retryS) {                       // the wait as a number too, for tools (Retry-After, RFC 9110)
    char s[12];
    snprintf(s, sizeof(s), "%lu", (unsigned long)r.retryS);
    out.begin(httpStatus(r.refusal), kJson, "Retry-After", s);
  } else {
    out.begin(httpStatus(r.refusal), kJson);
  }
  if (n > 0) out.write(w.text, (size_t)n);
  out.end();
}

// A JSON reply in pieces. lock: the reply reads the graphs (the SD task feeds them meanwhile).
bool WebApp::pump(ReplySink& out, JsonPump& p, WebWork& w, bool lock) {
  out.begin(200, kJson);
  for (;;) {
    if (lock) b_.lockData();
    const size_t k = p.fill(w.piece, sizeof(w.piece));
    if (lock) b_.unlockData();
    if (k == 0) break;
    if (!out.write(w.piece, k)) return false;
  }
  out.end();
  return true;
}

bool WebApp::readBody(const WebRequest& rq, BodyReader& body, WebWork& w, size_t* n) {
  *n = 0;
  if (rq.contentLength > kMaxJsonBody) return false;
  while (*n < rq.contentLength) {
    const int k = body.read(w.body + *n, (size_t)rq.contentLength - *n, kBodyWaitMs);
    if (k <= 0) break;
    *n += (size_t)k;
  }
  w.body[*n] = 0;
  return true;
}

// Reads a refused upload to its end (up to kMaxDrainBytes) and throws it away, so the browser,
// still sending, gets to see the reason rather than a cut connection.
void WebApp::drain(BodyReader& body, uint64_t left, WebWork& w) {
  if (left > kMaxDrainBytes) return;
  while (left > 0) {
    const int k = body.read(w.piece, (size_t)std::min<uint64_t>(left, sizeof(w.piece)), kUploadStallMs);
    if (k <= 0) return;
    left -= (uint64_t)k;
  }
}

void WebApp::handle(const WebRequest& rq, BodyReader& body, ReplySink& out, WebWork& w) {
  bool known = false;
  const Endpoint* ep = rq.known ? findEndpoint(rq.method, rq.path, &known) : nullptr;
  if (!ep) {
    if (!known) known = findEndpoint(Method::Get, rq.path) || findEndpoint(Method::Post, rq.path);
    error(out, w, refuse(known ? Refusal::MethodNotAllowed : Refusal::NotFound));
    return;
  }
  if (rq.method == Method::Post && ep->route != Route::UpdateUpload && !isJsonContentType(rq.contentType)) {
    error(out, w, refuse(Refusal::NotJson));
    return;
  }
  switch (ep->route) {
    case Route::Page:
      out.begin(200, "text/html; charset=utf-8", "Content-Encoding", "gzip");
      for (size_t at = 0; at < pageLen_; at += kWebPiece)
        if (!out.write((const char*)page_ + at, std::min(kWebPiece, pageLen_ - at))) return;
      out.end();
      return;
    case Route::Info: {
      b_.status(&w.status);
      json(out, w.text, formatInfo(w.text, sizeof(w.text), w.status.build, w.status.uptimeS, w.status.bootReason));
      return;
    }
    case Route::Status: {
      b_.status(&w.status);
      StatusReply r(w.status);
      pump(out, r, w, false);
      return;
    }
    case Route::History: {
      char v[32];
      HistRange range;
      if (!queryValue(rq.query, "range", v, sizeof(v)) || !parseHistRange(v, &range)) {
        error(out, w, refuse(Refusal::BadQuery, "range", "range is hour, day, week or month."));
        return;
      }
      int64_t since = 0;
      if (queryValue(rq.query, "since", v, sizeof(v))) since = strtoll(v, nullptr, 10);
      b_.status(&w.status);
      b_.lockData();
      HistoryReply* r = new (w.reply) HistoryReply(b_.history(), range, w.status.utcNowS, since);
      b_.unlockData();
      pump(out, *r, w, true);
      b_.lockData();
      r->~HistoryReply();      // the graphs move again (rows that waited go in)
      b_.unlockData();
      return;
    }
    case Route::Events: events(rq, out, w); return;
    case Route::Settings: {
      b_.settings(&w.user, &w.tuning);
      SettingsReply r(w.user, w.tuning);
      pump(out, r, w, false);
      return;
    }
    case Route::Logs: logs(out, w); return;
    case Route::LogFile: download(rq, out, w); return;
    case Route::UpdateState:
      b_.updateState(&w.update);
      json(out, w.text, formatUpdateState(w.text, sizeof(w.text), w.update));
      return;
    case Route::SettingsWrite:
    case Route::Exclude:
    case Route::DoorTest: write(rq, ep->route, body, out, w); return;
    case Route::UpdateUpload: upload(rq, body, out, w); return;
    default: error(out, w, refuse(Refusal::NotFound)); return;
  }
}

static_assert(sizeof(HistoryReply) <= sizeof(WebWork::reply), "HistoryReply is built in WebWork::reply");

// ---------------------------------------------------------------- events
namespace {
struct BackCtx { EventRow* rows; int n, max; uint64_t oldest; bool any; };
struct FwdCtx { EventRow* rows; int max, count, head; };
}

void WebApp::events(const WebRequest& rq, ReplySink& out, WebWork& w) {
  char v[160];
  int n = kEventsPage;
  if (queryValue(rq.query, "n", v, sizeof(v))) {
    n = atoi(v);
    if (n < 1 || n > kEventsPage) { error(out, w, refuse(Refusal::BadQuery, "n", "n is 1 to 50.")); return; }
  }
  CardFacts card;
  int nf = b_.card(&card) ? b_.listLogs(w.files, kMaxLogFiles) : -1;
  if (nf < 0) {   // the card is out: the newest kept in RAM, marked (5.5)
    const int k = b_.recentEvents(w.rows, n);
    EventsReply r(w.rows, k, nullptr, nullptr, true);
    pump(out, r, w, false);
    return;
  }
  char path[48];
  uint64_t end = 0, size = 0;
  bool after = false;
  if (queryValue(rq.query, "before", v, sizeof(v))) {
    if (!parseCursor(v, path, sizeof(path), &end)) { error(out, w, refuse(Refusal::BadQuery, "before")); return; }
  } else if (queryValue(rq.query, "after", v, sizeof(v))) {
    if (!parseCursor(v, path, sizeof(path), &end)) { error(out, w, refuse(Refusal::BadQuery, "after")); return; }
    after = true;
  } else {
    if (!newestEventsFile(w.files, nf, path, sizeof(path))) {
      EventsReply r(w.rows, 0, nullptr, nullptr);
      pump(out, r, w, false);
      return;
    }
    end = UINT64_MAX;   // from the end, whatever its size now
  }
  // the file's size now (a read of nothing)
  int got = b_.readLog(path, 0, w.piece, 0, &size);
  if (got == -2) { error(out, w, refuse(Refusal::NotFound)); return; }
  if (got < 0) { error(out, w, refuse(Refusal::CardUnavailable)); return; }
  if (end > size) end = size;
  w.cursorOlder[0] = w.cursorNewest[0] = 0;

  if (after) {
    // new rows since the cursor, the newest n of them, newest first
    FwdCtx c{w.rows, n, 0, 0};
    w.forward.begin(end, [](const char* line, size_t len, uint64_t off, void* p) -> bool {
      FwdCtx* x = (FwdCtx*)p;
      EventRow e;
      if (!parseEventRow(line, len, &e)) return true;
      e.offset = off;
      x->rows[x->head] = e;
      x->head = (x->head + 1) % x->max;
      x->count++;
      return true;
    }, &c);
    uint64_t at = end;
    while (at < size) {
      got = b_.readLog(path, at, w.piece, (size_t)std::min<uint64_t>(size - at, sizeof(w.piece)), nullptr);
      if (got <= 0) { error(out, w, refuse(got == -2 ? Refusal::NotFound : Refusal::CardUnavailable)); return; }
      w.forward.feed(w.piece, (size_t)got);
      at += (uint64_t)got;
    }
    w.forward.finish();
    const int m = std::min(c.count, n);
    const int start = c.count > n ? c.head : 0;    // the oldest kept
    std::rotate(w.rows, w.rows + start, w.rows + m);
    std::reverse(w.rows, w.rows + m);
    // the next poll: the end of this file, or the start of a newer one (a new month)
    char newest[48];
    if (newestEventsFile(w.files, nf, newest, sizeof(newest)) && strcmp(newest, path) != 0)
      formatCursor(w.cursorNewest, sizeof(w.cursorNewest), newest, 0);
    else
      formatCursor(w.cursorNewest, sizeof(w.cursorNewest), path, size);
    EventsReply r(w.rows, m, nullptr, w.cursorNewest);
    pump(out, r, w, false);
    return;
  }

  // newest first, back from `end`, a piece at a time; into older files too
  formatCursor(w.cursorNewest, sizeof(w.cursorNewest), path, size);
  BackCtx c{w.rows, 0, n, end, false};
  bool more = true;
  int pieces = 0;
  for (;;) {
    c.oldest = end;
    c.any = false;
    w.backward.begin([](const char* line, size_t len, uint64_t off, void* p) -> bool {
      BackCtx* x = (BackCtx*)p;
      EventRow e;
      if (!parseEventRow(line, len, &e)) return true;
      e.offset = off;
      x->rows[x->n++] = e;
      x->oldest = off;
      x->any = true;
      return x->n < x->max;
    }, &c);
    uint64_t e2 = end;
    bool stopped = false, capped = false;
    while (e2 > 0 && !stopped) {
      if (++pieces > kEventsMaxPieces) { capped = true; break; }
      const uint64_t s2 = e2 > sizeof(w.piece) ? e2 - sizeof(w.piece) : 0;
      got = b_.readLog(path, s2, w.piece, (size_t)(e2 - s2), nullptr);
      if (got != (int)(e2 - s2)) { error(out, w, refuse(got == -2 ? Refusal::NotFound : Refusal::CardUnavailable)); return; }
      stopped = !w.backward.feed(w.piece, (size_t)got, s2);
      e2 = s2;
    }
    if (capped) break;               // enough card for one request: the next page carries on from here
    if (!stopped) w.backward.finish();
    if (c.n >= n) break;
    char older[48];
    if (!olderEventsFile(w.files, nf, path, older, sizeof(older))) { more = false; break; }
    snprintf(path, sizeof(path), "%s", older);
    if (b_.readLog(path, 0, w.piece, 0, &end) < 0) { more = false; break; }
  }
  if (more && (c.n > 0 || pieces > kEventsMaxPieces)) formatCursor(w.cursorOlder, sizeof(w.cursorOlder), path, c.oldest);
  EventsReply r(w.rows, c.n, w.cursorOlder[0] ? w.cursorOlder : nullptr, w.cursorNewest);
  pump(out, r, w, false);
}

// ---------------------------------------------------------------- logs
void WebApp::logs(ReplySink& out, WebWork& w) {
  CardFacts c;
  const int nf = b_.card(&c) ? b_.listLogs(w.files, kMaxLogFiles) : -1;
  if (nf < 0) { error(out, w, refuse(Refusal::CardUnavailable)); return; }
  sortLogFilesNewestFirst(w.files, nf);
  LogsReply r(c, w.files, nf);
  pump(out, r, w, false);
}

void WebApp::download(const WebRequest& rq, ReplySink& out, WebWork& w) {
  char path[96];
  LogName ln;
  if (!queryValue(rq.query, "path", path, sizeof(path)) || !parseLogPath(path, &ln)) {
    error(out, w, refuse(Refusal::BadPath));
    return;
  }
  CardFacts c;
  if (!b_.card(&c)) { error(out, w, refuse(Refusal::CardUnavailable)); return; }
  const Refusal t = b_.startTransfer(TransferKind::Download);
  if (t != Refusal::None) { error(out, w, refuse(t)); return; }
  uint64_t size = 0;
  int got = b_.readLog(path, 0, w.piece, sizeof(w.piece), &size);
  if (got < 0) {
    b_.endTransfer();
    error(out, w, refuse(got == -2 ? Refusal::NotFound : Refusal::CardUnavailable));
    return;
  }
  // the file as it was when the download started (an events file may grow meanwhile)
  const char* name = strrchr(path, '/') + 1;
  snprintf(w.text, sizeof(w.text), "attachment; filename=\"%s\"", name);
  out.begin(200, "text/csv; charset=utf-8", "Content-Disposition", w.text);
  uint64_t at = 0;
  for (;;) {
    const size_t want = (size_t)std::min<uint64_t>(size - at, (uint64_t)got);
    if (want && !out.write(w.piece, want)) { b_.endTransfer(); return; }   // the browser went
    at += want;
    if (at >= size) break;
    got = b_.readLog(path, at, w.piece, (size_t)std::min<uint64_t>(size - at, sizeof(w.piece)), nullptr);
    if (got <= 0) { b_.endTransfer(); out.abort(); return; }               // the card failed partway
  }
  out.end();
  b_.endTransfer();
}

// ---------------------------------------------------------------- writes
void WebApp::write(const WebRequest& rq, Route r, BodyReader& body, ReplySink& out, WebWork& w) {
  size_t n = 0;
  if (!readBody(rq, body, w, &n)) {
    error(out, w, refuse(Refusal::BadJson, nullptr, "The request body is over 1,024 bytes."));
    return;
  }
  const ApiResult p = parseWriteBody(w.body, n, &w.json);
  if (!p.ok()) { error(out, w, p); return; }
  w.mail = MailRequest{};
  w.mail.kind = r == Route::SettingsWrite ? MailKind::Settings : r == Route::Exclude ? MailKind::Exclude : MailKind::DoorTest;
  w.mail.json = &w.json;
  if (!b_.ask(&w.mail, &w.answer)) { error(out, w, refuse(Refusal::NoAnswer)); return; }
  if (!w.answer.result.ok()) { error(out, w, w.answer.result); return; }
  switch (r) {
    case Route::SettingsWrite: {
      b_.settings(&w.user, &w.tuning);
      SettingsReply s(w.answer.user, w.tuning);
      pump(out, s, w, false);
      return;
    }
    case Route::Exclude:
      json(out, w.text, formatExcludeReply(w.text, sizeof(w.text), w.answer.channel, w.answer.excluded));
      return;
    default:
      json(out, w.text, formatDoorTestReply(w.text, sizeof(w.text)));
      return;
  }
}

// ---------------------------------------------------------------- the firmware upload
void WebApp::upload(const WebRequest& rq, BodyReader& body, ReplySink& out, WebWork& w) {
  uint64_t left = rq.contentLength;
  auto refused = [&](const ApiResult& r, bool readAway) {
    if (readAway) drain(body, left, w);
    b_.noteRefused(r);
    error(out, w, r);
  };
  // 1. The gate, decided by the loop (window, compressor, already updating, still being checked),
  //    before a byte is read.
  w.mail = MailRequest{};
  w.mail.kind = MailKind::UpdateStart;
  w.mail.length = rq.contentLength > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)rq.contentLength;
  w.mail.transferBusy = b_.transferBusy();
  if (!b_.ask(&w.mail, &w.answer)) { refused(refuse(Refusal::NoAnswer), true); return; }
  if (!w.answer.result.ok()) { refused(w.answer.result, true); return; }
  const Refusal t = b_.startTransfer(TransferKind::Update);
  if (t != Refusal::None) { refused(refuse(t), true); return; }
  // 2. The first bytes: the build's identity.
  size_t n = 0;
  while (n < kImageCheckBytes) {
    const int k = body.read((char*)w.head + n, kImageCheckBytes - n, kUploadStallMs);
    if (k <= 0) break;
    n += (size_t)k;
  }
  left -= n;
  if (n < kImageCheckBytes) { b_.endTransfer(); refused(refuse(Refusal::Stalled), false); return; }
  const ImageFacts facts = readImageFacts(w.head, n);
  const ImageVerdict v = judgeImage(facts);
  if (v != ImageVerdict::Accept) { b_.endTransfer(); refused(refuseImage(v), true); return; }
  // 3. Install: started through the loop, written a piece per pass, checked, set to boot.
  const ApiResult r = b_.install(facts, w.head, n, (uint32_t)rq.contentLength, body, &left, (uint8_t*)w.piece);
  // a refusal: the rest read away so the browser sees why; but not after a stall (nothing more is coming)
  if (!r.ok() && r.refusal != Refusal::Stalled) drain(body, left, w);
  b_.endTransfer();
  if (!r.ok()) { error(out, w, r); return; }
  json(out, w.text, formatInstalledReply(w.text, sizeof(w.text), facts.version));
}

}  // namespace wineador
