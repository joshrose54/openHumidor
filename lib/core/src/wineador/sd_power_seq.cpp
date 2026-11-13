#include "wineador/sd_power_seq.h"

namespace wineador {

void SdPowerSeq::startCycle(uint32_t nowMs) {
  (void)nowMs;
  if (state_ != State::On) return;     // one at a time
  cycle_ = true;
  wantOn_ = true;
  state_ = State::Releasing;
}

void SdPowerSeq::requestOff(uint32_t nowMs) {
  (void)nowMs;
  if (state_ != State::On) return;
  cycle_ = false;
  wantOn_ = false;
  state_ = State::Releasing;
}

void SdPowerSeq::requestOn() {
  if (state_ == State::Off || state_ == State::Cutting || state_ == State::Releasing) wantOn_ = true;
}

uint32_t SdPowerSeq::onAllowedInMs(uint32_t nowMs) const {
  if (state_ != State::Off) return 0;
  const uint32_t off = nowMs - cutMs_;
  return off >= kMinOffMs ? 0 : kMinOffMs - off;
}

SdPowerSeq::Action SdPowerSeq::next(uint32_t nowMs) {
  switch (state_) {
    case State::On:
      return Action::None;
    case State::Releasing:                 // the bus is released BEFORE the power is cut
      state_ = State::Cutting;
      return Action::ReleaseBus;
    case State::Cutting:
      state_ = State::Off;
      cutMs_ = nowMs;
      return Action::CutPower;
    case State::Off: {
      if (!wantOn_) return Action::None;
      const uint32_t wait = cycle_ ? kCycleOffMs : kMinOffMs;
      if (nowMs - cutMs_ < wait) return Action::None;
      wantOn_ = false;
      onMs_ = nowMs;
      if (cycle_) { state_ = State::Settling; }
      else        { state_ = State::On; }
      return Action::SoftOn;
    }
    case State::Settling:
      if (nowMs - onMs_ < kSettleMs) return Action::None;
      state_ = State::On;
      cycle_ = false;
      return Action::Remount;
  }
  return Action::None;
}

}  // namespace wineador
