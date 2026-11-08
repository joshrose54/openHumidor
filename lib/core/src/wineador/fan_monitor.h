#pragma once
// Plenum fan stall handling (spec 5.6). Fed each tach window's result:
//   tach zero with PWM asserted -> STALL: compressor and heater are already held
//   off by their fan guard (4.4); a kickstart at once, then another every retry
//   interval (5 min real, 30 s bench) while the tach still reads zero;
//   the tach reads again -> RECOVERED, and the guard lets the loads resume.
// Plain logic. The kickstart itself is FanController's (100% for 1 s).

#include "wineador/types.h"

namespace wineador {

class FanMonitor {
 public:
  enum class Event : uint8_t { None, Stall, Kick, Recovered };

  void begin(uint32_t retryMs) { *this = FanMonitor{}; retryMs_ = retryMs; }

  // After each tach window. Returns true if a kickstart should be done now.
  bool window(uint32_t nowMs, bool pwmAsserted, uint32_t rpm);

  bool stalled() const { return stalled_; }
  uint32_t kicksThisStall() const { return kicksThisStall_; }
  uint32_t stalls() const { return stalls_; }

  Event takeEvent(uint32_t* kickNo = nullptr);

 private:
  void push(Event e);
  uint32_t retryMs_ = 0;
  bool stalled_ = false;
  uint32_t lastKickMs_ = 0;
  uint32_t kicksThisStall_ = 0;
  uint32_t stalls_ = 0;
  static constexpr int kQ = 4;
  Event q_[kQ] = {};
  uint32_t qKick_[kQ] = {};
  int qHead_ = 0, qLen_ = 0;
};

}  // namespace wineador
