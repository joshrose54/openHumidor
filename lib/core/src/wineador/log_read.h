#pragma once
// Reading the card's own log files back (spec 5.4, 5.5), the plain-logic half. The SD task reads
// a file a piece at a time between its own writes (the web server never reads the card itself,
// 5.2); this code turns the pieces into lines, the lines into rows, and checks file names.
//
//   - CSV fields, as logbook.h writes them (quotes doubled, commas inside quotes);
//   - lines from pieces, reading forwards (new events, the history refill, the weekly counts)
//     or backwards (the Events page pages back from the newest);
//   - event rows; log file names and paths (only real log files may be downloaded);
//   - the weekly counts (5.5): crossovers, RH strips, humidifier runs and disturbances over the
//     last 7 days, counted from the event rows.

#include <stdint.h>
#include <stddef.h>

namespace wineador {

constexpr size_t kMaxLogLine = 1024;      // longer lines (none are written) are skipped

// ---------------------------------------------------------------- CSV
// Splits one line in place (it is changed): fields point into it. Quotes removed, "" -> ".
// A trailing \r or \n is dropped. Returns the number of fields (at most maxFields).
int csvSplit(char* line, char** fields, int maxFields);

// "2026-11-13T14:32:05Z" -> seconds since 1970 (UTC). False if not in that form.
bool parseIsoUtc(const char* s, int64_t* utcS);
// "13 Nov 2026 14:32:05" in UK local time (Europe/London, 5.5).
const char* formatUkDateTime(char* out, size_t n, int64_t utcS);

// ---------------------------------------------------------------- lines from pieces
typedef bool (*LineFn)(const char* line, size_t len, uint64_t offset, void* ctx);   // false = stop

// Forwards: feed pieces in file order; each complete line goes to fn with its start offset.
class LineReader {
 public:
  void begin(uint64_t startOffset, LineFn fn, void* ctx);
  // Returns false once fn has asked to stop.
  bool feed(const char* data, size_t n);
  void finish();                 // the end of the file: a last line without its newline
  uint32_t skipped() const { return skipped_; }   // lines too long to keep

 private:
  char buf_[kMaxLogLine];
  size_t len_ = 0;
  uint64_t lineStart_ = 0, pos_ = 0;
  bool overlong_ = false, stopped_ = false;
  LineFn fn_ = nullptr;
  void* ctx_ = nullptr;
  uint32_t skipped_ = 0;
};

// Backwards: feed pieces from the end towards the start (each piece ends where the previous one
// began); lines come out newest first.
class BackwardLineReader {
 public:
  void begin(LineFn fn, void* ctx);
  bool feed(const char* data, size_t n, uint64_t pieceStart);
  void finish();                 // reached the start of the file: the first line
  uint32_t skipped() const { return skipped_; }

 private:
  bool emit(const char* a, size_t an, uint64_t offset);
  char carry_[kMaxLogLine];
  size_t carryLen_ = 0;
  bool carryOverlong_ = false;
  uint64_t lastStart_ = 0;
  char line_[kMaxLogLine];
  bool stopped_ = false;
  LineFn fn_ = nullptr;
  void* ctx_ = nullptr;
  uint32_t skipped_ = 0;
};

// ---------------------------------------------------------------- event rows
struct EventRow {
  int64_t utcS = 0;
  char timestamp[24] = "";
  bool trusted = false;
  uint64_t uptimeMs = 0;
  char type[16] = "";
  char detail[208] = "";
  uint64_t offset = 0;        // where the row starts in its file
};
// One events row (timestamp,time_trusted,uptime_ms,type,detail). False for the comment lines,
// the column header, or anything else.
bool parseEventRow(const char* line, size_t len, EventRow* out);

// ---------------------------------------------------------------- the weekly counts
enum class WeekKind : uint8_t { None, Crossover, Strip, HumidRun, Disturbance };
// Which count an event belongs to, from its type and detail, by the wording in event_text.h:
// crossover rows; strip rows starting kEvStripStart; humidifier rows starting kEvHumidifierOn;
// disturbance rows.
WeekKind weekKindOf(const char* type, const char* detail);

// The last 7 days (168 hours) in hourly buckets. At boot the SD task feeds it the event rows of
// the last 7 days from the files on the card; from then on each event as it is written.
class WeekCounts {
 public:
  void add(int64_t utcS, WeekKind k);
  // The counts over the 168 hours ending with the hour of nowUtcS.
  void counts(int64_t nowUtcS, uint32_t* crossovers, uint32_t* strips, uint32_t* humidRuns,
              uint32_t* disturbances) const;
  static constexpr int kHours = 168;

 private:
  struct Bucket { int64_t hour = -1; uint16_t n[4] = {}; };
  Bucket b_[kHours];
};

// ---------------------------------------------------------------- log files
enum class LogFileKind : uint8_t { Readings, Events, Hourly };
struct LogName {
  LogFileKind kind = LogFileKind::Readings;
  int year = 0, month = 0;     // 0 for hourly
  int schema = 1;              // no -sN = schema 1
  int part = 1;                // no -pN = part 1
};
// A file name as the logger writes it (5.4): "2026-11-readings-s2.csv", "2026-11-events-s2-p2.csv",
// "hourly-s2.csv", and the schema 1 forms without -s1.
bool parseLogName(const char* name, LogName* out);
// A full path to a log file: "/logs/hourly-s2.csv" or "/logs/2026/2026-11-events-s2.csv" (the
// folder's year must match the name's). Anything else — other folders, "..", other files — is
// refused: only real log files can be downloaded.
bool parseLogPath(const char* path, LogName* out);
const char* logFileKindName(LogFileKind k);   // "readings" / "events" / "hourly"
// Orders files in time: by month, then schema, then part (5.4: names don't sort in time order);
// within those, readings after events. Hourly files have no month: they sort by schema, then
// part, before everything else.
int64_t logOrderKey(const LogName& n);

struct LogFileEntry {
  char path[48] = "";
  uint64_t bytes = 0;
};
// Newest first (logOrderKey, highest first). Entries that aren't log paths go to the end.
void sortLogFilesNewestFirst(LogFileEntry* f, int n);
// The events file just older than `path` in the list (by logOrderKey), or false if none.
bool olderEventsFile(const LogFileEntry* f, int n, const char* path, char* out, size_t outN);
// The newest events file in the list, or false if none.
bool newestEventsFile(const LogFileEntry* f, int n, char* out, size_t outN);

// ---------------------------------------------------------------- event cursors
// Where an Events page ends: "<path>@<offset>", e.g. "/logs/2026/2026-11-events-s2.csv@81234".
int formatCursor(char* out, size_t n, const char* path, uint64_t offset);
// False unless it names an events file (parseLogPath) and an offset.
bool parseCursor(const char* s, char* path, size_t pathN, uint64_t* offset);

// ---------------------------------------------------------------- history refill
// Where to start reading a file's tail to find its last `wantBytes` of rows: never before 0.
inline uint64_t tailStart(uint64_t fileBytes, uint64_t wantBytes) {
  return fileBytes > wantBytes ? fileBytes - wantBytes : 0;
}

}  // namespace wineador
