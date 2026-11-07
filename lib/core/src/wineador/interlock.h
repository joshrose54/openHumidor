#pragma once
// The heat/cool interlock switching sequence (spec 3.2):
//   drop the load signal -> change K1 -> wait 20 ms to settle -> assert the new load.
// The settle is a state, not a delay: step() is called every pass and simply
// does not assert the new load until 20 ms have passed since K1 moved.
//
// K1 rests on the COOLING side (de-energised). It moves to heating only while
// the heater is allowed to run, or while the relay exercise holds it there.
// A load signal is only ever asserted when K1 has settled on that load's side —
// this is the "Interlock" row of the guard tables (4.4).
//
// Output order on the pins (outputs::apply): load signals going off first,
// then K1, then load signals going on. On the pass K1 moves, both temperature
// load signals are off.

#include "wineador/types.h"

namespace wineador {

class InterlockSequencer {
 public:
  void begin(uint32_t settleMs, uint32_t nowMs);

  // wantCool / wantHeat / wantHumid: what the guards allow this pass.
  // relayHold: -1 = none; 0 = hold K1 on the cooling side; 1 = heating side
  // (relay exercise). While held, neither temperature load is asserted.
  LoadOutputs step(uint32_t nowMs, bool wantCool, bool wantHeat, bool wantHumid, int relayHold = -1);

  bool relayHeating() const { return relayHeating_; }
  bool settled() const { return settled_; }
  uint32_t relayMoves() const { return moves_; }

 private:
  uint32_t settleMs_ = 20;
  bool relayHeating_ = false;
  bool settled_ = true;
  uint32_t movedAtMs_ = 0;
  uint32_t moves_ = 0;
};

}  // namespace wineador
