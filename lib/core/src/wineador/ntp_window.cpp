#include "wineador/ntp_window.h"

namespace wineador {

NtpWindow::Action NtpWindow::update(uint32_t nowMs, uint32_t syncCount) {
  if (open_) {
    if (syncCount != syncsAtOpen_) {
      open_ = false;
      closedMs_ = nowMs;
      waitMs_ = kIntervalMs;
      return Action::CloseSynced;
    }
    if (nowMs - openedMs_ >= kWindowMs) {
      open_ = false;
      closedMs_ = nowMs;
      waitMs_ = kRetryMs;
      return Action::CloseTimedOut;
    }
    return Action::None;
  }
  if (!started_ || now_ || nowMs - closedMs_ >= waitMs_) {
    started_ = true;
    now_ = false;
    open_ = true;
    openedMs_ = nowMs;
    syncsAtOpen_ = syncCount;
    return Action::Open;
  }
  return Action::None;
}

}  // namespace wineador
