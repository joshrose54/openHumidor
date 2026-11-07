#include "wineador/interlock.h"

namespace wineador {

void InterlockSequencer::begin(uint32_t settleMs, uint32_t nowMs) {
  settleMs_ = settleMs;
  relayHeating_ = false;   // cooling side, as at power-up
  settled_ = true;
  movedAtMs_ = nowMs;
  moves_ = 0;
}

LoadOutputs InterlockSequencer::step(uint32_t nowMs, bool wantCool, bool wantHeat, bool wantHumid, int relayHold) {
  // Both asked for should never happen (the guards prevent it). If it does,
  // neither is asserted.
  if (wantCool && wantHeat) wantCool = wantHeat = false;

  const bool held = relayHold >= 0;
  const bool wantRelayHeating = held ? (relayHold == 1) : wantHeat;

  if (wantRelayHeating != relayHeating_) {
    relayHeating_ = wantRelayHeating;
    movedAtMs_ = nowMs;
    settled_ = false;
    moves_++;
  }
  // Latched once true, so a millis() wrap can't make it "unsettled" again.
  if (!settled_ && elapsedMs(nowMs, movedAtMs_) >= settleMs_) settled_ = true;

  LoadOutputs o;
  o.relayHeating = relayHeating_;
  o.cool  = !held && wantCool && !relayHeating_ && settled_;
  o.heat  = !held && wantHeat &&  relayHeating_ && settled_;
  o.humid = wantHumid;     // not behind K1: software guards only (4.4)
  return o;
}

}  // namespace wineador
