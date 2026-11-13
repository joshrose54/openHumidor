// What the SD task feeds (lib/core web_feed): the graphs, the events kept in RAM and the week's
// counts from the logger's own lines; and the refill after a boot from the card's files, a piece
// at a time, oldest first, held back while a graph reply is being sent.
#include <unity.h>
#include <string.h>
#include <map>
#include <string>
#include "wineador/web_feed.h"
#include "wineador/event_text.h"
#include "../web_rows.h"

using namespace wineador;
using webrows::readingsRow;
using webrows::hourlyRow;
using webrows::eventRow;

void setUp() {}
void tearDown() {}

static const int64_t T0 = 1794571200;   // 2026-11-13T12:00:00Z
static HistPoint gRaw[400], gDay[1440], gHourly[744];
static EventRow gRecent[50];

static WebFeed& fresh() {
  static WebFeed f;
  History::Storage s;
  s.raw = gRaw; s.rawCap = 400;
  s.day = gDay; s.dayCap = 1440;
  s.hourly = gHourly; s.hourlyCap = 744;
  f.begin(s, gRecent, 50);
  return f;
}

static void feed(WebFeed& f, FeedLine k, const std::string& line) { f.line(k, line.data(), line.size()); }

void test_live_lines_feed_the_graphs_the_recent_events_and_the_week() {
  WebFeed& f = fresh();
  f.countLiveFromNow();
  for (int i = 0; i < 8; i++) feed(f, FeedLine::Readings, readingsRow(T0 + 15 * i, 20.0f, i < 4));
  feed(f, FeedLine::Hourly, hourlyRow(T0 - 3600));
  char d[120];
  snprintf(d, sizeof(d), "%s (RH 59.4%% below 60.0%%)", kEvHumidifierOn);
  feed(f, FeedLine::Event, eventRow(T0 + 60, kEvTypeHumidifier, d));
  feed(f, FeedLine::Event, eventRow(T0 + 70, kEvTypeHumidifier, d, false));   // untrusted time: not counted
  TEST_ASSERT_EQUAL_INT(8, f.history().count(History::Ring::Raw));
  TEST_ASSERT_EQUAL_INT(1, f.history().count(History::Ring::Hourly));
  EventRow rows[5];
  TEST_ASSERT_EQUAL_INT(2, f.recentEvents(rows, 5));
  TEST_ASSERT_FALSE(rows[0].trusted);               // newest first
  StatusSnapshot s;
  f.week(T0 + 100, &s);
  TEST_ASSERT_FALSE(s.weekReady);                   // not until the card's events are counted
  f.weekCounted();
  f.week(T0 + 100, &s);
  TEST_ASSERT_TRUE(s.weekReady);
  TEST_ASSERT_EQUAL_UINT32(1, s.humidRuns);
}

// A card in memory, read a piece at a time.
struct Card {
  std::map<std::string, std::string> files;
  int reads = 0;
  int read(const char* path, uint64_t off, size_t n, std::string* out) {
    reads++;
    auto it = files.find(path);
    if (it == files.end()) return -2;
    if (off >= it->second.size()) return 0;
    *out = it->second.substr((size_t)off, n);
    return (int)out->size();
  }
  int list(LogFileEntry* out) {
    int n = 0;
    for (auto& kv : files) {
      snprintf(out[n].path, sizeof(out[n].path), "%s", kv.first.c_str());
      out[n].bytes = kv.second.size();
      n++;
    }
    return n;
  }
};

static int runRefill(BootRefill& r, WebFeed& f, Card& c, size_t piece, int maxSteps = 100000) {
  int steps = 0;
  std::string data;
  while (!r.done() && steps < maxSteps) {
    const char* path;
    uint64_t off;
    size_t n;
    TEST_ASSERT_TRUE(r.next(&path, &off, &n, piece));
    const int got = c.read(path, off, n, &data);
    TEST_ASSERT_TRUE(r.feed(f, data.data(), got));
    steps++;
  }
  return steps;
}

static Card cardWithAWeek() {
  Card c;
  std::string rd = "# header line\n", rdOld, hr, ev, evOld;
  for (int i = 0; i < 300; i++) rd += readingsRow(T0 - 300 * 15 + 15 * i, 20.0f, false);
  for (int i = 0; i < 200; i++) rdOld += readingsRow(T0 - 30 * 86400 + 15 * i, 19.0f, false);
  for (int i = 0; i < 48; i++) hr += hourlyRow(T0 - (48 - i) * 3600);
  char d[120];
  snprintf(d, sizeof(d), "%s (temperature)", kEvStripStart);
  for (int i = 0; i < 5; i++) ev += eventRow(T0 - 86400 * i, kEvTypeStrip, d);
  for (int i = 0; i < 3; i++) evOld += eventRow(T0 - 86400 * (10 + i), kEvTypeStrip, d);   // older than a week
  c.files["/logs/2026/2026-11-readings-s2.csv"] = rd;
  c.files["/logs/2026/2026-10-readings-s2.csv"] = rdOld;
  c.files["/logs/hourly-s2.csv"] = hr;
  c.files["/logs/2026/2026-11-events-s2.csv"] = ev;
  c.files["/logs/2026/2026-10-events-s2.csv"] = evOld;
  c.files["/logs/2026/2026-09-readings.csv"] = "schema 1: never read\n";
  return c;
}

void test_the_refill_reads_the_cards_files_in_pieces() {
  WebFeed& f = fresh();
  Card c = cardWithAWeek();
  LogFileEntry list[16];
  const int n = c.list(list);
  BootRefill r;
  r.begin(list, n);
  TEST_ASSERT_EQUAL_INT(5, r.jobs());     // hourly, readings (last month's tail first, then this month's), two events files
  // a live row arrives in the middle of the refill: it waits, then follows the refilled rows
  std::string data;
  for (int i = 0; i < 3 && !r.done(); i++) {
    const char* p; uint64_t o; size_t k;
    r.next(&p, &o, &k, 4096);
    const int got = c.read(p, o, k, &data);
    r.feed(f, data.data(), got);
  }
  feed(f, FeedLine::Readings, readingsRow(T0 + 15, 21.0f, false));
  const int steps = runRefill(r, f, c, 4096);
  TEST_ASSERT_TRUE(steps > 10);                     // pieces, not whole files
  TEST_ASSERT_EQUAL_INT(48, f.history().count(History::Ring::Hourly));
  const int raw = f.history().count(History::Ring::Raw);
  TEST_ASSERT_TRUE(raw >= 300);
  TEST_ASSERT_EQUAL_FLOAT(21.0f, f.history().at(History::Ring::Raw, raw - 1).v[kHCabT]);   // the live row last
  StatusSnapshot s;
  f.week(T0 + 60, &s);
  TEST_ASSERT_TRUE(s.weekReady);
  TEST_ASSERT_EQUAL_UINT32(5, s.strips);            // the three older than a week not counted
}

void test_the_refill_waits_while_a_graph_reply_is_open() {
  WebFeed& f = fresh();
  Card c = cardWithAWeek();
  LogFileEntry list[16];
  BootRefill r;
  r.begin(list, c.list(list));
  f.history().beginRead();                          // a reply is being sent
  const char* p; uint64_t o; size_t k;
  r.next(&p, &o, &k, 4096);
  std::string data;
  const int got = c.read(p, o, k, &data);
  TEST_ASSERT_FALSE(r.feed(f, data.data(), got));   // refused: the same piece again later
  const char* p2; uint64_t o2; size_t k2;
  r.next(&p2, &o2, &k2, 4096);
  TEST_ASSERT_EQUAL_STRING(p, p2);
  TEST_ASSERT_EQUAL_UINT64(o, o2);
  f.history().endRead();
  TEST_ASSERT_TRUE(r.feed(f, data.data(), got));
  runRefill(r, f, c, 4096);
  TEST_ASSERT_EQUAL_INT(48, f.history().count(History::Ring::Hourly));
}

void test_a_tail_skips_its_broken_first_line_and_a_failed_file_is_skipped() {
  WebFeed& f = fresh();
  Card c;
  std::string hr;
  for (int i = 0; i < 800; i++) hr += hourlyRow(T0 - (800 - i) * 3600);
  c.files["/logs/hourly-s2.csv"] = hr;
  c.files["/logs/2026/2026-11-events-s2.csv"] = eventRow(T0, kEvTypeDoor, kEvDoorTest);
  LogFileEntry list[4];
  const int n = c.list(list);
  c.files.erase("/logs/2026/2026-11-events-s2.csv");   // listed, then gone: read fails
  BootRefill r;
  r.begin(list, n);
  runRefill(r, f, c, 4096);
  TEST_ASSERT_TRUE(r.done());
  // the tail started mid-line: no half row was taken; a month's hours kept
  TEST_ASSERT_TRUE(f.history().count(History::Ring::Hourly) <= 744);
  TEST_ASSERT_TRUE(f.history().count(History::Ring::Hourly) >= 700);
  StatusSnapshot s;
  f.week(T0, &s);
  TEST_ASSERT_TRUE(s.weekReady);                    // the events file failed: counted as far as it could
}

// The card was out at boot: events logged meanwhile are counted from the card by the refill once
// it is back, not as they came as well.
void test_an_event_is_never_counted_twice() {
  WebFeed& f = fresh();
  char d[120];
  snprintf(d, sizeof(d), "%s (temperature)", kEvStripStart);
  const std::string early = eventRow(T0 - 60, kEvTypeStrip, d);
  feed(f, FeedLine::Event, early);                  // before the refill lists the files
  Card c;
  c.files["/logs/2026/2026-11-events-s2.csv"] = early;   // written when the card came back
  LogFileEntry list[4];
  BootRefill r;
  r.begin(list, c.list(list));
  f.countLiveFromNow();                             // (the board does this as it lists)
  runRefill(r, f, c, 4096);
  feed(f, FeedLine::Event, eventRow(T0, kEvTypeStrip, d));   // after: counted as it comes
  StatusSnapshot s;
  f.week(T0 + 60, &s);
  TEST_ASSERT_EQUAL_UINT32(2, s.strips);
}

void test_an_empty_card_finishes_at_once() {
  WebFeed& f = fresh();
  BootRefill r;
  r.begin(nullptr, 0);
  TEST_ASSERT_TRUE(r.done());
  TEST_ASSERT_FALSE(f.weekReady());   // nothing to count: the board marks it ready itself (web_server.cpp)
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_live_lines_feed_the_graphs_the_recent_events_and_the_week);
  RUN_TEST(test_the_refill_reads_the_cards_files_in_pieces);
  RUN_TEST(test_the_refill_waits_while_a_graph_reply_is_open);
  RUN_TEST(test_a_tail_skips_its_broken_first_line_and_a_failed_file_is_skipped);
  RUN_TEST(test_an_event_is_never_counted_twice);
  RUN_TEST(test_an_empty_card_finishes_at_once);
  return UNITY_END();
}
