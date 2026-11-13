#include "wineador/web_feed.h"
#include <stdio.h>
#include <string.h>

namespace wineador {

void WebFeed::begin(const History::Storage& h, EventRow* recent, int recentCap) {
  history_.begin(h);
  recent_.begin(recent, recentCap);
  week_ = WeekCounts{};
  weekReady_ = false;
  countLive_ = false;
}

void WebFeed::countEvent(const EventRow& e) {
  if (!e.trusted) return;
  const WeekKind k = weekKindOf(e.type, e.detail);
  if (k != WeekKind::None) week_.add(e.utcS, k);
}

void WebFeed::line(FeedLine k, const char* text, size_t n) {
  switch (k) {
    case FeedLine::Readings: history_.addReadingsLine(text, n); break;
    case FeedLine::Hourly: history_.addHourlyLine(text, n); break;
    case FeedLine::Event: {
      EventRow e;
      if (!parseEventRow(text, n, &e)) break;
      recent_.add(e);
      if (countLive_) countEvent(e);
      break;
    }
  }
}

void WebFeed::week(int64_t nowUtcS, StatusSnapshot* s) const {
  s->weekReady = weekReady_;
  if (weekReady_) week_.counts(nowUtcS, &s->crossovers, &s->strips, &s->humidRuns, &s->disturbances);
}

// ---------------------------------------------------------------- the refill
void BootRefill::add(FeedLine k, const char* path, uint64_t start, uint64_t end) {
  if (nJobs_ >= kMaxJobs || end <= start) return;
  Job& j = jobs_[nJobs_++];
  j.kind = k;
  snprintf(j.path, sizeof(j.path), "%s", path);
  j.start = start;
  j.end = end;
}

void BootRefill::begin(const LogFileEntry* files, int n) {
  nJobs_ = job_ = 0;
  at_ = 0;
  started_ = false;
  // the newest and next-newest file of each kind (by logOrderKey)
  int best[3][2] = {{-1, -1}, {-1, -1}, {-1, -1}};
  int64_t key[3][2] = {};
  for (int i = 0; i < n; i++) {
    LogName ln;
    if (!parseLogPath(files[i].path, &ln) || ln.schema < 2) continue;   // the graphs read schema 2 rows
    const int k = (int)ln.kind;
    const int64_t o = logOrderKey(ln);
    if (best[k][0] < 0 || o > key[k][0]) {
      best[k][1] = best[k][0]; key[k][1] = key[k][0];
      best[k][0] = i; key[k][0] = o;
    } else if (best[k][1] < 0 || o > key[k][1]) {
      best[k][1] = i; key[k][1] = o;
    }
  }
  const int hr = best[(int)LogFileKind::Hourly][0];
  const int r0 = best[(int)LogFileKind::Readings][0], r1 = best[(int)LogFileKind::Readings][1];
  const int e0 = best[(int)LogFileKind::Events][0], e1 = best[(int)LogFileKind::Events][1];
  // the graphs: a month of hourly rows; a day of readings (into the month before, if this one is young)
  if (hr >= 0) add(FeedLine::Hourly, files[hr].path, tailStart(files[hr].bytes, kHourlyTail), files[hr].bytes);
  if (r0 >= 0) {
    const uint64_t have = files[r0].bytes;
    if (r1 >= 0 && have < kReadingsTail)
      add(FeedLine::Readings, files[r1].path, tailStart(files[r1].bytes, kReadingsTail - have), files[r1].bytes);
    add(FeedLine::Readings, files[r0].path, tailStart(have, kReadingsTail), have);
  }
  // the week's counts: the last two events files whole (a month's events are ~100 KB)
  if (e1 >= 0) add(FeedLine::Event, files[e1].path, 0, files[e1].bytes);
  if (e0 >= 0) add(FeedLine::Event, files[e0].path, 0, files[e0].bytes);
  for (int i = 0; i < nJobs_; i++) {
    jobs_[i].firstOfKind = i == 0 || jobs_[i - 1].kind != jobs_[i].kind;
    jobs_[i].lastOfKind = i == nJobs_ - 1 || jobs_[i + 1].kind != jobs_[i].kind;
  }
}

bool BootRefill::next(const char** path, uint64_t* offset, size_t* n, size_t pieceMax) const {
  if (done()) return false;
  const Job& j = jobs_[job_];
  const uint64_t from = started_ ? at_ : j.start;
  *path = j.path;
  *offset = from;
  const uint64_t left = j.end - from;
  *n = (size_t)(left < pieceMax ? left : pieceMax);
  return true;
}

void BootRefill::finishJob(WebFeed& f) {
  const Job& j = jobs_[job_];
  if (started_) reader_.finish();
  if (j.lastOfKind) {
    if (j.kind == FeedLine::Readings) f.history().endRefill(History::Kind::Readings);
    if (j.kind == FeedLine::Hourly) f.history().endRefill(History::Kind::Hourly);
    if (j.kind == FeedLine::Event) f.weekCounted();
  }
  job_++;
  started_ = false;
}

bool BootRefill::feed(WebFeed& f, const char* data, int got) {
  if (done()) return true;
  Job& j = jobs_[job_];
  const bool graphs = j.kind != FeedLine::Event;
  if (graphs && f.history().replyOpen()) return false;
  if (!started_) {
    if (j.firstOfKind && graphs &&
        !f.history().beginRefill(j.kind == FeedLine::Readings ? History::Kind::Readings : History::Kind::Hourly))
      return false;
    started_ = true;
    at_ = j.start;
    ctx_ = Ctx{this, &f, j.start > 0};   // a tail starts mid-line: its first line is skipped
    reader_.begin(j.start, [](const char* line, size_t len, uint64_t, void* p) -> bool {
      Ctx* c = (Ctx*)p;
      if (c->skipFirst) { c->skipFirst = false; return true; }
      const Job& jj = c->self->jobs_[c->self->job_];
      switch (jj.kind) {
        case FeedLine::Readings: c->feed->history().refillLine(History::Kind::Readings, line, len); break;
        case FeedLine::Hourly: c->feed->history().refillLine(History::Kind::Hourly, line, len); break;
        case FeedLine::Event: {
          EventRow e;
          if (parseEventRow(line, len, &e)) c->feed->countEvent(e);
          break;
        }
      }
      return true;
    }, &ctx_);
  }
  if (got <= 0) {               // the file couldn't be read: skip the rest of it
    finishJob(f);
    return true;
  }
  reader_.feed(data, (size_t)got);
  at_ += (uint64_t)got;
  if (at_ >= j.end) finishJob(f);
  return true;
}

}  // namespace wineador
