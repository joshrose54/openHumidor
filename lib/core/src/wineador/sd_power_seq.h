#pragma once
// Rev Q's SD card power switch (spec 3A.10, 3A.11): the plain-logic half. GPIO39 drives Q10:
//   - CUT: first release the card's bus (unmount, the four SD lines to inputs) — or the card is
//     back-fed through them — then drive GPIO39 HIGH;
//   - wait at least 100 ms;
//   - ON: GPIO39 to an INPUT with its internal pull-up off (the soft start through R91/C55).
//     It is NEVER driven low: that switches the card on hard. There is no such step here.
//   - the card is on about 3 ms after the pin is released, with a ~1 ms rise; remount 10 ms on.
// Used by the SD task (5.4 / 3A.11: when the remount for a failed write's retry fails, power-
// cycle the card once before setting SD FAIL) and by the BP2 test 26 sketch. The driver
// (src/drivers/sd_power) does each Action; this decides the order and the timing.

#include <stdint.h>

namespace wineador {

class SdPowerSeq {
 public:
  static constexpr uint32_t kMinOffMs = 100;    // spec 3A.11: at least 100 ms off
  static constexpr uint32_t kCycleOffMs = 150;  // a power cycle stays off this long (margin over 100)
  static constexpr uint32_t kSettleMs = 10;     // switched on -> remount (3A.10: on in ~3 ms, ~1 ms rise)

  // What the driver must do now. There is deliberately no "drive the pin low".
  enum class Action : uint8_t {
    None,          // nothing due yet (waiting)
    ReleaseBus,    // unmount; SCK, MOSI, MISO, CS to inputs
    CutPower,      // GPIO39 output HIGH
    SoftOn,        // GPIO39 input, internal pull-up off
    Remount,       // mount the card again
  };
  enum class State : uint8_t { On, Releasing, Cutting, Off, Settling };

  // A full power cycle: release, cut, kCycleOffMs, soft on, kSettleMs, remount.
  void startCycle(uint32_t nowMs);
  // Separately (BP2 test 26): switch off now; switch on when allowed (at least kMinOffMs after the cut).
  void requestOff(uint32_t nowMs);
  void requestOn();

  // Call repeatedly; returns the next Action due (None while waiting). Advances the state.
  Action next(uint32_t nowMs);

  State state() const { return state_; }
  bool busy() const { return state_ != State::On; }
  bool powered() const { return state_ == State::On || state_ == State::Settling || state_ == State::Releasing; }
  // ms until a switch-on is allowed (0 = now, or not off).
  uint32_t onAllowedInMs(uint32_t nowMs) const;

 private:
  State state_ = State::On;
  bool cycle_ = false;         // remount after switching on
  bool wantOn_ = false;
  uint32_t cutMs_ = 0, onMs_ = 0;
};

}  // namespace wineador
