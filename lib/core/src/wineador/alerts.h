#pragma once
// The LCD's alerts (spec 5.5): what is wrong now, most serious first, each with when it
// started. Plain logic. Fed each pass with the state the firmware already has; an alert leaves
// the list by itself once its condition clears. Nothing is kept across a reboot: the list is
// rebuilt from the current state (so an alert still active after a reboot starts again then).
//
// Most serious first (5.5), oldest first within the same rank:
//   0 Fault (5.5's definition): the 4.3 sensor requirement not met (including its recovery
//     hold), the plenum fan stalled (until loads resume), or the settings failing their boot
//     check. One alert per cause, each naming it.
//   1 a 12V dip in progress (Rev Q)
//   2 heater, cooling or humidifier ineffective (one alert each)
//   3 a sensor excluded, automatic or manual (one alert per sensor, named by position)
//   4 SD card failure
//   5 the clock lost or not yet set
// WiFi is never an alert (its icon only).

#include <stdint.h>
#include <stddef.h>
#include "wineador/types.h"

namespace wineador {

enum class SensorFault : uint8_t {
  None,
  Chamber,       // the chamber sensor is not healthy
  Cabinet,       // fewer than 3 cabinet sensors healthy
  RecoveryHold,  // the requirement is met again; the recovery hold is running
};

struct AlertInputs {
  bool sensorsKnown = false;        // the first sweep has finished (before it, nothing is judged)
  SensorFault sensorFault = SensorFault::None;
  bool fanStalled = false;
  bool settingsBad = false;
  bool dip12v = false;
  bool heaterIneffective = false;
  bool coolingIneffective = false;
  bool humidIneffective = false;
  uint8_t excludedMask = 0;         // bit i: sensor i not healthy
  uint8_t manualMask = 0;           // bit i: that exclusion is the manual override
  bool sdFailed = false;
  bool clockBad = false;            // timestamps not trusted (5.5's definition)
};

// One slot per possible alert.
enum class AlertSlot : uint8_t {
  FaultSensors, FaultFan, FaultSettings,
  Dip12v,
  HeaterIneffective, CoolingIneffective, HumidIneffective,
  Sensor0, Sensor1, Sensor2, Sensor3, Sensor4, Sensor5,
  SdFailed,
  ClockBad,
  Count
};
constexpr int kAlertSlots = (int)AlertSlot::Count;

int alertRank(AlertSlot s);          // 0 = most serious

struct Alert {
  AlertSlot slot = AlertSlot::Count;
  uint8_t rank = 0;
  uint64_t sinceMs = 0;              // uptime when it started
  char title[44] = "";               // e.g. "Fault: plenum fan stalled"
};

class AlertTracker {
 public:
  // Call every pass (or every few hundred ms). uptimeMs never wraps (64-bit).
  void update(const AlertInputs& in, uint64_t uptimeMs);

  int count() const { return n_; }
  // Sorted: most serious first, oldest first within a rank.
  const Alert& at(int i) const { return sorted_[i]; }
  bool any() const { return n_ > 0; }

 private:
  bool active_[kAlertSlots] = {};
  uint64_t since_[kAlertSlots] = {};
  Alert sorted_[kAlertSlots];
  int n_ = 0;
};

// Fault's sensor cause (5.5) from the health now and the 4.3 gate: the requirement not met by the
// health as it is NOW (so a sweep that has just made it good never shows a fault for the pass
// before the gate sees it), or met with the recovery hold still running.
SensorFault sensorFaultFrom(bool chamberHealthy, int cabinetHealthy, int minCabinet, bool recoveryHolding);

// The clock for the LCD (5.5): bad = never set, or timestamps untrusted (the DS3231 lost time and
// NTP hasn't corrected it), or the RTC unreadable with no NTP sync since boot.
struct ClockFacts {
  bool firstSweepDone = false;   // the RTC has been read once (the boot sweep)
  bool clockSet = false;         // from the RTC or NTP
  bool trusted = false;          // ClockState::trusted()
  bool rtcOk = false;
  bool ntpSynced = false;
};
bool clockBad(const ClockFacts& c);
// The alert only once the boot sweep has read the RTC (before it, "--:--" but no alert).
inline bool clockAlert(const ClockFacts& c) { return c.firstSweepDone && clockBad(c); }

// The title text for a slot as it is now.
void alertTitle(char* out, size_t n, AlertSlot s, const AlertInputs& in);

// When an alert started (5.5): "14:32" if it started today (UK local), "13 Nov 14:32" otherwise,
// or "for 12 min" while the clock is not trusted. clockOk + nowUtcS give the time now.
void formatAlertWhen(char* out, size_t n, uint64_t sinceMs, uint64_t nowMs, bool clockOk, int64_t nowUtcS);

// How long, for "for 12 min": "for <1 min", "for 12 min", "for 3 h 5 min", "for 2 d 4 h".
void formatFor(char* out, size_t n, uint64_t ms);

}  // namespace wineador
