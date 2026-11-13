#pragma once
// The figures behind the dashboard's graphs (spec 5.5): "hour and day from the readings; week
// and month from the hourly rows". Kept in RAM (PSRAM on the board), because the web server
// never reads the card itself (5.2).
//
//   raw     every readings row, for the hour graph (at the 15 s active rate an hour is 240 rows)
//   day     one point a minute (the mean of that minute's rows), for the day graph
//   hourly  the hourly rows, for the week (168) and month (744) graphs
//
// Everything is fed from the CSV lines the logger writes to the card (logbook.h, hourly.h,
// schema 2): live, each row as it is written; and after a boot, the refill — the SD task reads
// the tails of the files on the card and feeds their rows in, oldest first, while new rows wait
// in a small queue until it has finished. Rows whose time isn't trusted (5.6) are left out: they
// can't be placed on a time axis. A channel's values are used only when its flag is "ok".
//
// A reply is sent in pieces, and rows keep arriving meanwhile: while any reply is open (from its
// HistoryReply being made until it is destroyed) new rows wait in the same small queue, and no
// refill may start or be fed, so the rings stay still under it and every column lines up.
//
// Plain C++: the storage is given by the caller. Not thread-safe: the board holds one lock
// around each add, each refill line, and each fill() of a reply.

#include <stdint.h>
#include <stddef.h>
#include "wineador/json_out.h"

namespace wineador {

// One value per graph line. Temperatures in °C, RH in %, loads as the fraction of the time on
// (a readings row: 0 or 1), fan in % duty.
enum HistField : uint8_t {
  kHCabT, kHCabTMin, kHCabTMax, kHCabRh,                     // cabinet mean; coldest / warmest cabinet sensor
  kHCh0T, kHCh1T, kHCh2T, kHCh3T, kHCh4T, kHCh5T,
  kHCh0Rh, kHCh1Rh, kHCh2Rh, kHCh3Rh, kHCh4Rh, kHCh5Rh,
  kHCompressor, kHHeater, kHHumidifier, kHFan,
  kHistFields
};
const char* histFieldName(int f);    // "cab_t", "ch3_rh", "compressor" ... (the JSON keys)

struct HistPoint {
  int64_t t = 0;                     // UTC seconds (a minute's start; an hour's start)
  float v[kHistFields];              // NaN = no value
};

enum class HistRange : uint8_t { Hour, Day, Week, Month };
bool parseHistRange(const char* s, HistRange* r);
const char* histRangeName(HistRange r);

// Rows -> points. False for a comment, the column header, an untrusted time, or a short row.
bool parseReadingsLine(const char* line, size_t len, HistPoint* p);
bool parseHourlyLine(const char* line, size_t len, HistPoint* p);

class History {
 public:
  struct Storage {
    HistPoint* raw = nullptr;    int rawCap = 0;      // 400 is enough for an hour at 10 s
    HistPoint* day = nullptr;    int dayCap = 0;      // 1,440: a day of minutes
    HistPoint* hourly = nullptr; int hourlyCap = 0;   // 744: 31 days of hours
  };
  void begin(const Storage& s);

  // Live rows, as written to the card.
  bool addReadingsLine(const char* line, size_t len);
  bool addHourlyLine(const char* line, size_t len);
  void addReading(const HistPoint& p);
  void addHourly(const HistPoint& p);

  // The refill after a boot, one kind at a time. Rows already held move to the waiting queue,
  // the refill's rows go in oldest first, then the waiting rows follow at endRefill().
  enum class Kind : uint8_t { Readings, Hourly };
  // beginRefill and refillLine refuse (false) while a reply is open: the SD task tries again.
  bool beginRefill(Kind k);
  bool refillLine(Kind k, const char* line, size_t len);
  void endRefill(Kind k);
  bool replyOpen() const { return readers_ > 0; }
  bool refilling(Kind k) const { return k == Kind::Readings ? refillR_ : refillH_; }

  // The rings, oldest first.
  enum class Ring : uint8_t { Raw, Day, Hourly };
  int count(Ring r) const;
  const HistPoint& at(Ring r, int i) const;
  uint32_t waitingDropped() const { return dropped_; }   // live rows lost from a full queue

  // HistoryReply calls these: rows wait while any reply is open.
  void beginRead() { readers_++; }
  void endRead();

 private:
  struct R {
    HistPoint* p = nullptr;
    int cap = 0, head = 0, n = 0;    // head = oldest
    void clear() { head = n = 0; }
    void push(const HistPoint& x);
    const HistPoint& at(int i) const { return p[(head + i) % cap]; }
    const HistPoint* back() const { return n ? &at(n - 1) : nullptr; }
  };
  void pushReading(const HistPoint& p);
  void pushHourly(const HistPoint& p);
  void closeMinute();
  R raw_, day_, hourly_;
  // the minute being collected for the day ring
  bool minuteOpen_ = false;
  int64_t minute_ = 0;
  double sum_[kHistFields] = {};
  uint16_t cnt_[kHistFields] = {};
  float mn_ = 0, mx_ = 0;
  // rows waiting while a refill runs
  static constexpr int kWaitR = 32, kWaitH = 4;
  HistPoint waitR_[kWaitR], waitH_[kWaitH];
  int nWaitR_ = 0, nWaitH_ = 0;
  bool refillR_ = false, refillH_ = false;
  int readers_ = 0;
  uint32_t dropped_ = 0;
  void flushWaiting();
};

// GET /api/history?range=...&since=...: columns for uPlot — "t" (UTC seconds) then one array per
// HistField — with a null row wherever the points are further apart than a gap, so the graph
// shows a break; and the duty of each load over the whole range ("duty"), even with `since`.
class HistoryReply : public JsonPump {
 public:
  // since: only points after this time (0 = the whole range). now: the board's clock. The duty is
  // always over the whole range. The history holds still until the reply is destroyed.
  HistoryReply(History& h, HistRange r, int64_t nowUtcS, int64_t sinceUtcS);
  ~HistoryReply() override { h_.endRead(); }
  HistoryReply(const HistoryReply&) = delete;
  HistoryReply& operator=(const HistoryReply&) = delete;
  size_t maxStep() const override { return 200; }
  int points() const { return n_ - i0_; }     // points in the reply (gap rows not counted)

 protected:
  bool step(JsonWriter& w) override;

 private:
  const HistPoint& pt(int i) const { return h_.at(ring_, i); }
  bool gapBefore(int i) const;
  History& h_;
  HistRange range_;
  History::Ring ring_;
  int64_t now_;
  int i0_ = 0, n_ = 0;
  int iRange_ = 0;         // the first point of the whole range (i0_ is after `since`)
  int64_t gapS_ = 0, stepS_ = 0;
  // where the writing has got to
  int phase_ = 0;          // 0 header, 1 a column, 2 end
  int col_ = 0;            // -1 = "t", 0.. = HistField
  int idx_ = 0;
  bool gapDone_ = false;
  bool colOpen_ = false;
};

// The fraction of the time a load was on over a ring's points from i0 (NaN if none). Day and
// hourly points count equally (a minute, an hour each); raw rows count by the time to the next
// row (at most 120 s), the last by the time to `now`.
double histDuty(const History& h, History::Ring r, int i0, HistField load, int64_t nowUtcS);

}  // namespace wineador
