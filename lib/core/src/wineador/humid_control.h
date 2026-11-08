#pragma once
// Humidifier control (spec 4.7b, 5.6) — stage 5. Plain logic.
//
// It only ASKS for the humidifier; the runtime guards (4.4) decide whether it
// runs: minimum off, maximum run and rest, the 70 % ceiling, the sensor
// requirement and its recovery hold, never during stripping, not during a
// temperature-driven compressor run unless that setting is on, user disable.
//
//   Call      Starts when the mean healthy-cabinet RH is at or below setpoint
//             - band (62 - 2 = 60 % at the defaults) for the whole persistence
//             time (10 min; bench 30 s). Any break restarts the count. The count
//             runs only while no strip is active, from zero when a strip ends.
//   Release   Mean RH at or above setpoint. No minimum on: off at once.
//             A call stays on until then — the guards only pause the humidifier,
//             and it resumes when they clear, with no fresh count.
//   Readings  The call is judged only on fresh readings from at least the
//             required number of healthy cabinet sensors (3 of 4); otherwise it
//             is left as it is (the sensor guard pauses the humidifier).
//   Ineffective (5.6)  Once per run, from the mean cabinet VAPOUR PRESSURE at
//             start: it must rise by the equivalent of 0.5 RH points at setpoint
//             conditions (~0.012 kPa at 62 %/20 C) within 20 min (bench 1 min).
//             Else humidifier off, flagged, held off for 2 h (bench 2 min), then
//             tried again. A run ending inside the window gives no result; a
//             flag clears on a later passing window.
//
// Events (call start / end, ineffective / effective) are queued for main to print.

#include "wineador/types.h"
#include "wineador/settings.h"
#include "wineador/summary.h"

namespace wineador {

struct HumidInputs {
  uint32_t nowMs = 0;
  CabinetSummary cab;          // healthy sensors only, corrected RH
  bool readingsFresh = false;
  LoadOutputs live;            // what is live right now (last pass's result)
  bool stripping = false;      // RH stripping active (TemperatureControl)
  bool scriptedDemand = false; // TEST BUILDS: a stage 2-3 scenario asks for loads directly
};

struct HumidOutput {
  bool demand = false;
  bool ineffectiveHold = false;   // -> the guards: humidifier held off until the retry
};

struct HumidEvent {
  enum class Type : uint8_t { None, CallStart, CallEnd, Ineffective, Effective };
  Type type = Type::None;
  float rh = 0;          // mean RH % at the event
  float vpChange = 0;    // Ineffective / Effective: vapour pressure change over the run, kPa
  uint32_t count = 0;    // CallStart: calls since boot
};

int formatHumidEvent(char* out, size_t n, const HumidEvent& e, const Settings& s);

class HumidControl {
 public:
  void begin(const Settings& s, uint32_t nowMs);
  HumidOutput update(const HumidInputs& in);
  void setUser(const UserSettings& u) { s_.user = u; }

  bool call() const { return call_; }
  bool persisting() const { return persisting_; }
  uint32_t persistMs(uint32_t nowMs) const { return persisting_ ? elapsedMs(nowMs, persistSinceMs_) : 0; }
  uint32_t calls() const { return calls_; }
  uint32_t runs() const { return runs_; }
  bool ineffective() const { return flag_; }
  bool held() const { return hold_; }
  uint32_t retryLeftMs(uint32_t nowMs) const;
  float riseNeededKpa() const;         // the vapour pressure rise the check needs
  float triggerPct() const { return s_.user.rhSetpointPct - s_.tuning.humidifyBandPct; }

  bool takeEvent(HumidEvent& e);

 private:
  void push(const HumidEvent& e);

  Settings s_;
  bool call_ = false;
  bool persisting_ = false;
  uint32_t persistSinceMs_ = 0;
  uint32_t calls_ = 0, runs_ = 0;
  bool prevOn_ = false;
  bool armed_ = false;
  uint32_t armedAtMs_ = 0;
  float startVp_ = 0;
  bool flag_ = false;
  bool hold_ = false;
  uint32_t holdSinceMs_ = 0;

  static constexpr int kEvents = 8;
  HumidEvent events_[kEvents];
  int evHead_ = 0, evCount_ = 0;
};

// The humidifier part of the status line, e.g.
// "humid CALL | calls 2 runs 3"  or  "humid idle persist 12s | calls 0 runs 0 | HUMID INEFFECTIVE (retry 95s)"
int formatHumidText(char* out, size_t n, const HumidControl& hc, uint32_t nowMs);

}  // namespace wineador
