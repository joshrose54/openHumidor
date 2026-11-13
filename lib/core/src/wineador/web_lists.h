#pragma once
// The replies for the Events, Logs and Update pages (spec 5.5, docs/api.md). The SD task reads
// the rows and the file list from the card (log_read.h); these write the JSON, a step at a time.

#include <stdint.h>
#include <stddef.h>
#include "wineador/json_out.h"
#include "wineador/log_read.h"
#include "wineador/web_api.h"

namespace wineador {

constexpr int kEventsPage = 50;   // 5.5: 50 at a time
constexpr const char* kRecentOnlyNote = "card unavailable: recent events only";

// The newest events, kept in RAM (PSRAM on the board) as they are logged, so the Events page
// still has something to show while the SD card is failed or out. Storage given by the caller.
class RecentEvents {
 public:
  void begin(EventRow* storage, int cap) { p_ = storage; cap_ = cap; head_ = n_ = 0; }
  void add(const EventRow& e);
  int count() const { return n_; }
  const EventRow& newest(int i) const { return p_[(head_ + n_ - 1 - i) % cap_]; }   // 0 = newest
  // Copies up to `max` rows, newest first, into out. Returns how many.
  int copyNewest(EventRow* out, int max) const;

 private:
  EventRow* p_ = nullptr;
  int cap_ = 0, head_ = 0, n_ = 0;
};

// GET /api/events: rows newest first. From the card: "older" pages back from the oldest row sent
// (null when there is nothing older); "newest" is where to ask for new rows from next time.
// While the card is unavailable (recentOnly): the rows kept in RAM, "source":"ram", the note
// kRecentOnlyNote, and no cursors.
class EventsReply : public JsonPump {
 public:
  EventsReply(const EventRow* rows, int n, const char* olderCursor, const char* newestCursor, bool recentOnly = false)
      : rows_(rows), n_(n), older_(olderCursor), newest_(newestCursor), recent_(recentOnly) {}
  // the worst row: every detail and type byte a control character, written as \u00XX (6 bytes),
  // plus its sentence (event_sentence.h, at most 320 bytes, the same escaping)
  size_t maxStep() const override { return 3600; }
 protected:
  bool step(JsonWriter& w) override;
 private:
  const EventRow* rows_;
  int n_;
  const char* older_;
  const char* newest_;
  bool recent_;
  int phase_ = 0, i_ = 0;
};

struct CardFacts {
  bool ok = false;               // mounted and working
  uint64_t freeBytes = 0, totalBytes = 0;
};

// GET /api/logs: the card, and every log file on it, newest first (sort them first).
class LogsReply : public JsonPump {
 public:
  LogsReply(const CardFacts& c, const LogFileEntry* files, int n) : c_(c), f_(files), n_(n) {}
  size_t maxStep() const override { return 300; }
 protected:
  bool step(JsonWriter& w) override;
 private:
  CardFacts c_;
  const LogFileEntry* f_;
  int n_;
  int phase_ = 0, i_ = 0;
};

// The last update's result, kept for the Update page (and, after a rollback, read at boot).
enum class UpdateResult : uint8_t { None, Installed, Refused, Abandoned, RolledBack, Failed };
const char* updateResultKey(UpdateResult r);   // "none", "installed", "refused", "abandoned", "rolled_back", "failed"

struct UpdateState {
  bool windowOpen = false;
  uint32_t windowLeftS = 0;
  bool compressorRunning = false;
  bool updating = false;
  uint8_t pct = 0;
  uint32_t slotBytes = 0;
  BuildFacts running;
  bool pendingVerify = false;          // this image hasn't yet "started properly" (5.2)
  UpdateResult last = UpdateResult::None;
  char lastDetail[120] = "";           // the reason, or the version installed / rolled back from
  int64_t lastUtcS = 0;                // when (0 = unknown)
};

// GET /api/update
int formatUpdateState(char* out, size_t n, const UpdateState& u);

}  // namespace wineador
