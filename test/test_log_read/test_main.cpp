// Reading the card's log files back (spec 5.4, 5.5), plain logic: CSV fields as the logger writes
// them, lines from pieces forwards and backwards (any piece size), event rows, the weekly counts
// from event rows, log file names and paths (only real log files can be downloaded), the Events
// page cursors, and the Events, Logs and Update replies.
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "wineador/log_read.h"
#include "wineador/logbook.h"
#include "wineador/uk_time.h"
#include "wineador/web_lists.h"
#include "../json_check.h"

using namespace wineador;

void setUp() {}
void tearDown() {}

static int64_t utc(int y, int mo, int d, int h, int mi, int s = 0) {
  return daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
}

// ---------------------------------------------------------------- CSV and times
void test_csv_split_matches_the_logger() {
  // the logger's own quoting (logbook csvField), read back
  char row[400];
  formatEventRow(row, sizeof(row), "2026-11-13T14:32:05Z", true, 123456, "settings",
                 "target_temp_c 20.0 -> 20.5, from \"dashboard\"");
  char* f[8];
  const int k = csvSplit(row, f, 8);
  TEST_ASSERT_EQUAL_INT(5, k);
  TEST_ASSERT_EQUAL_STRING("2026-11-13T14:32:05Z", f[0]);
  TEST_ASSERT_EQUAL_STRING("1", f[1]);
  TEST_ASSERT_EQUAL_STRING("123456", f[2]);
  TEST_ASSERT_EQUAL_STRING("settings", f[3]);
  TEST_ASSERT_EQUAL_STRING("target_temp_c 20.0 -> 20.5, from \"dashboard\"", f[4]);
  char empty[] = "a,,c,\r\n";
  TEST_ASSERT_EQUAL_INT(4, csvSplit(empty, f, 8));
  TEST_ASSERT_EQUAL_STRING("", f[1]);
  TEST_ASSERT_EQUAL_STRING("", f[3]);
}

void test_iso_times_and_uk_dates() {
  int64_t t;
  TEST_ASSERT_TRUE(parseIsoUtc("2026-11-13T14:32:05Z", &t));
  TEST_ASSERT_EQUAL_INT64(utc(2026, 11, 13, 14, 32, 5), t);
  TEST_ASSERT_FALSE(parseIsoUtc("2026-11-13 14:32:05", &t));
  TEST_ASSERT_FALSE(parseIsoUtc("timestamp", &t));
  TEST_ASSERT_FALSE(parseIsoUtc("2026-13-13T14:32:05Z", &t));
  char b[32];
  TEST_ASSERT_EQUAL_STRING("13 Nov 2026 14:32:05", formatUkDateTime(b, sizeof(b), utc(2026, 11, 13, 14, 32, 5)));
  TEST_ASSERT_EQUAL_STRING("1 Jul 2026 15:00:00", formatUkDateTime(b, sizeof(b), utc(2026, 7, 1, 14, 0)));   // BST
}

// ---------------------------------------------------------------- lines from pieces
struct Lines {
  std::vector<std::string> text;
  std::vector<uint64_t> off;
  int stopAfter = -1;
};
static bool collect(const char* line, size_t len, uint64_t offset, void* ctx) {
  Lines* L = (Lines*)ctx;
  L->text.push_back(std::string(line, len));
  L->off.push_back(offset);
  return L->stopAfter < 0 || (int)L->text.size() < L->stopAfter;
}

static const char* kFile =
    "# wineador events\n"
    "timestamp,time_trusted,uptime_ms,type,detail\n"
    "2026-11-13T10:00:00Z,1,1000,boot,\"boot, POWERON\"\n"
    "2026-11-13T10:00:01Z,1,2000,sd,card mounted\n"
    "\n"
    "2026-11-13T10:05:00Z,1,300000,compressor,compressor ON (cooling)\n";

void test_forward_lines_any_piece_size() {
  const size_t n = strlen(kFile);
  for (size_t piece = 1; piece <= n + 3; piece++) {
    Lines L;
    FileLineReader r;
    r.begin(0, collect, &L);
    for (size_t at = 0; at < n; at += piece) r.feed(kFile + at, (at + piece > n) ? n - at : piece);
    r.finish();
    TEST_ASSERT_EQUAL_INT(5, (int)L.text.size());   // the empty line is skipped
    TEST_ASSERT_EQUAL_STRING("# wineador events", L.text[0].c_str());
    TEST_ASSERT_EQUAL_STRING("2026-11-13T10:05:00Z,1,300000,compressor,compressor ON (cooling)", L.text[4].c_str());
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(strstr(kFile, "2026-11-13T10:00:01Z") - kFile), L.off[3]);
  }
}

void test_backward_lines_any_piece_size_newest_first() {
  const size_t n = strlen(kFile);
  for (size_t piece = 1; piece <= n + 3; piece++) {
    Lines L;
    BackwardLineReader r;
    r.begin(collect, &L);
    uint64_t end = n;
    while (end > 0) {
      const uint64_t start = end > piece ? end - piece : 0;
      r.feed(kFile + start, (size_t)(end - start), start);
      end = start;
    }
    r.finish();
    TEST_ASSERT_EQUAL_INT(5, (int)L.text.size());
    TEST_ASSERT_EQUAL_STRING("2026-11-13T10:05:00Z,1,300000,compressor,compressor ON (cooling)", L.text[0].c_str());
    TEST_ASSERT_EQUAL_STRING("# wineador events", L.text[4].c_str());
    for (size_t i = 0; i < L.text.size(); i++)   // each offset is where its line starts in the file
      TEST_ASSERT_EQUAL_INT(0, strncmp(kFile + L.off[i], L.text[i].c_str(), L.text[i].size()));
  }
}

void test_backward_from_a_cursor_and_stopping_early() {
  // start from the beginning of the sd row (a cursor): only the lines before it come out
  const uint64_t cursor = (uint64_t)(strstr(kFile, "2026-11-13T10:00:01Z") - kFile);
  Lines L;
  L.stopAfter = 1;
  BackwardLineReader r;
  r.begin(collect, &L);
  r.feed(kFile, (size_t)cursor, 0);
  r.finish();
  TEST_ASSERT_EQUAL_INT(1, (int)L.text.size());
  TEST_ASSERT_EQUAL_STRING("2026-11-13T10:00:00Z,1,1000,boot,\"boot, POWERON\"", L.text[0].c_str());
}

void test_overlong_lines_are_skipped_not_split() {
  std::string f = "a,1\n" + std::string(kMaxLogLine + 10, 'x') + "\nb,2\n";
  Lines L;
  FileLineReader r;
  r.begin(0, collect, &L);
  for (size_t at = 0; at < f.size(); at += 100) r.feed(f.data() + at, std::min<size_t>(100, f.size() - at));
  r.finish();
  TEST_ASSERT_EQUAL_INT(2, (int)L.text.size());
  TEST_ASSERT_EQUAL_UINT32(1, r.skipped());
  Lines B;
  BackwardLineReader br;
  br.begin(collect, &B);
  uint64_t end = f.size();
  while (end > 0) {
    const uint64_t s = end > 100 ? end - 100 : 0;
    br.feed(f.data() + s, (size_t)(end - s), s);
    end = s;
  }
  br.finish();
  TEST_ASSERT_EQUAL_INT(2, (int)B.text.size());
  TEST_ASSERT_EQUAL_STRING("b,2", B.text[0].c_str());
  TEST_ASSERT_EQUAL_STRING("a,1", B.text[1].c_str());
  TEST_ASSERT_EQUAL_UINT32(1, br.skipped());
}

// ---------------------------------------------------------------- event rows
void test_event_rows() {
  char row[400];
  const int k = formatEventRow(row, sizeof(row), "2026-11-13T14:32:05Z", false, 99, "crossover",
                               "CROSSOVER 3 since boot: cooling -> heating");
  EventRow e;
  TEST_ASSERT_TRUE(parseEventRow(row, (size_t)k, &e));
  TEST_ASSERT_EQUAL_INT64(utc(2026, 11, 13, 14, 32, 5), e.utcS);
  TEST_ASSERT_FALSE(e.trusted);
  TEST_ASSERT_EQUAL_UINT64(99, e.uptimeMs);
  TEST_ASSERT_EQUAL_STRING("crossover", e.type);
  TEST_ASSERT_EQUAL_STRING("CROSSOVER 3 since boot: cooling -> heating", e.detail);
  const char* header = "timestamp,time_trusted,uptime_ms,type,detail";
  TEST_ASSERT_FALSE(parseEventRow(header, strlen(header), &e));
  TEST_ASSERT_FALSE(parseEventRow("# wineador events", 17, &e));
  TEST_ASSERT_FALSE(parseEventRow("2026-11-13T14:32:05Z,1,5", 24, &e));   // cut short
}

// ---------------------------------------------------------------- the weekly counts
void test_week_kinds_from_the_firmwares_own_texts() {
  TEST_ASSERT_TRUE(weekKindOf("crossover", "CROSSOVER 3 since boot: cooling -> heating") == WeekKind::Crossover);
  TEST_ASSERT_TRUE(weekKindOf("strip", "RH STRIP START (2 since boot): mean RH 68.0% >= 67.0%") == WeekKind::Strip);
  TEST_ASSERT_TRUE(weekKindOf("strip", "RH STRIP STOP: vapour pressure") == WeekKind::None);
  TEST_ASSERT_TRUE(weekKindOf("humidifier", "humidifier ON (humidify)") == WeekKind::HumidRun);
  TEST_ASSERT_TRUE(weekKindOf("humidifier", "humidifier OFF (demand ended)") == WeekKind::None);
  TEST_ASSERT_TRUE(weekKindOf("disturbance", "rise 0.8 C/min, hold-off 3 min") == WeekKind::Disturbance);
  TEST_ASSERT_TRUE(weekKindOf("compressor", "compressor ON") == WeekKind::None);
}

void test_week_counts_cover_the_last_168_hours() {
  WeekCounts w;
  const int64_t now = utc(2026, 11, 13, 14, 30);
  w.add(now - 60, WeekKind::Crossover);
  w.add(now - 3 * 86400, WeekKind::Crossover);
  w.add(now - 3 * 86400, WeekKind::Strip);
  w.add(now - 167 * 3600, WeekKind::HumidRun);          // inside: 167 hours back
  w.add(now - 168 * 3600, WeekKind::HumidRun);          // outside: 168 hours back
  w.add(now - 30 * 86400, WeekKind::Disturbance);       // long ago
  w.add(now + 7200, WeekKind::Crossover);               // the future (a clock jump): not counted
  w.add(now, WeekKind::None);
  uint32_t x, s, h, d;
  w.counts(now, &x, &s, &h, &d);
  TEST_ASSERT_EQUAL_UINT32(2, x);
  TEST_ASSERT_EQUAL_UINT32(1, s);
  TEST_ASSERT_EQUAL_UINT32(1, h);
  TEST_ASSERT_EQUAL_UINT32(0, d);
  // a week later every bucket has rolled out
  w.counts(now + 8 * 86400, &x, &s, &h, &d);
  TEST_ASSERT_EQUAL_UINT32(0, x + s + h + d);
  // a bucket reused for a new hour starts again from zero: the hour of `now` (one crossover) is
  // the same bucket as 168 hours later; only the crossover 2 hours after `now` is still in that week
  w.add(now + 168 * 3600, WeekKind::Strip);
  w.counts(now + 168 * 3600, &x, &s, &h, &d);
  TEST_ASSERT_EQUAL_UINT32(1, s);
  TEST_ASSERT_EQUAL_UINT32(1, x);
  // an event older than the hour its bucket now holds is ignored (it doesn't wipe the newer hour)
  w.add(now, WeekKind::Strip);
  w.counts(now + 168 * 3600, &x, &s, &h, &d);
  TEST_ASSERT_EQUAL_UINT32(1, s);
}

void test_week_counts_from_an_events_file_read_in_pieces() {
  // the boot scan: event rows from a file, through the line reader, into the counts
  std::string file = "# wineador events\ntimestamp,time_trusted,uptime_ms,type,detail\n";
  char row[300];
  const int64_t now = utc(2026, 11, 13, 12, 0);
  for (int i = 0; i < 10; i++) {
    char ts[24];
    const int64_t t = now - (int64_t)i * 86400;   // one a day for 10 days
    int y, m, d;
    civilFromDays(t / 86400, &y, &m, &d);
    snprintf(ts, sizeof(ts), "%04d-%02d-%02dT12:00:00Z", y, m, d);
    formatEventRow(row, sizeof(row), ts, true, 1, "humidifier", "humidifier ON (humidify)");
    file += row;
    formatEventRow(row, sizeof(row), ts, i != 3, 2, "crossover", "CROSSOVER 1 since boot: cooling -> heating");
    file += row;
  }
  struct Ctx { WeekCounts w; };
  Ctx c;
  FileLineReader r;
  r.begin(0, [](const char* line, size_t len, uint64_t, void* p) -> bool {
    EventRow e;
    if (parseEventRow(line, len, &e) && e.trusted) ((Ctx*)p)->w.add(e.utcS, weekKindOf(e.type, e.detail));
    return true;
  }, &c);
  for (size_t at = 0; at < file.size(); at += 64) r.feed(file.data() + at, std::min<size_t>(64, file.size() - at));
  r.finish();
  uint32_t x, s, h, d;
  c.w.counts(now, &x, &s, &h, &d);
  TEST_ASSERT_EQUAL_UINT32(7, h);     // days 0-6
  TEST_ASSERT_EQUAL_UINT32(6, x);     // days 0-6, less the untrusted one
}

// ---------------------------------------------------------------- log files
void test_log_names() {
  LogName n;
  TEST_ASSERT_TRUE(parseLogName("2026-11-readings-s2.csv", &n));
  TEST_ASSERT_TRUE(n.kind == LogFileKind::Readings);
  TEST_ASSERT_EQUAL_INT(2026, n.year);
  TEST_ASSERT_EQUAL_INT(11, n.month);
  TEST_ASSERT_EQUAL_INT(2, n.schema);
  TEST_ASSERT_EQUAL_INT(1, n.part);
  TEST_ASSERT_TRUE(parseLogName("2026-09-events.csv", &n));
  TEST_ASSERT_TRUE(n.kind == LogFileKind::Events);
  TEST_ASSERT_EQUAL_INT(1, n.schema);
  TEST_ASSERT_TRUE(parseLogName("2026-11-readings-s2-p2.csv", &n));
  TEST_ASSERT_EQUAL_INT(2, n.part);
  TEST_ASSERT_TRUE(parseLogName("hourly-s2.csv", &n));
  TEST_ASSERT_TRUE(n.kind == LogFileKind::Hourly);
  TEST_ASSERT_TRUE(parseLogName("hourly.csv", &n));
  TEST_ASSERT_EQUAL_INT(1, n.schema);
  const char* bad[] = {"2026-13-readings.csv", "2026-11-readings.txt", "2026-11-notes.csv", "hourly-s2.csv.bak",
                       "2026-11-readings-p1.csv", "2026-11-readings-s0.csv", "../hourly.csv", "readings.csv"};
  for (const char* b : bad) TEST_ASSERT_FALSE_MESSAGE(parseLogName(b, &n), b);
}

void test_only_real_log_paths_can_be_downloaded() {
  LogName n;
  TEST_ASSERT_TRUE(parseLogPath("/logs/2026/2026-11-readings-s2.csv", &n));
  TEST_ASSERT_TRUE(parseLogPath("/logs/2026/2026-11-events-s2.csv", &n));
  TEST_ASSERT_TRUE(parseLogPath("/logs/hourly-s2.csv", &n));
  TEST_ASSERT_TRUE(parseLogPath("/logs/hourly.csv", &n));
  const char* bad[] = {
    "/logs/2025/2026-11-readings-s2.csv",   // folder year doesn't match
    "/logs/2026/hourly-s2.csv",             // hourly lives in /logs
    "/logs/2026-11-readings-s2.csv",        // monthly files live in their year folder
    "/logs/2026/../2026/2026-11-events.csv",
    "/logs/../wifi_time.h",
    "/logs/2026/2026-11-events-s2.csv/",
    "logs/hourly.csv",
    "/LOGS/hourly.csv",
    "/logs/2026/2026-11-events-s2.csv?x",
    "/logs/2026/2026-11-events-s2.csv\n",
    "/sys/config.csv",
    "",
  };
  for (const char* b : bad) TEST_ASSERT_FALSE_MESSAGE(parseLogPath(b, &n), b);
  TEST_ASSERT_FALSE(parseLogPath(nullptr, &n));
}

void test_files_order_newest_first_and_the_older_events_file() {
  LogFileEntry f[8];
  const char* paths[] = {"/logs/2026/2026-09-events.csv", "/logs/hourly.csv", "/logs/2026/2026-11-events-s2.csv",
                         "/logs/2026/2026-11-events.csv", "/logs/2026/2026-11-readings-s2.csv", "/junk.txt",
                         "/logs/2026/2026-10-events-s2.csv", "/logs/hourly-s2.csv"};
  for (int i = 0; i < 8; i++) snprintf(f[i].path, sizeof(f[i].path), "%s", paths[i]);
  sortLogFilesNewestFirst(f, 8);
  // 5.4: by month, then schema (2026-11-events-s2 is newer than 2026-11-events), hourly first-made last
  TEST_ASSERT_EQUAL_STRING("/logs/2026/2026-11-readings-s2.csv", f[0].path);
  TEST_ASSERT_EQUAL_STRING("/logs/2026/2026-11-events-s2.csv", f[1].path);
  TEST_ASSERT_EQUAL_STRING("/logs/2026/2026-11-events.csv", f[2].path);
  TEST_ASSERT_EQUAL_STRING("/logs/2026/2026-10-events-s2.csv", f[3].path);
  TEST_ASSERT_EQUAL_STRING("/logs/2026/2026-09-events.csv", f[4].path);
  TEST_ASSERT_EQUAL_STRING("/logs/hourly-s2.csv", f[5].path);
  TEST_ASSERT_EQUAL_STRING("/logs/hourly.csv", f[6].path);
  TEST_ASSERT_EQUAL_STRING("/junk.txt", f[7].path);
  char p[48];
  TEST_ASSERT_TRUE(newestEventsFile(f, 8, p, sizeof(p)));
  TEST_ASSERT_EQUAL_STRING("/logs/2026/2026-11-events-s2.csv", p);
  TEST_ASSERT_TRUE(olderEventsFile(f, 8, "/logs/2026/2026-11-events-s2.csv", p, sizeof(p)));
  TEST_ASSERT_EQUAL_STRING("/logs/2026/2026-11-events.csv", p);
  TEST_ASSERT_TRUE(olderEventsFile(f, 8, "/logs/2026/2026-11-events.csv", p, sizeof(p)));
  TEST_ASSERT_EQUAL_STRING("/logs/2026/2026-10-events-s2.csv", p);
  TEST_ASSERT_FALSE(olderEventsFile(f, 8, "/logs/2026/2026-09-events.csv", p, sizeof(p)));
}

void test_cursors() {
  char c[80];
  formatCursor(c, sizeof(c), "/logs/2026/2026-11-events-s2.csv", 81234);
  TEST_ASSERT_EQUAL_STRING("/logs/2026/2026-11-events-s2.csv@81234", c);
  char p[48];
  uint64_t off = 0;
  TEST_ASSERT_TRUE(parseCursor(c, p, sizeof(p), &off));
  TEST_ASSERT_EQUAL_STRING("/logs/2026/2026-11-events-s2.csv", p);
  TEST_ASSERT_EQUAL_UINT64(81234, off);
  TEST_ASSERT_FALSE(parseCursor("/logs/2026/2026-11-readings-s2.csv@5", p, sizeof(p), &off));   // not events
  TEST_ASSERT_FALSE(parseCursor("/logs/2026/2026-11-events-s2.csv@", p, sizeof(p), &off));
  TEST_ASSERT_FALSE(parseCursor("/logs/2026/2026-11-events-s2.csv@12x", p, sizeof(p), &off));
  TEST_ASSERT_FALSE(parseCursor("/etc/passwd@0", p, sizeof(p), &off));
  TEST_ASSERT_FALSE(parseCursor("81234", p, sizeof(p), &off));
  TEST_ASSERT_EQUAL_UINT64(0, tailStart(100, 500));
  TEST_ASSERT_EQUAL_UINT64(400, tailStart(900, 500));
}

// ---------------------------------------------------------------- replies
void test_events_reply() {
  EventRow rows[2];
  char row[300];
  int k = formatEventRow(row, sizeof(row), "2026-11-13T14:32:05Z", true, 5, "settings",
                         "target_temp_c 20.0 -> 20.5 (from dashboard) \"quoted\"");
  parseEventRow(row, (size_t)k, &rows[0]);
  k = formatEventRow(row, sizeof(row), "2026-11-13T14:00:00Z", false, 4, "boot", "boot, POWERON");
  parseEventRow(row, (size_t)k, &rows[1]);
  EventsReply a(rows, 2, "/logs/2026/2026-11-events-s2.csv@100", "/logs/2026/2026-11-events-s2.csv@900");
  EventsReply b(rows, 2, "/logs/2026/2026-11-events-s2.csv@100", "/logs/2026/2026-11-events-s2.csv@900");
  const std::string w = jsoncheck::whole(a);
  TEST_ASSERT_TRUE(jsoncheck::valid(w));
  TEST_ASSERT_EQUAL_STRING(w.c_str(), jsoncheck::inPieces(b, b.maxStep() + 1).c_str());
  TEST_ASSERT_TRUE(w.find("\"uk\":\"13 Nov 2026 14:32:05\",\"trusted\":true,\"type\":\"settings\",\"sentence\":") != std::string::npos);
  TEST_ASSERT_TRUE(w.find("\\\"quoted\\\"") != std::string::npos);
  TEST_ASSERT_TRUE(w.find("\"older\":\"/logs/2026/2026-11-events-s2.csv@100\"") != std::string::npos);
  TEST_ASSERT_TRUE(w.find("\"source\":\"card\",\"note\":null") != std::string::npos);
  EventsReply none(rows, 0, nullptr, nullptr);
  TEST_ASSERT_EQUAL_STRING("{\"api\":4,\"source\":\"card\",\"note\":null,\"events\":[],\"older\":null,\"newest\":null}",
                           jsoncheck::whole(none).c_str());
  // the worst row: every byte a control character (6 bytes each as \u00XX) still fits one step
  EventRow ctl;
  snprintf(ctl.timestamp, sizeof(ctl.timestamp), "2026-11-13T14:32:05Z");
  memset(ctl.detail, 0x01, sizeof(ctl.detail) - 1);
  ctl.detail[sizeof(ctl.detail) - 1] = 0;
  memset(ctl.type, 0x02, sizeof(ctl.type) - 1);
  ctl.type[sizeof(ctl.type) - 1] = 0;
  EventsReply cw(&ctl, 1, nullptr, nullptr), cp(&ctl, 1, nullptr, nullptr);
  const std::string cws = jsoncheck::whole(cw);
  TEST_ASSERT_TRUE(jsoncheck::valid(cws));
  TEST_ASSERT_EQUAL_STRING(cws.c_str(), jsoncheck::inPieces(cp, cp.maxStep() + 1).c_str());
  // the largest possible row fits one step
  EventRow big;
  snprintf(big.timestamp, sizeof(big.timestamp), "2026-11-13T14:32:05Z");
  memset(big.detail, '"', sizeof(big.detail) - 1);
  big.detail[sizeof(big.detail) - 1] = 0;
  memset(big.type, '\\', sizeof(big.type) - 1);
  big.type[sizeof(big.type) - 1] = 0;
  EventsReply bigR(&big, 1, nullptr, nullptr), bigP(&big, 1, nullptr, nullptr);
  const std::string bw = jsoncheck::whole(bigR);
  TEST_ASSERT_TRUE(jsoncheck::valid(bw));
  TEST_ASSERT_EQUAL_STRING(bw.c_str(), jsoncheck::inPieces(bigP, bigP.maxStep() + 1).c_str());
}

void test_events_from_ram_while_the_card_is_out() {
  // the events kept in RAM as they are logged; the newest 50 shown, newest first, marked
  static EventRow store[60];
  RecentEvents r;
  r.begin(store, 60);
  for (int i = 0; i < 75; i++) {
    EventRow e;
    snprintf(e.timestamp, sizeof(e.timestamp), "2026-11-13T12:%02d:00Z", i % 60);
    e.utcS = utc(2026, 11, 13, 12, 0) + i * 60;
    e.trusted = true;
    snprintf(e.type, sizeof(e.type), "sd");
    snprintf(e.detail, sizeof(e.detail), "event %d", i);
    r.add(e);
  }
  TEST_ASSERT_EQUAL_INT(60, r.count());
  TEST_ASSERT_EQUAL_STRING("event 74", r.newest(0).detail);
  TEST_ASSERT_EQUAL_STRING("event 15", r.newest(59).detail);
  static EventRow page[kEventsPage];
  const int n = r.copyNewest(page, kEventsPage);
  TEST_ASSERT_EQUAL_INT(50, n);
  TEST_ASSERT_EQUAL_STRING("event 25", page[49].detail);
  // the cursors are ignored: there is nothing to page back through
  EventsReply a(page, n, "/logs/2026/2026-11-events-s2.csv@100", "/logs/2026/2026-11-events-s2.csv@900", true);
  EventsReply b(page, n, "/logs/2026/2026-11-events-s2.csv@100", "/logs/2026/2026-11-events-s2.csv@900", true);
  const std::string w = jsoncheck::whole(a);
  TEST_ASSERT_TRUE(jsoncheck::valid(w));
  TEST_ASSERT_EQUAL_STRING(w.c_str(), jsoncheck::inPieces(b, b.maxStep() + 1).c_str());
  TEST_ASSERT_TRUE(w.find("\"source\":\"ram\",\"note\":\"card unavailable: recent events only\"") != std::string::npos);
  TEST_ASSERT_TRUE(w.find("\"older\":null,\"newest\":null}") != std::string::npos);
  TEST_ASSERT_EQUAL_INT(50, jsoncheck::count(w, "\"detail\":"));
  TEST_ASSERT_TRUE(w.find("event 74") < w.find("event 73"));   // newest first
}

void test_logs_reply_lists_only_log_files() {
  LogFileEntry f[3];
  snprintf(f[0].path, sizeof(f[0].path), "/logs/2026/2026-11-readings-s2.csv");
  f[0].bytes = 4567890;
  snprintf(f[1].path, sizeof(f[1].path), "/logs/hourly-s2.csv");
  f[1].bytes = 12345;
  snprintf(f[2].path, sizeof(f[2].path), "/logs/notes.txt");
  CardFacts c;
  c.ok = true;
  c.freeBytes = 59000000000ULL;
  c.totalBytes = 60000000000ULL;
  LogsReply a(c, f, 3), b(c, f, 3);
  const std::string w = jsoncheck::whole(a);
  TEST_ASSERT_TRUE(jsoncheck::valid(w));
  TEST_ASSERT_EQUAL_STRING(w.c_str(), jsoncheck::inPieces(b, b.maxStep() + 1).c_str());
  TEST_ASSERT_TRUE(w.find("{\"path\":\"/logs/2026/2026-11-readings-s2.csv\",\"name\":\"2026-11-readings-s2.csv\","
                          "\"kind\":\"readings\",\"month\":\"2026-11\",\"schema\":2,\"part\":1,\"bytes\":4567890}") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(w.find("\"kind\":\"hourly\",\"month\":null") != std::string::npos);
  TEST_ASSERT_TRUE(w.find("notes.txt") == std::string::npos);
  TEST_ASSERT_TRUE(w.find("\"free_bytes\":59000000000") != std::string::npos);
}

void test_update_state_reply() {
  UpdateState u;
  u.windowOpen = true;
  u.windowLeftS = 212;
  u.slotBytes = 3342336;
  u.running.version = "2.0.0";
  u.last = UpdateResult::RolledBack;
  snprintf(u.lastDetail, sizeof(u.lastDetail), "2.0.1 didn't start properly; back on 2.0.0");
  char out[600];
  TEST_ASSERT_TRUE(formatUpdateState(out, sizeof(out), u) > 0);
  TEST_ASSERT_TRUE(jsoncheck::valid(out, strlen(out)));
  TEST_ASSERT_TRUE(strstr(out, "\"window_open\":true,\"window_s\":212") != nullptr);
  TEST_ASSERT_TRUE(strstr(out, "\"check_bytes\":352") != nullptr);
  TEST_ASSERT_TRUE(strstr(out, "\"result\":\"rolled_back\"") != nullptr);
  TEST_ASSERT_TRUE(strstr(out, "\"utc_s\":null") != nullptr);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_csv_split_matches_the_logger);
  RUN_TEST(test_iso_times_and_uk_dates);
  RUN_TEST(test_forward_lines_any_piece_size);
  RUN_TEST(test_backward_lines_any_piece_size_newest_first);
  RUN_TEST(test_backward_from_a_cursor_and_stopping_early);
  RUN_TEST(test_overlong_lines_are_skipped_not_split);
  RUN_TEST(test_event_rows);
  RUN_TEST(test_week_kinds_from_the_firmwares_own_texts);
  RUN_TEST(test_week_counts_cover_the_last_168_hours);
  RUN_TEST(test_week_counts_from_an_events_file_read_in_pieces);
  RUN_TEST(test_log_names);
  RUN_TEST(test_only_real_log_paths_can_be_downloaded);
  RUN_TEST(test_files_order_newest_first_and_the_older_events_file);
  RUN_TEST(test_cursors);
  RUN_TEST(test_events_reply);
  RUN_TEST(test_events_from_ram_while_the_card_is_out);
  RUN_TEST(test_logs_reply_lists_only_log_files);
  RUN_TEST(test_update_state_reply);
  return UNITY_END();
}
