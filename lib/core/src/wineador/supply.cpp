#include "wineador/supply.h"

namespace wineador {

bool SupplySense::add(float volts, uint32_t nowMs) {
  // ---- dip: ten readings in a row below 11.0 V; recovered after ten in a row at or above
  if (volts < kDipBelowV) {
    above_ = 0;
    if (below_ == 0) dipStartMs_ = nowMs;    // the first reading below (when the dip began)
    if (below_ < kPerAverage) below_++;
    if (inDip_ && volts < dipLow_) dipLow_ = volts;
    if (!inDip_) {
      if (below_ == 1 || volts < dipLow_) dipLow_ = volts;
      if (below_ >= kPerAverage && evN_ < 2) {
        inDip_ = true;
        ev_[evN_++] = Event{Event::Kind::Dip, volts, 0};
      }
    }
  } else {
    if (!inDip_) below_ = 0;
    if (inDip_) {
      if (above_ < kPerAverage) above_++;
      if (above_ >= kPerAverage && evN_ < 2) {
        inDip_ = false;
        below_ = 0;
        // below from the first low reading to the first good one of this run of ten
        const uint32_t dur = (nowMs - dipStartMs_) - (uint32_t)(kPerAverage - 1) * kSampleMs;
        ev_[evN_++] = Event{Event::Kind::Recovered, dipLow_, dur};
      }
    }
  }

  // ---- 1 s averages
  sum_ += volts;
  if (++n_ < kPerAverage) return false;
  lastAvg_ = sum_ / (float)n_;
  haveAvg_ = true;
  sum_ = 0;
  n_ = 0;
  for (Window& w : win_) {
    if (w.n == 0 || lastAvg_ < w.low) w.low = lastAvg_;
    w.sum += lastAvg_;
    w.n++;
  }
  return true;
}

bool SupplySense::take(int which, float* meanV, float* lowest1sV) {
  Window& w = win_[which];
  if (w.n == 0) return false;
  if (meanV) *meanV = w.sum / (float)w.n;
  if (lowest1sV) *lowest1sV = w.low;
  w = Window{};
  return true;
}

bool SupplySense::takeEvent(Event& e) {
  if (evN_ == 0) return false;
  e = ev_[0];
  ev_[0] = ev_[1];
  evN_--;
  return true;
}

}  // namespace wineador
