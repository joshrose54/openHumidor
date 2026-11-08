#include "wineador/fan_monitor.h"

namespace wineador {

void FanMonitor::push(Event e) {
  if (qLen_ == kQ) { qHead_ = (qHead_ + 1) % kQ; qLen_--; }
  const int at = (qHead_ + qLen_) % kQ;
  q_[at] = e;
  qKick_[at] = kicksThisStall_;
  qLen_++;
}

FanMonitor::Event FanMonitor::takeEvent(uint32_t* kickNo) {
  if (qLen_ == 0) return Event::None;
  const Event e = q_[qHead_];
  if (kickNo) *kickNo = qKick_[qHead_];
  qHead_ = (qHead_ + 1) % kQ;
  qLen_--;
  return e;
}

bool FanMonitor::window(uint32_t now, bool pwmAsserted, uint32_t rpm) {
  if (!pwmAsserted) return false;            // fan meant to be stopped: nothing to judge
  if (rpm > 0) {
    if (stalled_) {
      stalled_ = false;
      push(Event::Recovered);
    }
    return false;
  }
  // Tach reads zero with PWM asserted.
  if (!stalled_) {
    stalled_ = true;
    stalls_++;
    kicksThisStall_ = 0;
    push(Event::Stall);
  } else if (elapsedMs(now, lastKickMs_) < retryMs_) {
    return false;
  }
  kicksThisStall_++;
  lastKickMs_ = now;
  push(Event::Kick);
  return true;
}

}  // namespace wineador
