// TEST BUILDS ONLY — see cabinet_model.h. In the cabinet build this file is empty.
#if defined(WINEADOR_TEST_BUILD)

#include "wineador/cabinet_model.h"
#include "wineador/sensor_health.h"
#include "wineador/psychro.h"
#include <math.h>

namespace wineador {

void CabinetModel::begin(const ModelRates& rates, float startC, float startRhPct,
                         float ambientC, float ambientRhPct) {
  rates_ = rates;
  cabinetC_ = chamberC_ = startC;
  cabinetVp_ = vapourPressureKpa(startC, startRhPct);
  ambientC_ = ambientC;
  ambientRh_ = ambientRhPct;
  for (int i = 0; i < kNumSensors; i++) { offC_[i] = 0; offRh_[i] = 0; }
}

namespace {
// Move v towards target by at most maxStep (maxStep >= 0).
float towards(float v, float target, float maxStep) {
  if (v > target) return (v - maxStep < target) ? target : v - maxStep;
  if (v < target) return (v + maxStep > target) ? target : v + maxStep;
  return v;
}
}

void CabinetModel::step(uint32_t dtMs, const LoadOutputs& loads) {
  const float min = dtMs / 60000.0f;
  // K1 decides which load actually has a feed, as on the board (3.2).
  const bool cooling = loads.cool && !loads.relayHeating && rates_.coolResponds;
  const bool heating = loads.heat && loads.relayHeating && rates_.heatResponds;

  // Heat leaks in or out towards the room all the time; a running load adds its own rate on top.
  cabinetC_ = towards(cabinetC_, rates_.roomC, rates_.driftCPerMin * min);
  if (cooling) {
    cabinetC_ += rates_.coolCPerMin * min;
    cabinetVp_ -= rates_.coolVpKpaPerMin * min;
    if (cabinetVp_ < 0.0f) cabinetVp_ = 0.0f;
  } else if (heating) {
    cabinetC_ += rates_.heatCPerMin * min;
  }

  // The chamber chases cabinet -/+ swing during a run, the cabinet value otherwise.
  const float target = cabinetC_ + (cooling ? -rates_.chamberSwingC : heating ? rates_.chamberSwingC : 0.0f);
  const float k = rates_.chamberTauS > 0 ? 1.0f - expf(-(dtMs / 1000.0f) / rates_.chamberTauS) : 1.0f;
  chamberC_ += (target - chamberC_) * k;

  if (loads.humid && rates_.humidResponds) {
    // humidPctPerMin of RH at the present temperature, as vapour pressure
    cabinetVp_ += saturationVpKpa(cabinetC_) * rates_.humidPctPerMin / 100.0f * min;
    const float sat = saturationVpKpa(cabinetC_);
    if (cabinetVp_ > sat) cabinetVp_ = sat;
  }
}

void CabinetModel::setCabinetRh(float rhPct) { cabinetVp_ = vapourPressureKpa(cabinetC_, rhPct); }

float CabinetModel::cabinetRh() const {
  float rh = rhFromVp(cabinetC_, cabinetVp_);
  return rh > 100.0f ? 100.0f : rh;
}

float CabinetModel::tempC(int i) const {
  if (i < 0 || i >= kNumSensors) return 0.0f;
  float base = (i < kCabinetSensors) ? cabinetC_ : (i == kChamberIndex) ? chamberC_ : ambientC_;
  return base + offC_[i];
}

float CabinetModel::rhPct(int i) const {
  if (i < 0 || i >= kNumSensors) return 0.0f;
  if (i == kAmbientIndex) return ambientRh_ + offRh_[i];
  // A cabinet sensor scripted warmer or colder reads the RH the cabinet's vapour
  // pressure gives at ITS temperature. (The chamber keeps the cabinet value: its
  // RH is never used for control.)
  float rh = (i < kCabinetSensors) ? rhFromVp(cabinetC_ + offC_[i], cabinetVp_) : cabinetRh();
  if (rh > 100.0f) rh = 100.0f;
  return rh + offRh_[i];
}

void CabinetModel::setOffset(int i, float dC, float dRhPct) {
  if (i < 0 || i >= kNumSensors) return;
  offC_[i] = dC;
  offRh_[i] = dRhPct;
}

void applyScripted(Readings& r, const CabinetModel& m) {
  for (int i = 0; i < kNumSensors; i++) {
    if (r.s[i].bus != BusResult::Ok) continue;
    r.s[i].tempC = m.tempC(i);
    // The model works in true (corrected) RH; the reading carries the RAW value
    // that this sensor's calibration turns back into it (2.3).
    r.s[i].rhRawPct = rawForCorrectedRh(i, m.rhPct(i));
  }
}

}  // namespace wineador

#endif  // WINEADOR_TEST_BUILD
