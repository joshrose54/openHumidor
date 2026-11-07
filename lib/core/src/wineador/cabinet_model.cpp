// TEST BUILDS ONLY — see cabinet_model.h. In the cabinet build this file is empty.
#if defined(WINEADOR_TEST_BUILD)

#include "wineador/cabinet_model.h"

namespace wineador {

void CabinetModel::begin(const ModelRates& rates, float startC, float startRhPct,
                         float ambientC, float ambientRhPct) {
  rates_ = rates;
  cabinetC_ = chamberC_ = startC;
  cabinetRh_ = startRhPct;
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

  if (cooling) {
    cabinetC_ += rates_.coolCPerMin * min;
    chamberC_ += rates_.coolCPerMin * min;
  } else if (heating) {
    cabinetC_ += rates_.heatCPerMin * min;
    chamberC_ += rates_.heatCPerMin * min;
  } else {
    cabinetC_ = towards(cabinetC_, rates_.roomC, rates_.driftCPerMin * min);
    chamberC_ = towards(chamberC_, rates_.roomC, rates_.driftCPerMin * min);
  }

  if (loads.humid && rates_.humidResponds) {
    cabinetRh_ += rates_.humidPctPerMin * min;
    if (cabinetRh_ > 100.0f) cabinetRh_ = 100.0f;
  }
}

float CabinetModel::tempC(int i) const {
  if (i < 0 || i >= kNumSensors) return 0.0f;
  float base = (i < kCabinetSensors) ? cabinetC_ : (i == kChamberIndex) ? chamberC_ : ambientC_;
  return base + offC_[i];
}

float CabinetModel::rhPct(int i) const {
  if (i < 0 || i >= kNumSensors) return 0.0f;
  float base = (i == kAmbientIndex) ? ambientRh_ : cabinetRh_;
  return base + offRh_[i];
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
    r.s[i].rhRawPct = m.rhPct(i);
  }
}

}  // namespace wineador

#endif  // WINEADOR_TEST_BUILD
