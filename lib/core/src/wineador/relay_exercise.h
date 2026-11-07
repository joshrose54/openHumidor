#pragma once
// Weekly relay exercise (spec 2.4, 5.2): three full cycles of K1 with both
// temperature loads off — each cycle 1 s energised, 1 s released — ending on
// the cooling side. Gold contacts on a relay that rarely moves can grow a film;
// moving it keeps them clean, and the ineffective checks (5.6) catch a bad one.
//
//   Idle -> Waiting (a week has passed, or the 'exercise' command)
//        -> Cycling, once compressor and heater are both off
//        -> Idle, K1 back on the cooling side
//
// While Waiting, the compressor and heater may not START (a running one carries
// on until it stops by its own rules, at most its maximum run). While Cycling,
// both are held off. The humidifier is not behind K1 and is unaffected.
// The week is counted from boot, then from the last exercise.

#include "wineador/types.h"

namespace wineador {

class RelayExercise {
 public:
  enum class Event : uint8_t { None, Waiting, Energised, Released, Done };

  void begin(uint32_t nowMs, uint32_t periodMs, uint32_t onMs, uint32_t offMs, uint8_t cycles);

  // Start one now (the 'exercise' command). Returns false if one is already under way.
  bool request(uint32_t nowMs);

  // Every pass. tempLoadsOff: compressor and heater both off right now.
  void update(uint32_t nowMs, bool tempLoadsOff);

  bool waiting() const { return state_ == State::Waiting; }
  bool cycling() const { return state_ == State::On || state_ == State::Off; }
  bool active()  const { return state_ != State::Idle; }

  // For the interlock sequencer: -1 = not holding K1; 1 = energised; 0 = released.
  int relayHold() const;

  uint8_t cycle() const { return cycle_; }         // 1..cycles while cycling
  uint8_t cycles() const { return cycles_; }
  uint32_t completed() const { return completed_; }

  // Events since the last call, oldest first, for the serial output.
  Event takeEvent(uint8_t* cycleOut = nullptr);

 private:
  enum class State : uint8_t { Idle, Waiting, On, Off };
  void push(Event e);

  State state_ = State::Idle;
  uint32_t periodMs_ = 0, onMs_ = 1000, offMs_ = 1000;
  uint8_t cycles_ = 3, cycle_ = 0;
  uint32_t lastStartMs_ = 0;   // when the week was last counted from
  uint32_t stateAtMs_ = 0;
  uint32_t completed_ = 0;

  struct Queued { Event e; uint8_t cycle; };
  static constexpr int kQ = 8;
  Queued q_[kQ] = {};
  int qHead_ = 0, qLen_ = 0;
};

}  // namespace wineador
