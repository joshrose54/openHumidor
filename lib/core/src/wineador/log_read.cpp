#include "wineador/log_read.h"
#include "wineador/uk_time.h"
#include "wineador/event_text.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

namespace wineador {

// ---------------------------------------------------------------- CSV
int csvSplit(char* line, char** fields, int maxFields) {
  size_t L = strlen(line);
  while (L > 0 && (line[L - 1] == '\n' || line[L - 1] == '\r')) line[--L] = 0;
  int count = 0;
  char* r = line;      // read
  char* w = line;      // write (unquoting shrinks the text in place)
  for (;;) {
    if (count >= maxFields) break;
    fields[count++] = w;
    if (*r == '"') {
      r++;
      for (;;) {
        if (*r == 0) break;
        if (*r == '"') {
          if (r[1] == '"') { *w++ = '"'; r += 2; continue; }
          r++;
          break;
        }
        *w++ = *r++;
      }
      while (*r && *r != ',') r++;   // anything after the closing quote is ignored
    } else {
      while (*r && *r != ',') *w++ = *r++;
    }
    if (*r == ',') { *w++ = 0; r++; continue; }
    *w = 0;
    break;
  }
  return count;
}

static bool digits(const char* s, int n, int* out) {
  int v = 0;
  for (int i = 0; i < n; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
    v = v * 10 + (s[i] - '0');
  }
  *out = v;
  return true;
}

bool parseIsoUtc(const char* s, int64_t* utcS) {
  if (!s || strlen(s) < 20) return false;
  int y, mo, d, h, mi, se;
  if (!digits(s, 4, &y) || s[4] != '-' || !digits(s + 5, 2, &mo) || s[7] != '-' || !digits(s + 8, 2, &d) ||
      s[10] != 'T' || !digits(s + 11, 2, &h) || s[13] != ':' || !digits(s + 14, 2, &mi) || s[16] != ':' ||
      !digits(s + 17, 2, &se) || s[19] != 'Z')
    return false;
  if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 60) return false;
  *utcS = daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se;
  return true;
}

const char* formatUkDateTime(char* out, size_t n, int64_t utcS) {
  static const char* const kMon[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  const LocalTime t = ukLocal(utcS);
  snprintf(out, n, "%d %s %d %02d:%02d:%02d", t.day, kMon[(t.month - 1) % 12], t.year, t.hour, t.minute, t.second);
  return out;
}

// ---------------------------------------------------------------- forwards
void FileLineReader::begin(uint64_t startOffset, LineFn fn, void* ctx) {
  len_ = 0;
  lineStart_ = pos_ = startOffset;
  overlong_ = stopped_ = false;
  fn_ = fn;
  ctx_ = ctx;
  skipped_ = 0;
}

bool FileLineReader::feed(const char* data, size_t n) {
  for (size_t i = 0; i < n && !stopped_; i++) {
    const char c = data[i];
    pos_++;
    if (c == '\n') {
      if (overlong_) skipped_++;
      else if (len_ > 0) {
        buf_[len_] = 0;
        if (!fn_(buf_, len_, lineStart_, ctx_)) stopped_ = true;
      }
      len_ = 0;
      overlong_ = false;
      lineStart_ = pos_;
      continue;
    }
    if (overlong_) continue;
    if (len_ + 1 >= sizeof(buf_)) { overlong_ = true; continue; }
    buf_[len_++] = c;
  }
  return !stopped_;
}

void FileLineReader::finish() {
  if (stopped_) return;
  if (overlong_) skipped_++;
  else if (len_ > 0) {
    buf_[len_] = 0;
    fn_(buf_, len_, lineStart_, ctx_);
  }
  len_ = 0;
}

// ---------------------------------------------------------------- backwards
void BackwardLineReader::begin(LineFn fn, void* ctx) {
  carryLen_ = 0;
  carryOverlong_ = false;
  lastStart_ = 0;
  stopped_ = false;
  fn_ = fn;
  ctx_ = ctx;
  skipped_ = 0;
}

// a[0..an) then the carry make one line starting at `offset`.
bool BackwardLineReader::emit(const char* a, size_t an, uint64_t offset) {
  const size_t total = an + carryLen_;
  const bool over = carryOverlong_ || total + 1 > sizeof(line_);
  carryLen_ = 0;
  carryOverlong_ = false;
  if (over) { skipped_++; return true; }
  if (total == 0) return true;
  memcpy(line_, a, an);
  memcpy(line_ + an, carry_, total - an);
  line_[total] = 0;
  if (line_[total - 1] == '\r') line_[total - 1] = 0;
  if (!fn_(line_, strlen(line_), offset, ctx_)) stopped_ = true;
  return !stopped_;
}

bool BackwardLineReader::feed(const char* data, size_t n, uint64_t pieceStart) {
  if (stopped_) return false;
  lastStart_ = pieceStart;
  size_t end = n;   // data[j+1 .. end) + carry is the line after the newline at j
  for (size_t j = n; j-- > 0;) {
    if (data[j] != '\n') continue;
    if (!emit(data + j + 1, end - (j + 1), pieceStart + j + 1)) return false;
    end = j;
  }
  // data[0 .. end) begins a line whose start is in an earlier piece: put it in front of the carry.
  if (carryOverlong_ || end + carryLen_ + 1 > sizeof(carry_)) {
    carryOverlong_ = true;
    carryLen_ = 0;
  } else {
    memmove(carry_ + end, carry_, carryLen_);
    memcpy(carry_, data, end);
    carryLen_ += end;
  }
  return true;
}

void BackwardLineReader::finish() {
  if (stopped_) return;
  if (carryLen_ > 0 || carryOverlong_) emit("", 0, lastStart_);
}

// ---------------------------------------------------------------- event rows
bool parseEventRow(const char* line, size_t len, EventRow* out) {
  if (len == 0 || len >= kMaxLogLine || line[0] == '#') return false;
  char buf[kMaxLogLine];
  memcpy(buf, line, len);
  buf[len] = 0;
  char* f[6];
  const int k = csvSplit(buf, f, 6);
  if (k < 5) return false;
  int64_t t;
  if (!parseIsoUtc(f[0], &t)) return false;   // the column header ("timestamp,...") fails here
  if (strcmp(f[1], "0") != 0 && strcmp(f[1], "1") != 0) return false;
  EventRow r;
  r.utcS = t;
  snprintf(r.timestamp, sizeof(r.timestamp), "%s", f[0]);
  r.trusted = f[1][0] == '1';
  r.uptimeMs = strtoull(f[2], nullptr, 10);
  snprintf(r.type, sizeof(r.type), "%s", f[3]);
  snprintf(r.detail, sizeof(r.detail), "%s", f[4]);
  *out = r;
  return true;
}

// ---------------------------------------------------------------- the weekly counts
WeekKind weekKindOf(const char* type, const char* detail) {
  if (!type || !detail) return WeekKind::None;
  if (strcmp(type, kEvTypeCrossover) == 0) return WeekKind::Crossover;
  if (strcmp(type, kEvTypeStrip) == 0 && strncmp(detail, kEvStripStart, strlen(kEvStripStart)) == 0) return WeekKind::Strip;
  if (strcmp(type, kEvTypeHumidifier) == 0 && strncmp(detail, kEvHumidifierOn, strlen(kEvHumidifierOn)) == 0)
    return WeekKind::HumidRun;
  if (strcmp(type, kEvTypeDisturbance) == 0) return WeekKind::Disturbance;
  return WeekKind::None;
}

static int64_t floorDiv(int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

void WeekCounts::add(int64_t utcS, WeekKind k) {
  if (k == WeekKind::None) return;
  const int64_t h = floorDiv(utcS, 3600);
  Bucket& b = b_[(int)(((h % kHours) + kHours) % kHours)];
  if (b.hour > h) return;                       // older than the hour this bucket now holds
  if (b.hour < h) { b = Bucket{}; b.hour = h; } // a new hour: the bucket starts again
  uint16_t& c = b.n[(int)k - 1];
  if (c < 0xFFFF) c++;
}

void WeekCounts::counts(int64_t nowUtcS, uint32_t* x, uint32_t* s, uint32_t* hu, uint32_t* d) const {
  const int64_t now = floorDiv(nowUtcS, 3600);
  uint32_t t[4] = {};
  for (const Bucket& b : b_) {
    if (b.hour < 0 || b.hour > now || b.hour <= now - kHours) continue;
    for (int i = 0; i < 4; i++) t[i] += b.n[i];
  }
  *x = t[0];
  *s = t[1];
  *hu = t[2];
  *d = t[3];
}

// ---------------------------------------------------------------- log files
// Reads "-sN" / "-pN" if next; returns the characters used.
static int suffixNum(const char* s, char letter, int* v) {
  if (s[0] != '-' || s[1] != letter || s[2] < '0' || s[2] > '9') return 0;
  int k = 2, n = 0;
  while (s[k] >= '0' && s[k] <= '9' && k < 6) { n = n * 10 + (s[k] - '0'); k++; }
  *v = n;
  return k;
}

static bool tail(const char* rest, LogName* n) {
  int v;
  int k = suffixNum(rest, 's', &v);
  if (k) { if (v < 1) return false; n->schema = v; rest += k; }
  k = suffixNum(rest, 'p', &v);
  if (k) { if (v < 2) return false; n->part = v; rest += k; }
  return strcmp(rest, ".csv") == 0;
}

bool parseLogName(const char* name, LogName* out) {
  LogName n;
  if (strncmp(name, "hourly", 6) == 0) {
    n.kind = LogFileKind::Hourly;
    if (!tail(name + 6, &n)) return false;
    *out = n;
    return true;
  }
  int y, m;
  if (strlen(name) < 12 || !digits(name, 4, &y) || name[4] != '-' || !digits(name + 5, 2, &m) || m < 1 || m > 12)
    return false;
  const char* rest = name + 7;
  if (strncmp(rest, "-readings", 9) == 0) { n.kind = LogFileKind::Readings; rest += 9; }
  else if (strncmp(rest, "-events", 7) == 0) { n.kind = LogFileKind::Events; rest += 7; }
  else return false;
  n.year = y;
  n.month = m;
  if (!tail(rest, &n)) return false;
  *out = n;
  return true;
}

bool parseLogPath(const char* path, LogName* out) {
  if (!path || strncmp(path, "/logs/", 6) != 0 || strlen(path) >= sizeof(LogFileEntry{}.path)) return false;
  const char* p = path + 6;
  LogName n;
  if (strncmp(p, "hourly", 6) == 0) {
    if (!parseLogName(p, &n) || n.kind != LogFileKind::Hourly) return false;
    *out = n;
    return true;
  }
  int y;
  if (!digits(p, 4, &y) || p[4] != '/') return false;
  if (!parseLogName(p + 5, &n) || n.kind == LogFileKind::Hourly || n.year != y) return false;
  *out = n;
  return true;
}

const char* logFileKindName(LogFileKind k) {
  switch (k) {
    case LogFileKind::Readings: return "readings";
    case LogFileKind::Events:   return "events";
    case LogFileKind::Hourly:   return "hourly";
  }
  return "?";
}

int64_t logOrderKey(const LogName& n) {
  const int64_t month = n.kind == LogFileKind::Hourly ? 0 : (int64_t)n.year * 12 + (n.month - 1);
  const int kind = n.kind == LogFileKind::Readings ? 2 : n.kind == LogFileKind::Events ? 1 : 0;
  return ((month * 100 + n.schema) * 100 + n.part) * 4 + kind;   // the kind only orders a month's files
}

// Entries that aren't log paths count as the oldest, so they sort to the end.
static int64_t keyOf(const LogFileEntry& e) {
  LogName n;
  return parseLogPath(e.path, &n) ? logOrderKey(n) : INT64_MIN;
}

void sortLogFilesNewestFirst(LogFileEntry* f, int n) {
  for (int i = 1; i < n; i++) {                   // insertion sort (stable): a few dozen files
    const LogFileEntry k = f[i];
    const int64_t kk = keyOf(k);
    int j = i - 1;
    while (j >= 0 && keyOf(f[j]) < kk) { f[j + 1] = f[j]; j--; }
    f[j + 1] = k;
  }
}

bool olderEventsFile(const LogFileEntry* f, int n, const char* path, char* out, size_t outN) {
  LogName me;
  if (!parseLogPath(path, &me)) return false;
  const int64_t mine = logOrderKey(me);
  int best = -1;
  int64_t bestKey = 0;
  for (int i = 0; i < n; i++) {
    LogName x;
    if (!parseLogPath(f[i].path, &x) || x.kind != LogFileKind::Events) continue;
    const int64_t k = logOrderKey(x);
    if (k < mine && (best < 0 || k > bestKey)) { best = i; bestKey = k; }
  }
  if (best < 0) return false;
  snprintf(out, outN, "%s", f[best].path);
  return true;
}

bool newestEventsFile(const LogFileEntry* f, int n, char* out, size_t outN) {
  int best = -1;
  int64_t bestKey = 0;
  for (int i = 0; i < n; i++) {
    LogName x;
    if (!parseLogPath(f[i].path, &x) || x.kind != LogFileKind::Events) continue;
    const int64_t k = logOrderKey(x);
    if (best < 0 || k > bestKey) { best = i; bestKey = k; }
  }
  if (best < 0) return false;
  snprintf(out, outN, "%s", f[best].path);
  return true;
}

// ---------------------------------------------------------------- cursors
int formatCursor(char* out, size_t n, const char* path, uint64_t offset) {
  return snprintf(out, n, "%s@%llu", path, (unsigned long long)offset);
}

bool parseCursor(const char* s, char* path, size_t pathN, uint64_t* offset) {
  if (!s) return false;
  const char* at = strrchr(s, '@');
  if (!at || at == s || at[1] == 0) return false;
  const size_t pl = (size_t)(at - s);
  if (pl + 1 > pathN) return false;
  for (const char* d = at + 1; *d; d++)
    if (*d < '0' || *d > '9') return false;
  memcpy(path, s, pl);
  path[pl] = 0;
  LogName n;
  if (!parseLogPath(path, &n) || n.kind != LogFileKind::Events) return false;
  *offset = strtoull(at + 1, nullptr, 10);
  return true;
}

}  // namespace wineador
