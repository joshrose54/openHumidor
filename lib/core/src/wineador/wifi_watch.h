#pragma once
// Rev Q keeps WiFi connected (spec 5.4): "WiFi lost", and "back after N s", as events.
// Plain logic: told whether WiFi is connected each pass, it says what happened.
//   - Nothing is reported until the first connection after boot (connecting at boot is
//     not a loss).
//   - LOST when a connection drops; BACK when it returns, with how long it was gone.
// The 10-an-hour limit (5.4) is applied where the events are logged, per outage, so a
// "lost" is never logged without its "back".

#include <stdint.h>

namespace wineador {

class WifiWatch {
 public:
  enum class Event : uint8_t { None, FirstConnect, Lost, Back };

  Event update(bool connected, uint32_t nowMs);
  uint32_t lastOutageMs() const { return outageMs_; }   // for Back
  bool everConnected() const { return ever_; }
  uint32_t outages() const { return outages_; }

 private:
  bool ever_ = false, up_ = false;
  uint32_t lostAtMs_ = 0, outageMs_ = 0, outages_ = 0;
};

}  // namespace wineador
