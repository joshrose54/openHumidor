#include "wineador/history.h"
#include "wineador/log_read.h"
#include "wineador/web_api.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

namespace wineador {

static const char* const kFieldNames[kHistFields] = {
  "cab_t", "cab_t_min", "cab_t_max", "cab_rh",
  "ch0_t", "ch1_t", "ch2_t", "ch3_t", "ch4_t", "ch5_t",
  "ch0_rh", "ch1_rh", "ch2_rh", "ch3_rh", "ch4_rh", "ch5_rh",
  "compressor", "heater", "humidifier", "fan",
};
const char* histFieldName(int f) { return f >= 0 && f < kHistFields ? kFieldNames[f] : "?"; }

static int decimalsFor(int f) {
  if (f >= kHCompressor && f <= kHHumidifier) return 3;
  if (f == kHFan) return 1;
  return 2;
}

bool parseHistRange(const char* s, HistRange* r) {
  if (!s) return false;
  if (strcmp(s, "hour") == 0)  { *r = HistRange::Hour; return true; }
  if (strcmp(s, "day") == 0)   { *r = HistRange::Day; return true; }
  if (strcmp(s, "week") == 0)  { *r = HistRange::Week; return true; }
  if (strcmp(s, "month") == 0) { *r = HistRange::Month; return true; }
  return false;
}

const char* histRangeName(HistRange r) {
  switch (r) {
    case HistRange::Hour:  return "hour";
    case HistRange::Day:   return "day";
    case HistRange::Week:  return "week";
    case HistRange::Month: return "month";
  }
  return "?";
}

// ---------------------------------------------------------------- rows -> points
static float numOrNan(const char* s) {
  if (!s || !*s) return NAN;
  char* end = nullptr;
  const double v = strtod(s, &end);
  if (end == s || *end) return NAN;
  return (float)v;
}

static void clearPoint(HistPoint* p) {
  p->t = 0;
  for (int i = 0; i < kHistFields; i++) p->v[i] = NAN;
}

static bool split(const char* line, size_t len, char* buf, char** f, int maxF, int* k) {
  if (len == 0 || len >= kMaxLogLine || line[0] == '#') return false;
  memcpy(buf, line, len);
  buf[len] = 0;
  *k = csvSplit(buf, f, maxF);
  return true;
}

// Readings, schema 2 (logbook.cpp): timestamp, time_trusted, then per channel t, rh, rh_raw, flag
// (columns 2-25), cab_mean_t 26, cab_min_t 27, cab_max_t 28, cab_mean_rh 29, control_t 30,
// fan_pwm 31, fan_rpm 32, compressor 33, heater_duty 34, humidifier 35, ...
bool parseReadingsLine(const char* line, size_t len, HistPoint* p) {
  char buf[kMaxLogLine];
  char* f[64];
  int k;
  if (!split(line, len, buf, f, 64, &k) || k < 36) return false;
  int64_t t;
  if (!parseIsoUtc(f[0], &t) || strcmp(f[1], "1") != 0) return false;
  clearPoint(p);
  p->t = t;
  p->v[kHCabT] = numOrNan(f[26]);
  p->v[kHCabTMin] = numOrNan(f[27]);
  p->v[kHCabTMax] = numOrNan(f[28]);
  p->v[kHCabRh] = numOrNan(f[29]);
  for (int c = 0; c < 6; c++) {
    if (strcmp(f[5 + 4 * c], "ok") != 0) continue;
    p->v[kHCh0T + c] = numOrNan(f[2 + 4 * c]);
    p->v[kHCh0Rh + c] = numOrNan(f[3 + 4 * c]);
  }
  p->v[kHFan] = numOrNan(f[31]);
  p->v[kHCompressor] = strcmp(f[33], "1") == 0 ? 1.0f : 0.0f;
  const float hd = numOrNan(f[34]);
  p->v[kHHeater] = (!isnan(hd) && hd > 0) ? 1.0f : 0.0f;
  p->v[kHHumidifier] = strcmp(f[35], "1") == 0 ? 1.0f : 0.0f;
  return true;
}

// Hourly, schema 2 (logbook.cpp): hour, time_trusted, samples, then per channel t_min, t_max,
// t_mean, rh_min, rh_max, rh_mean (columns 3-38), cab_mean_t min/max/mean 39-41, cab_mean_rh
// min/max/mean 42-44, control_t 45-47, fan_pwm_mean 48, fan_rpm_mean 49, compressor_on_s 50,
// heater_on_s 51, humidifier_on_s 52, ...
bool parseHourlyLine(const char* line, size_t len, HistPoint* p) {
  char buf[kMaxLogLine];
  char* f[72];
  int k;
  if (!split(line, len, buf, f, 72, &k) || k < 53) return false;
  int64_t t;
  if (!parseIsoUtc(f[0], &t) || strcmp(f[1], "1") != 0) return false;
  clearPoint(p);
  p->t = t;
  p->v[kHCabT] = numOrNan(f[41]);
  p->v[kHCabRh] = numOrNan(f[44]);
  float lo = NAN, hi = NAN;
  for (int c = 0; c < 6; c++) {
    p->v[kHCh0T + c] = numOrNan(f[5 + 6 * c]);
    p->v[kHCh0Rh + c] = numOrNan(f[8 + 6 * c]);
    if (c < 4) {   // the coldest and warmest cabinet sensor over the hour, as the readings' min/max
      const float a = numOrNan(f[3 + 6 * c]), b = numOrNan(f[4 + 6 * c]);
      if (!isnan(a) && (isnan(lo) || a < lo)) lo = a;
      if (!isnan(b) && (isnan(hi) || b > hi)) hi = b;
    }
  }
  p->v[kHCabTMin] = lo;
  p->v[kHCabTMax] = hi;
  p->v[kHFan] = numOrNan(f[48]);
  for (int L = 0; L < 3; L++) {
    const float s = numOrNan(f[50 + L]);
    p->v[kHCompressor + L] = isnan(s) ? NAN : (s >= 3600 ? 1.0f : s / 3600.0f);
  }
  return true;
}

// ---------------------------------------------------------------- the rings
void History::R::push(const HistPoint& x) {
  if (cap <= 0) return;
  if (n < cap) { p[(head + n) % cap] = x; n++; }
  else { p[head] = x; head = (head + 1) % cap; }
}

void History::begin(const Storage& s) {
  raw_ = R{};    raw_.p = s.raw;       raw_.cap = s.rawCap;
  day_ = R{};    day_.p = s.day;       day_.cap = s.dayCap;
  hourly_ = R{}; hourly_.p = s.hourly; hourly_.cap = s.hourlyCap;
  minuteOpen_ = false;
  nWaitR_ = nWaitH_ = 0;
  refillR_ = refillH_ = false;
  dropped_ = 0;
}

void History::closeMinute() {
  if (!minuteOpen_) return;
  HistPoint m;
  clearPoint(&m);
  m.t = minute_;
  for (int f = 0; f < kHistFields; f++) {
    if (f == kHCabTMin) m.v[f] = cnt_[f] ? mn_ : NAN;
    else if (f == kHCabTMax) m.v[f] = cnt_[f] ? mx_ : NAN;
    else m.v[f] = cnt_[f] ? (float)(sum_[f] / cnt_[f]) : NAN;
  }
  day_.push(m);
  minuteOpen_ = false;
}

void History::pushReading(const HistPoint& p) {
  const HistPoint* last = raw_.back();
  if (last && p.t <= last->t) return;              // in time order only
  raw_.push(p);
  const int64_t minute = p.t - ((p.t % 60) + 60) % 60;
  if (minuteOpen_ && minute != minute_) closeMinute();
  if (!minuteOpen_) {
    minuteOpen_ = true;
    minute_ = minute;
    for (int f = 0; f < kHistFields; f++) { sum_[f] = 0; cnt_[f] = 0; }
  }
  for (int f = 0; f < kHistFields; f++) {
    const float v = p.v[f];
    if (isnan(v)) continue;
    if (f == kHCabTMin) { if (!cnt_[f] || v < mn_) mn_ = v; }
    else if (f == kHCabTMax) { if (!cnt_[f] || v > mx_) mx_ = v; }
    else sum_[f] += v;
    cnt_[f]++;
  }
}

void History::pushHourly(const HistPoint& p) {
  const HistPoint* last = hourly_.back();
  if (last && p.t <= last->t) return;
  hourly_.push(p);
}

void History::addReading(const HistPoint& p) {
  if (!refillR_ && readers_ == 0) { pushReading(p); return; }
  if (nWaitR_ == kWaitR) {                       // full: the oldest waiting row is lost
    memmove(waitR_, waitR_ + 1, sizeof(HistPoint) * (kWaitR - 1));
    nWaitR_--;
    dropped_++;
  }
  waitR_[nWaitR_++] = p;
}

void History::addHourly(const HistPoint& p) {
  if (!refillH_ && readers_ == 0) { pushHourly(p); return; }
  if (nWaitH_ == kWaitH) {
    memmove(waitH_, waitH_ + 1, sizeof(HistPoint) * (kWaitH - 1));
    nWaitH_--;
    dropped_++;
  }
  waitH_[nWaitH_++] = p;
}

bool History::addReadingsLine(const char* line, size_t len) {
  HistPoint p;
  if (!parseReadingsLine(line, len, &p)) return false;
  addReading(p);
  return true;
}

bool History::addHourlyLine(const char* line, size_t len) {
  HistPoint p;
  if (!parseHourlyLine(line, len, &p)) return false;
  addHourly(p);
  return true;
}

bool History::beginRefill(Kind k) {
  if (readers_ > 0) return false;
  if (k == Kind::Readings) {
    if (refillR_) return true;
    // rows already held wait (the newest kWaitR), and the rings start again from the card
    nWaitR_ = 0;
    const int from = raw_.n > kWaitR ? raw_.n - kWaitR : 0;
    for (int i = from; i < raw_.n; i++) waitR_[nWaitR_++] = raw_.at(i);
    raw_.clear();
    day_.clear();
    minuteOpen_ = false;
    refillR_ = true;
  } else {
    if (refillH_) return true;
    nWaitH_ = 0;
    const int from = hourly_.n > kWaitH ? hourly_.n - kWaitH : 0;
    for (int i = from; i < hourly_.n; i++) waitH_[nWaitH_++] = hourly_.at(i);
    hourly_.clear();
    refillH_ = true;
  }
  return true;
}

bool History::refillLine(Kind k, const char* line, size_t len) {
  if (readers_ > 0) return false;
  HistPoint p;
  if (k == Kind::Readings) {
    if (!refillR_ || !parseReadingsLine(line, len, &p)) return false;
    pushReading(p);
  } else {
    if (!refillH_ || !parseHourlyLine(line, len, &p)) return false;
    pushHourly(p);
  }
  return true;
}

void History::endRefill(Kind k) {
  if (k == Kind::Readings) refillR_ = false;
  else refillH_ = false;
  flushWaiting();
}

void History::endRead() {
  if (readers_ > 0) readers_--;
  flushWaiting();
}

// The waiting rows go in once nothing holds them: no reply open, and that kind not refilling.
void History::flushWaiting() {
  if (readers_ > 0) return;
  if (!refillR_) {
    for (int i = 0; i < nWaitR_; i++) pushReading(waitR_[i]);
    nWaitR_ = 0;
  }
  if (!refillH_) {
    for (int i = 0; i < nWaitH_; i++) pushHourly(waitH_[i]);
    nWaitH_ = 0;
  }
}

int History::count(Ring r) const {
  switch (r) {
    case Ring::Raw:    return raw_.n;
    case Ring::Day:    return day_.n;
    case Ring::Hourly: return hourly_.n;
  }
  return 0;
}

const HistPoint& History::at(Ring r, int i) const {
  switch (r) {
    case Ring::Raw:    return raw_.at(i);
    case Ring::Day:    return day_.at(i);
    default:           return hourly_.at(i);
  }
}

// ---------------------------------------------------------------- duty
double histDuty(const History& h, History::Ring r, int i0, HistField load, int64_t now) {
  const int n = h.count(r);
  double on = 0, total = 0;
  for (int i = i0; i < n; i++) {
    const float v = h.at(r, i).v[load];
    if (isnan(v)) continue;
    double w = 1;
    if (r == History::Ring::Raw) {
      const int64_t next = i + 1 < n ? h.at(r, i + 1).t : now;
      int64_t d = next - h.at(r, i).t;
      if (d > 120) d = 120;
      if (d < 0) d = 0;
      w = (double)d;
    }
    on += v * w;
    total += w;
  }
  return total > 0 ? on / total : NAN;
}

// ---------------------------------------------------------------- the reply
HistoryReply::HistoryReply(History& h, HistRange r, int64_t now, int64_t since)
    : h_(h), range_(r), now_(now) {
  h_.beginRead();
  int64_t span;
  switch (r) {
    case HistRange::Hour:  ring_ = History::Ring::Raw;    span = 3600;       gapS_ = 300;   stepS_ = 15;   break;
    case HistRange::Day:   ring_ = History::Ring::Day;    span = 86400;      gapS_ = 180;   stepS_ = 60;   break;
    case HistRange::Week:  ring_ = History::Ring::Hourly; span = 7 * 86400;  gapS_ = 10800; stepS_ = 3600; break;
    default:               ring_ = History::Ring::Hourly; span = 31 * 86400; gapS_ = 10800; stepS_ = 3600; break;
  }
  const int64_t from = now - span;
  n_ = h.count(ring_);
  iRange_ = 0;
  while (iRange_ < n_ && pt(iRange_).t <= from) iRange_++;
  i0_ = iRange_;
  if (since > from)
    while (i0_ < n_ && pt(i0_).t <= since) i0_++;
}

// A break before point i: it is further from the point before it (in the range — with `since`,
// the page already has that one) than the gap.
bool HistoryReply::gapBefore(int i) const { return i > iRange_ && pt(i).t - pt(i - 1).t > gapS_; }

bool HistoryReply::step(JsonWriter& w) {
  if (phase_ == 0) {
    w.beginObject();
    w.kInt("api", kApiVersion);
    w.kStr("range", histRangeName(range_));
    w.kInt("step_s", stepS_);
    w.kInt("now", now_);
    w.key("duty");
    w.beginObject();
    w.kNum("compressor", histDuty(h_, ring_, iRange_, kHCompressor, now_), 3);
    w.kNum("heater", histDuty(h_, ring_, iRange_, kHHeater, now_), 3);
    w.kNum("humidifier", histDuty(h_, ring_, iRange_, kHHumidifier, now_), 3);
    w.endObject();
    phase_ = 1;
    col_ = -1;
    return true;
  }
  if (phase_ == 1) {
    if (!colOpen_) {
      w.key(col_ < 0 ? "t" : histFieldName(col_));
      w.beginArray();
      colOpen_ = true;
      idx_ = i0_;
      gapDone_ = false;
      return true;
    }
    if (idx_ < n_) {
      if (gapBefore(idx_) && !gapDone_) {      // a break: one null row between the two points
        if (col_ < 0) w.integer((pt(idx_ - 1).t + pt(idx_).t) / 2);
        else w.null();
        gapDone_ = true;
        return true;
      }
      const HistPoint& p = pt(idx_);
      if (col_ < 0) w.integer(p.t);
      else w.num(p.v[col_], decimalsFor(col_));
      idx_++;
      gapDone_ = false;
      return true;
    }
    w.endArray();
    colOpen_ = false;
    col_++;
    if (col_ >= kHistFields) phase_ = 2;
    return true;
  }
  if (phase_ == 2) {
    w.endObject();
    phase_ = 3;
    return true;
  }
  return false;
}

}  // namespace wineador
