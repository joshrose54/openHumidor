#pragma once
// An independent check that the guards were never breached (B4: "no guard was
// ever breached"). It watches only what was LIVE — not the guard code — and
// measures every start and stop against the safety timers:
//   min off before each start · min on before each stop (compressor, heater) ·
//   max run never exceeded · the crossover delay both ways · never both
//   temperature loads, and each only on its own relay side.
// The first breach is kept, with a description. tools/check_logs.py does the same
// check again from the events file on the Mac.

#include "wineador/types.h"
#include "wineador/settings.h"

namespace wineador {

class GuardAudit {
 public:
  void begin(const Settings& s, uint32_t nowMs);
  void update(uint32_t nowMs, const LoadOutputs& live);

  bool breached() const { return breached_; }
  const char* breach() const { return text_; }
  uint32_t starts(int load) const { return starts_[load]; }     // 0 compressor, 1 heater, 2 humidifier
  uint32_t maxRunStops(int load) const { return maxRunStops_[load]; }

  static constexpr uint32_t kSlackMs = 1500;   // pass and sensor-read granularity

 private:
  void fail(const char* fmt, ...);
  Settings s_;
  LoadOutputs prev_;
  bool known_[3] = {};               // a stop has been seen since begin (min off measurable)
  uint32_t onAt_[3] = {}, offAt_[3] = {};
  uint32_t starts_[3] = {}, maxRunStops_[3] = {};
  bool breached_ = false;
  char text_[160] = {};
};

}  // namespace wineador
