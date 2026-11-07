#include "wineador/relay_exercise.h"

namespace wineador {

void RelayExercise::begin(uint32_t nowMs, uint32_t periodMs, uint32_t onMs, uint32_t offMs, uint8_t cycles) {
  periodMs_ = periodMs;
  onMs_ = onMs;
  offMs_ = offMs;
  cycles_ = cycles;
  cycle_ = 0;
  state_ = State::Idle;
  lastStartMs_ = nowMs;
  stateAtMs_ = nowMs;
  completed_ = 0;
  qHead_ = qLen_ = 0;
}

bool RelayExercise::request(uint32_t nowMs) {
  if (state_ != State::Idle) return false;
  state_ = State::Waiting;
  stateAtMs_ = nowMs;
  lastStartMs_ = nowMs;      // the next weekly one is a week from now
  push(Event::Waiting);
  return true;
}

void RelayExercise::update(uint32_t nowMs, bool tempLoadsOff) {
  switch (state_) {
    case State::Idle:
      if (elapsedMs(nowMs, lastStartMs_) >= periodMs_) request(nowMs);
      break;
    case State::Waiting:
      if (tempLoadsOff) {
        cycle_ = 1;
        state_ = State::On;
        stateAtMs_ = nowMs;
        push(Event::Energised);
      }
      break;
    case State::On:
      if (elapsedMs(nowMs, stateAtMs_) >= onMs_) {
        state_ = State::Off;
        stateAtMs_ = nowMs;
        push(Event::Released);
      }
      break;
    case State::Off:
      if (elapsedMs(nowMs, stateAtMs_) >= offMs_) {
        if (cycle_ >= cycles_) {
          state_ = State::Idle;
          completed_++;
          push(Event::Done);
        } else {
          cycle_++;
          state_ = State::On;
          stateAtMs_ = nowMs;
          push(Event::Energised);
        }
      }
      break;
  }
}

int RelayExercise::relayHold() const {
  if (state_ == State::On) return 1;
  if (state_ == State::Off) return 0;
  return -1;
}

void RelayExercise::push(Event e) {
  if (qLen_ == kQ) { qHead_ = (qHead_ + 1) % kQ; qLen_--; }   // drop the oldest
  q_[(qHead_ + qLen_) % kQ] = Queued{e, cycle_};
  qLen_++;
}

RelayExercise::Event RelayExercise::takeEvent(uint8_t* cycleOut) {
  if (qLen_ == 0) return Event::None;
  Queued x = q_[qHead_];
  qHead_ = (qHead_ + 1) % kQ;
  qLen_--;
  if (cycleOut) *cycleOut = x.cycle;
  return x.e;
}

}  // namespace wineador
