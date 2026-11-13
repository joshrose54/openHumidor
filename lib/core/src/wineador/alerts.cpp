#include "wineador/alerts.h"
#include <stdio.h>
#include "wineador/sensor_table.h"
#include "wineador/uk_time.h"

namespace wineador {

int alertRank(AlertSlot s) {
  switch (s) {
    case AlertSlot::FaultSensors: case AlertSlot::FaultFan: case AlertSlot::FaultSettings: return 0;
    case AlertSlot::Dip12v: return 1;
    case AlertSlot::HeaterIneffective: case AlertSlot::CoolingIneffective: case AlertSlot::HumidIneffective: return 2;
    case AlertSlot::SdFailed: return 4;
    case AlertSlot::ClockBad: return 5;
    default: return 3;   // the six sensors
  }
}

static bool isActive(AlertSlot s, const AlertInputs& in) {
  const int i = (int)s - (int)AlertSlot::Sensor0;
  switch (s) {
    case AlertSlot::FaultSensors: return in.sensorsKnown && in.sensorFault != SensorFault::None;
    case AlertSlot::FaultFan: return in.fanStalled;
    case AlertSlot::FaultSettings: return in.settingsBad;
    case AlertSlot::Dip12v: return in.dip12v;
    case AlertSlot::HeaterIneffective: return in.heaterIneffective;
    case AlertSlot::CoolingIneffective: return in.coolingIneffective;
    case AlertSlot::HumidIneffective: return in.humidIneffective;
    case AlertSlot::SdFailed: return in.sdFailed;
    case AlertSlot::ClockBad: return in.clockBad;
    default: return in.sensorsKnown && i >= 0 && i < kNumSensors && (in.excludedMask & (1u << i));
  }
}

void alertTitle(char* out, size_t n, AlertSlot s, const AlertInputs& in) {
  const int i = (int)s - (int)AlertSlot::Sensor0;
  switch (s) {
    case AlertSlot::FaultSensors:
      snprintf(out, n, "%s", in.sensorFault == SensorFault::Chamber   ? "Fault: chamber sensor"
                             : in.sensorFault == SensorFault::Cabinet ? "Fault: cabinet sensors"
                                                                      : "Fault: sensor recovery");
      break;
    case AlertSlot::FaultFan: snprintf(out, n, "Fault: plenum fan stall"); break;
    case AlertSlot::FaultSettings: snprintf(out, n, "Fault: settings check"); break;
    case AlertSlot::Dip12v: snprintf(out, n, "12V supply dip"); break;
    case AlertSlot::HeaterIneffective: snprintf(out, n, "Heater ineffective"); break;
    case AlertSlot::CoolingIneffective: snprintf(out, n, "Cooling ineffective"); break;
    case AlertSlot::HumidIneffective: snprintf(out, n, "Humidifier ineffective"); break;
    case AlertSlot::SdFailed: snprintf(out, n, "SD card failure"); break;
    case AlertSlot::ClockBad: snprintf(out, n, "Clock lost or not set"); break;
    default:
      if (i >= 0 && i < kNumSensors)
        snprintf(out, n, "Excluded: %s%s", kSensors[i].position, (in.manualMask & (1u << i)) ? " (manual)" : "");
      else snprintf(out, n, "?");
      break;
  }
}

SensorFault sensorFaultFrom(bool chamberHealthy, int cabinetHealthy, int minCabinet, bool recoveryHolding) {
  if (!chamberHealthy) return SensorFault::Chamber;
  if (cabinetHealthy < minCabinet) return SensorFault::Cabinet;
  return recoveryHolding ? SensorFault::RecoveryHold : SensorFault::None;
}

bool clockBad(const ClockFacts& c) { return !c.clockSet || !c.trusted || (!c.rtcOk && !c.ntpSynced); }

void AlertTracker::update(const AlertInputs& in, uint64_t now) {
  n_ = 0;
  for (int k = 0; k < kAlertSlots; k++) {
    const AlertSlot s = (AlertSlot)k;
    const bool on = isActive(s, in);
    if (on && !active_[k]) since_[k] = now;
    active_[k] = on;
    if (!on) continue;
    Alert& a = sorted_[n_++];
    a.slot = s;
    a.rank = (uint8_t)alertRank(s);
    a.sinceMs = since_[k];
    alertTitle(a.title, sizeof(a.title), s, in);
  }
  // Insertion sort: rank, then oldest first, then slot order (stable).
  for (int i = 1; i < n_; i++) {
    const Alert a = sorted_[i];
    int j = i - 1;
    while (j >= 0 && (sorted_[j].rank > a.rank || (sorted_[j].rank == a.rank && sorted_[j].sinceMs > a.sinceMs))) {
      sorted_[j + 1] = sorted_[j];
      j--;
    }
    sorted_[j + 1] = a;
  }
}

void formatFor(char* out, size_t n, uint64_t ms) {
  const uint64_t min = ms / 60000;
  if (min < 1) snprintf(out, n, "for <1 min");
  else if (min < 60) snprintf(out, n, "for %u min", (unsigned)min);
  else if (min < 24 * 60) snprintf(out, n, "for %u h %u min", (unsigned)(min / 60), (unsigned)(min % 60));
  else snprintf(out, n, "for %u d %u h", (unsigned)(min / 1440), (unsigned)(min / 60 % 24));
}

void formatAlertWhen(char* out, size_t n, uint64_t sinceMs, uint64_t nowMs, bool clockOk, int64_t nowUtcS) {
  const uint64_t ago = nowMs >= sinceMs ? nowMs - sinceMs : 0;
  if (!clockOk) { formatFor(out, n, ago); return; }
  static const char* const kMon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  const LocalTime now = ukLocal(nowUtcS);
  const LocalTime at = ukLocal(nowUtcS - (int64_t)(ago / 1000));
  if (at.year == now.year && at.month == now.month && at.day == now.day) snprintf(out, n, "%02d:%02d", at.hour, at.minute);
  else snprintf(out, n, "%d %s %02d:%02d", at.day, kMon[(at.month - 1) % 12], at.hour, at.minute);
}

}  // namespace wineador
