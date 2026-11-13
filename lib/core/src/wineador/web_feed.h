#pragma once
// What the dashboard shows that isn't in the loop's state copy (spec 5.5), kept in RAM (PSRAM on
// the board) and fed by the SD task, never by the control loop:
//   - the graphs' figures (history.h), from the readings and hourly rows as the logger writes them;
//   - the newest events, for the Events page while the card is out (web_lists.h RecentEvents);
//   - the last 7 days' counts (log_read.h WeekCounts).
// After a boot the card's own files refill them in the background (BootRefill below): the tails of
// the hourly and readings files for the graphs, the last two events files for the week's counts.
// Until that has finished the week's counts are not "ready" (the page shows "counting...").
//
// Plain C++, not thread-safe: on the board one lock (the "data lock") is held around each line,
// each refill piece fed, and each piece of a reply that reads it.

#include <stdint.h>
#include <stddef.h>
#include "wineador/history.h"
#include "wineador/log_read.h"
#include "wineador/web_lists.h"
#include "wineador/web_status.h"

namespace wineador {

enum class FeedLine : uint8_t { Readings, Event, Hourly };

class WebFeed {
 public:
  void begin(const History::Storage& h, EventRow* recent, int recentCap);
  // A line as the logger wrote it (newline included or not).
  void line(FeedLine k, const char* text, size_t n);
  History& history() { return history_; }
  const History& history() const { return history_; }
  int recentEvents(EventRow* out, int max) const { return recent_.copyNewest(out, max); }
  // The week's counts into a status copy (null until the refill has counted the card's events).
  void week(int64_t nowUtcS, StatusSnapshot* s) const;
  void weekCounted() { weekReady_ = true; }
  // From the moment the refill lists the card's files, new events are counted as they come; before
  // that they are on the card already (the SD task writes what it holds before the refill lists),
  // so the refill counts them there. An event is never counted twice.
  void countLiveFromNow() { countLive_ = true; }
  bool weekReady() const { return weekReady_; }
  void countEvent(const EventRow& e);   // the refill: an old event row from the card

 private:
  History history_;
  RecentEvents recent_;
  WeekCounts week_;
  bool weekReady_ = false;
  bool countLive_ = false;
};

// The refill after a boot: which pieces of which files to read, oldest first, and what to do with
// them. The SD task asks next() what to read, reads it (no lock), then gives it to feed() under the
// data lock; feed() refuses (false) while a graph reply is being sent, and the same piece is given
// again later.
class BootRefill {
 public:
  static constexpr uint64_t kHourlyTail = 744ull * 420ull;        // a month of hourly rows
  static constexpr uint64_t kReadingsTail = 2ull * 1024 * 1024;   // a day at the 15 s rate (~300 bytes a row)

  // From the card's file list (any order).
  void begin(const LogFileEntry* files, int n);
  bool done() const { return job_ >= nJobs_; }
  // The next piece to read: path, offset and bytes (at most pieceMax). False when finished.
  bool next(const char** path, uint64_t* offset, size_t* n, size_t pieceMax) const;
  // The piece read (got bytes; < 0 a read failure: that file is skipped). False: a reply is open,
  // give the same piece again later.
  bool feed(WebFeed& f, const char* data, int got);
  int jobs() const { return nJobs_; }

 private:
  struct Job {
    FeedLine kind;
    char path[48];
    uint64_t start = 0, end = 0;
    bool firstOfKind = false, lastOfKind = false;
  };
  void add(FeedLine k, const char* path, uint64_t start, uint64_t end);
  void finishJob(WebFeed& f);
  static constexpr int kMaxJobs = 6;
  Job jobs_[kMaxJobs];
  int nJobs_ = 0, job_ = 0;
  uint64_t at_ = 0;
  bool started_ = false;
  FileLineReader reader_;
  struct Ctx { BootRefill* self; WebFeed* feed; bool skipFirst; };
  Ctx ctx_{};
};

}  // namespace wineador
