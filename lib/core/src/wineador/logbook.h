#pragma once
// SD logging, the plain-logic half (spec 5.4). Builds the CSV text and decides
// what to write and delete; the SD task in src/drivers/sd_log only writes it.
//
//   Readings  every 60 s (15 s while the compressor or heater runs). Buffered in
//             RAM and written ~16 rows at a time.
//   Events    written at once: timestamp, time trusted, uptime, type, detail.
//   Hourly    min / max / mean per channel and derived value, plus each load's
//             on-time in the hour (hourly.h).
//   Files     /logs/YYYY/YYYY-MM-readings.csv and -events.csv by UTC month, and
//             /logs/hourly.csv. Self-describing headers. From schema 2 every file
//             name carries the schema as -sN (YYYY-MM-readings-s2.csv,
//             YYYY-MM-events-s2.csv, hourly-s2.csv), so a file holds one schema only.
//   Schemas   1: channels numbered by Rev N's mux, ch2-ch7 (B1-B4 cards).
//             2: channels 0-5 (spec 2.3), and a "# board" header line naming the
//                board and its mux wiring.
//   Retention at boot: below 10 % free, delete the oldest month of readings until
//             above 20 % free. Events and hourly are never deleted, nor the
//             current month's readings.

#include "wineador/types.h"
#include "wineador/settings.h"
#include "wineador/summary.h"
#include "wineador/guards.h"

namespace wineador {

// The heap floor (5.2): free heap must never go below this, WiFi bursts and SD writes
// included. The lowest since boot is logged in every readings and hourly row.
constexpr uint32_t kHeapFloorBytes = 30u * 1024u;

constexpr int kLogSchema = 2;          // bump whenever columns change, renamed ones included (5.4)
constexpr int kReadingsBatch = 16;         // rows per SD write (5.4 wear)
// FAT32 holds no file of 4 GiB or more. No log file should ever get near it
// (~12 MB of readings a month), but if one would pass this limit the logger
// starts a numbered part: 2026-11-readings-p2.csv, -p3 ... each with its header.
constexpr uint64_t kMaxLogFileBytes = 3900000000ULL;

// ---------------------------------------------------------------- CSV helpers
// Appends detail to out as one CSV field: quoted if it holds a comma, quote or
// newline (quotes doubled, newlines turned into spaces).
int csvField(char* out, size_t n, const char* text);

// ---------------------------------------------------------------- paths
enum class LogKind : uint8_t { Readings, Events, Hourly };
// "/logs/2026/2026-11-readings-s2.csv"; "/logs/hourly-s2.csv" (schema 1: no -s1). Returns the length.
int logPath(char* out, size_t n, LogKind k, int year, int month, int schema = kLogSchema);
// "/logs/2026"
int logYearDir(char* out, size_t n, int year);
// Part `part` (>= 2) of a log file: "/logs/2026/2026-11-readings.csv" -> "...-readings-p2.csv";
// part 1 is the path itself.
int logPartPath(char* out, size_t n, const char* path, int part);
// Would adding `addBytes` to a file of `fileBytes` pass the limit? (Then use the next part.)
inline bool logFileFull(uint64_t fileBytes, uint64_t addBytes) { return fileBytes + addBytes > kMaxLogFileBytes; }
// Parses "2026-11-readings.csv" (also "-s2" schema and "-p2" part forms): true with year and month.
bool parseReadingsName(const char* name, int* year, int* month);

// ---------------------------------------------------------------- headers
struct LogHeaderInfo {
  const char* firmware = "?";
  const char* board = "?";      // the "# board" line, from include/board.h, e.g. "rev-q (channel n = mux n)"
  Profile profile = Profile::Real;
  bool testBuild = false;
};
// Comment lines (# wineador readings, # schema 2, # board, # firmware, # profile, one line
// per sensor with serial, position, rh_gain, rh_offset) then the column header.
int formatReadingsHeader(char* out, size_t n, const LogHeaderInfo& h);
int formatEventsHeader(char* out, size_t n, const LogHeaderInfo& h);
int formatHourlyHeader(char* out, size_t n, const LogHeaderInfo& h);

// ---------------------------------------------------------------- readings row
struct ChannelLog {
  bool read = false;        // the bus read succeeded this sweep (values valid)
  float tempC = 0;
  float rhRawPct = 0;       // raw, as read
  float rhPct = 0;          // corrected (gain x raw + offset)
  const char* flag = "ok";  // health: "ok" or the exclusion reason ("crc", "absent" ...)
};

struct ReadingsRow {
  const char* timestamp = "";
  bool timeTrusted = false;
  ChannelLog ch[kNumSensors];        // the values the logic used (scripted in a test build)
  ChannelLog real[kNumSensors];      // TEST BUILDS: the real readings alongside
  CabinetSummary cab;
  uint8_t fanPct = 0;
  uint32_t fanRpm = 0;
  bool cool = false;
  uint8_t heaterDutyPct = 0;         // 0 off, else the duty while on (PWM cap)
  bool humid = false;
  bool relayHeating = false;
  uint32_t loopMaxUs = 0;            // longest loop pass in this log interval
  uint32_t heapFree = 0;
  uint32_t heapMin = 0;              // lowest free heap since boot (5.2)
  uint32_t sdWorstMs = 0;            // worst SD write since the last row
};
int formatReadingsRow(char* out, size_t n, const ReadingsRow& r, bool testBuild);

// ---------------------------------------------------------------- events row
int formatEventRow(char* out, size_t n, const char* timestamp, bool trusted, uint64_t uptimeMs,
                   const char* type, const char* detail);

// ---------------------------------------------------------------- retention
struct LogFileInfo {
  int year = 0, month = 0;
  uint64_t bytes = 0;
};
// Which readings files to delete, oldest first: none unless free < 10 % of total;
// then oldest months until free > 20 %. Never the current month. Returns how many
// of `files` (sorted here, oldest first) to delete from the front.
int retentionPlan(LogFileInfo* files, int count, uint64_t totalBytes, uint64_t freeBytes,
                  int currentYear, int currentMonth);

// ---------------------------------------------------------------- bus event limit
// CRC failures and timeouts (5.4): up to 10 per channel per hour are logged; the
// rest are counted and reported as one "N more this hour" line when the hour ends.
class BusEventLimiter {
 public:
  // An event for channel index i: true = log it.
  bool allow(int i, uint32_t nowMs);
  // Call every pass: when a channel's hour ends with suppressed events, returns
  // true once with its index and count (log "N more this hour").
  bool takeSuppressed(uint32_t nowMs, int* index, uint32_t* count);
  static constexpr uint32_t kPerHour = 10;

 private:
  struct Ch { bool open = false; uint32_t sinceMs = 0, logged = 0, more = 0; };
  Ch ch_[kNumSensors];
};

// ---------------------------------------------------------------- guard blocks
// One event when an asked-for load becomes blocked, another when the reason
// changes or clears — not every pass (5.4).
class GuardBlockTracker {
 public:
  struct Change {
    int load = -1;                 // 0 compressor, 1 heater, 2 humidifier
    GuardBlock from = GuardBlock::None, to = GuardBlock::None;
    bool started = false;          // cleared because the load started
    uint32_t waitMs = 0;
  };
  // Every pass, after the guards. Returns true for each change (call until false).
  void update(const GuardDecisions& d);
  bool take(Change& c);
  // "heater blocked: crossover (12 s left)" / "heater block cleared (started)" ...
  static int format(char* out, size_t n, const Change& c);

 private:
  GuardBlock last_[3] = {GuardBlock::None, GuardBlock::None, GuardBlock::None};
  Change q_[6];
  int qn_ = 0;
};

}  // namespace wineador
