#include "wineador/hourly.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace wineador {

void HourlyStats::begin(uint32_t hourKey, const char* hourStamp, bool trusted) {
  key_ = hourKey;
  strncpy(stamp_, hourStamp ? hourStamp : "", sizeof(stamp_) - 1);
  stamp_[sizeof(stamp_) - 1] = 0;
  trusted_ = trusted;
  samples_ = 0;
  for (int i = 0; i < kNumSensors; i++) { t_[i] = Acc{}; rh_[i] = Acc{}; }
  cabT_ = cabRh_ = ctl_ = fan_ = rpm_ = Acc{};
  onMs_[0] = onMs_[1] = onMs_[2] = 0;
  hasV12_ = hasPsram_ = false;
}

void HourlyStats::addSweep(const bool healthy[kNumSensors], const float tempC[kNumSensors],
                           const float rhPct[kNumSensors], const CabinetSummary& cab, uint8_t fanPct, uint32_t fanRpm) {
  samples_++;
  for (int i = 0; i < kNumSensors; i++) {
    if (!healthy[i]) continue;
    t_[i].add(tempC[i]);
    rh_[i].add(rhPct[i]);
  }
  if (cab.cabinetOk > 0) {
    cabT_.add(cab.meanC);
    cabRh_.add(cab.meanRhPct);
    ctl_.add(cab.controlC);
  }
  fan_.add(fanPct);
  rpm_.add((float)fanRpm);
}

void HourlyStats::addOnTime(uint32_t dtMs, const LoadOutputs& live) {
  if (live.cool) onMs_[0] += dtMs;
  if (live.heat) onMs_[1] += dtMs;
  if (live.humid) onMs_[2] += dtMs;
}

namespace {
struct Out {
  char* p;
  size_t n;
  size_t len = 0;
  Out(char* p_, size_t n_) : p(p_), n(n_) { if (n) p[0] = 0; }
  void add(const char* fmt, ...) {
    if (len + 1 >= n) return;
    va_list a;
    va_start(a, fmt);
    int w = vsnprintf(p + len, n - len, fmt, a);
    va_end(a);
    if (w > 0) len = (len + w < n) ? len + w : n - 1;
  }
};
void three(Out& o, const HourlyStats::Acc& a, int decimals) {
  if (a.n == 0) { o.add(",,,"); return; }
  o.add(",%.*f,%.*f,%.*f", decimals, a.mn, decimals, a.mx, decimals, (float)(a.sum / a.n));
}
}  // namespace

int HourlyStats::formatRow(char* out, size_t n) const {
  Out o(out, n);
  o.add("%s,%d,%lu", stamp_, trusted_ ? 1 : 0, (unsigned long)samples_);
  for (int i = 0; i < kNumSensors; i++) { three(o, t_[i], 2); three(o, rh_[i], 2); }
  three(o, cabT_, 2);
  three(o, cabRh_, 2);
  three(o, ctl_, 2);
  if (fan_.n) o.add(",%.1f,%.0f", (float)(fan_.sum / fan_.n), (float)(rpm_.sum / rpm_.n));
  else        o.add(",,");
  o.add(",%lu,%lu,%lu,%lu", (unsigned long)(onMs_[0] / 1000), (unsigned long)(onMs_[1] / 1000),
        (unsigned long)(onMs_[2] / 1000), (unsigned long)heapMin_);
  if (hasV12_) o.add(",%.2f,%.2f", v12Mean_, v12Low_);
  else         o.add(",,");
  if (hasPsram_) o.add(",%lu\n", (unsigned long)psramMin_);
  else           o.add(",\n");
  return (int)o.len;
}

}  // namespace wineador
