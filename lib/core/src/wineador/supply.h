#pragma once
// Rev Q's 12V sense (spec 3A.3, 3A.11, 5.4): the plain-logic half.
//   - The driver reads GPIO8 (ADC1, 12 dB) every 100 ms and hands over the volts.
//   - Ten readings make one 1 s average.
//   - Each readings row logs the mean of the 1 s averages in its interval and the lowest
//     1 s average in it (takeInterval()).
//   - A DIP: below 11.0 V for 1 s or more (ten readings in a row below it) is an event;
//     a RECOVERED event follows once it has been back at or above 11.0 V for 1 s, with
//     the lowest reading seen during the dip.
// Volts = pin millivolts / 1000 x scale. The scale is the divider's (100k + 15k) / 15k
// until BP2 test 27 measures it (build log); include/board.h holds it.

#include <stdint.h>

namespace wineador {

constexpr float kDividerScale12V = (100.0f + 15.0f) / 15.0f;   // nominal: 12 V reads 1.57 V

inline float supplyVolts(uint32_t pinMv, float scale) { return (float)pinMv / 1000.0f * scale; }

class SupplySense {
 public:
  static constexpr uint32_t kSampleMs = 100;    // one reading every 100 ms
  static constexpr int kPerAverage = 10;        // 10 readings = 1 s
  static constexpr float kDipBelowV = 11.0f;    // spec 3A.11

  struct Event {
    enum class Kind : uint8_t { Dip, Recovered } kind;
    float lowestV;          // the lowest reading during the dip (Recovered); the reading that confirmed it (Dip)
    uint32_t durationMs;    // Recovered: how long it was below 11.0 V
  };

  // One reading. True when it completed a new 1 s average.
  bool add(float volts, uint32_t nowMs);

  bool haveAverage() const { return haveAvg_; }
  float lastAverage() const { return lastAvg_; }

  // The readings row's two values since the last call; false if no 1 s average completed.
  bool takeInterval(float* meanV, float* lowest1sV) { return take(0, meanV, lowest1sV); }
  // The same for the hourly row (its own window, so the two never take each other's figures).
  bool takeHour(float* meanV, float* lowest1sV) { return take(1, meanV, lowest1sV); }

  // Dip and recovery events, in order; true while there is one to take.
  bool takeEvent(Event& e);
  bool inDip() const { return inDip_; }

 private:
  float sum_ = 0;
  int n_ = 0;
  bool haveAvg_ = false;
  float lastAvg_ = 0;
  // the windows: 0 = the readings row, 1 = the hourly row
  struct Window { float sum = 0; int n = 0; float low = 0; };
  Window win_[2];
  bool take(int w, float* meanV, float* lowest1sV);
  // dip
  int below_ = 0, above_ = 0;
  bool inDip_ = false;
  float dipLow_ = 0;
  uint32_t dipStartMs_ = 0;
  Event ev_[2];
  int evN_ = 0;
};

}  // namespace wineador
