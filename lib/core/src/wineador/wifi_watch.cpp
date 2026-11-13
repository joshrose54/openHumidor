#include "wineador/wifi_watch.h"

namespace wineador {

WifiWatch::Event WifiWatch::update(bool connected, uint32_t nowMs) {
  if (connected == up_) return Event::None;
  up_ = connected;
  if (connected) {
    if (!ever_) { ever_ = true; return Event::FirstConnect; }
    outageMs_ = nowMs - lostAtMs_;
    return Event::Back;
  }
  lostAtMs_ = nowMs;
  outages_ = outages_ + 1;
  return Event::Lost;
}

}  // namespace wineador
