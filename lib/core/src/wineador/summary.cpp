#include "wineador/summary.h"

namespace wineador {

CabinetSummary summarise(const Readings& r) {
  CabinetSummary c;
  float sumC = 0, sumRh = 0;
  for (int i = 0; i < kNumSensors; i++) {
    const SensorReading& s = r.s[i];
    if (s.bus != BusResult::Ok) continue;
    c.sensorsOk++;
    if (i < kCabinetSensors) {
      if (c.cabinetOk == 0) { c.minC = c.maxC = s.tempC; }
      if (s.tempC < c.minC) c.minC = s.tempC;
      if (s.tempC > c.maxC) c.maxC = s.tempC;
      sumC += s.tempC;
      sumRh += s.rhRawPct;
      c.cabinetOk++;
    } else if (i == kChamberIndex) {
      c.chamberOk = true;
      c.chamberC = s.tempC;
    } else {
      c.ambientOk = true;
      c.ambientC = s.tempC;
      c.ambientRhPct = s.rhRawPct;
    }
  }
  if (c.cabinetOk > 0) {
    c.meanC = sumC / c.cabinetOk;
    c.meanRhPct = sumRh / c.cabinetOk;
  }
  return c;
}

}  // namespace wineador
