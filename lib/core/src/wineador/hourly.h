#pragma once
// The hourly file (spec 5.4): min, max and mean over each UTC hour of each
// channel and derived value, from every sensor read (healthy sensors only), plus
// how long each load was on in the hour. What the dashboard graphs from.

#include "wineador/types.h"
#include "wineador/summary.h"

namespace wineador {

class HourlyStats {
 public:
  struct Acc {
    float mn = 0, mx = 0;
    double sum = 0;
    uint32_t n = 0;
    void add(float v) {
      if (n == 0) { mn = mx = v; }
      if (v < mn) mn = v;
      if (v > mx) mx = v;
      sum += v;
      n++;
    }
  };

  // The hour key is (year * 12 + month) * 31 * 24 + day * 24 + hour — any value that
  // changes once an hour. Starting a new hour returns true from rolled().
  void begin(uint32_t hourKey, const char* hourStamp, bool trusted);

  // After each sensor read: healthy flags and values per channel, the summary.
  void addSweep(const bool healthy[kNumSensors], const float tempC[kNumSensors], const float rhPct[kNumSensors],
                const CabinetSummary& cab, uint8_t fanPct, uint32_t fanRpm);
  // Every pass: how long the loads have been on since the last call.
  void addOnTime(uint32_t dtMs, const LoadOutputs& live);

  uint32_t hourKey() const { return key_; }
  uint32_t samples() const { return samples_; }

  // The row for the hour so far (one line, newline included).
  int formatRow(char* out, size_t n) const;

 private:
  uint32_t key_ = 0;
  char stamp_[24] = {};
  bool trusted_ = false;
  uint32_t samples_ = 0;
  Acc t_[kNumSensors], rh_[kNumSensors];
  Acc cabT_, cabRh_, ctl_, fan_, rpm_;
  uint64_t onMs_[3] = {};
};

}  // namespace wineador
