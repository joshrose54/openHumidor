#include "wineador/web_lists.h"
#include "wineador/image_identity.h"
#include "wineador/event_sentence.h"
#include <stdio.h>

namespace wineador {

void RecentEvents::add(const EventRow& e) {
  if (cap_ <= 0) return;
  if (n_ < cap_) { p_[(head_ + n_) % cap_] = e; n_++; }
  else { p_[head_] = e; head_ = (head_ + 1) % cap_; }
}

int RecentEvents::copyNewest(EventRow* out, int max) const {
  int k = 0;
  for (; k < max && k < n_; k++) out[k] = newest(k);
  return k;
}

bool EventsReply::step(JsonWriter& w) {
  if (phase_ == 0) {
    w.beginObject();
    w.kInt("api", kApiVersion);
    w.kStr("source", recent_ ? "ram" : "card");
    if (recent_) w.kStr("note", kRecentOnlyNote);
    else w.kNull("note");
    w.key("events");
    w.beginArray();
    phase_ = 1;
    return true;
  }
  if (phase_ == 1) {
    if (i_ < n_) {
      const EventRow& r = rows_[i_++];
      char uk[32];
      w.beginObject();
      w.kStr("utc", r.timestamp);
      w.kStr("uk", formatUkDateTime(uk, sizeof(uk), r.utcS));
      w.kBool("trusted", r.trusted);
      w.kStr("type", r.type);
      char sentence[320];
      eventSentence(sentence, sizeof(sentence), r.type, r.detail);
      w.kStr("sentence", sentence);
      w.kStr("detail", r.detail);
      w.endObject();
      return true;
    }
    w.endArray();
    if (!recent_ && older_ && older_[0]) w.kStr("older", older_);
    else w.kNull("older");
    if (!recent_ && newest_ && newest_[0]) w.kStr("newest", newest_);
    else w.kNull("newest");
    w.endObject();
    phase_ = 2;
    return true;
  }
  return false;
}

bool LogsReply::step(JsonWriter& w) {
  if (phase_ == 0) {
    w.beginObject();
    w.kInt("api", kApiVersion);
    w.key("card");
    w.beginObject();
    w.kBool("ok", c_.ok);
    w.kInt("free_bytes", (int64_t)c_.freeBytes);
    w.kInt("total_bytes", (int64_t)c_.totalBytes);
    w.endObject();
    w.key("files");
    w.beginArray();
    phase_ = 1;
    return true;
  }
  if (phase_ == 1) {
    while (i_ < n_) {
      const LogFileEntry& e = f_[i_++];
      LogName ln;
      if (!parseLogPath(e.path, &ln)) continue;   // only real log files are listed
      const char* slash = e.path;
      for (const char* c = e.path; *c; c++)
        if (*c == '/') slash = c + 1;
      w.beginObject();
      w.kStr("path", e.path);
      w.kStr("name", slash);
      w.kStr("kind", logFileKindName(ln.kind));
      if (ln.kind == LogFileKind::Hourly) w.kNull("month");
      else {
        char m[16];
        snprintf(m, sizeof(m), "%04d-%02d", ln.year % 10000, ln.month % 100);
        w.kStr("month", m);
      }
      w.kInt("schema", ln.schema);
      w.kInt("part", ln.part);
      w.kInt("bytes", (int64_t)e.bytes);
      w.endObject();
      return true;
    }
    w.endArray();
    w.endObject();
    phase_ = 2;
    return true;
  }
  return false;
}

const char* updateResultKey(UpdateResult r) {
  switch (r) {
    case UpdateResult::None:       return "none";
    case UpdateResult::Installed:  return "installed";
    case UpdateResult::Refused:    return "refused";
    case UpdateResult::Abandoned:  return "abandoned";
    case UpdateResult::RolledBack: return "rolled_back";
    case UpdateResult::Failed:     return "failed";
  }
  return "?";
}

int formatUpdateState(char* out, size_t n, const UpdateState& u) {
  JsonWriter w;
  w.setBuffer(out, n > 0 ? n - 1 : 0);
  w.beginObject();
  w.kInt("api", kApiVersion);
  w.kBool("window_open", u.windowOpen);
  w.kInt("window_s", u.windowOpen ? u.windowLeftS : 0);
  w.kBool("compressor_running", u.compressorRunning);
  w.kBool("updating", u.updating);
  w.kInt("pct", u.updating ? u.pct : 0);
  w.kInt("slot_bytes", u.slotBytes);
  w.kInt("check_bytes", (int64_t)kImageCheckBytes);
  writeBuild(w, u.running);
  w.kBool("pending_verify", u.pendingVerify);
  w.key("last");
  w.beginObject();
  w.kStr("result", updateResultKey(u.last));
  w.kStr("detail", u.lastDetail);
  if (u.lastUtcS > 0) w.kInt("utc_s", u.lastUtcS);
  else w.kNull("utc_s");
  w.endObject();
  w.endObject();
  if (w.overflowed() || n == 0) return -1;
  out[w.length()] = 0;
  return (int)w.length();
}

}  // namespace wineador
