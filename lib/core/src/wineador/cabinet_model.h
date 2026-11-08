#pragma once
// TEST BUILDS ONLY — the simple cabinet model (spec B3, "Scripted readings
// respond to the loads"). Compiled out of the cabinet build entirely: without
// WINEADOR_TEST_BUILD this header declares nothing and cabinet_model.cpp is
// empty, so any use of the model in a cabinet build fails to compile.
//
// Heating raises the scripted cabinet temperature, cooling lowers it,
// humidifying adds moisture; and all the time the cabinet drifts towards the room
// value (heat leaking in or out), so a strong drift can outrun a load (the B4 soak
// uses that to reach the maximum runs). Each response can be switched off to test the ineffective checks.
//
// Chamber (stage 4): it sits by the plate and heater, so it swings much faster
// than the cabinet: during a run it heads for cabinet +/- chamberSwingC with a
// time constant of chamberTauS; afterwards it heads back to the cabinet value
// (that return is what ends a harvest).
//
// Moisture (stage 4): the state is the cabinet's VAPOUR PRESSURE, so RH rises as
// the air cools and falls as it warms with no water moved — as the real cabinet
// (4.7). The compressor removes water at coolVpKpaPerMin; the humidifier adds it.
//
// The model honours the K1 interlock like the real board: cooling only acts
// when the relay is on the cooling side, heating only on the heating side.

#if defined(WINEADOR_TEST_BUILD)

#include "wineador/types.h"

namespace wineador {

// Default rates. Cooling and drift follow Track A (A2), room 20 C to match;
// heating and humidifying are chosen to pass the ineffective checks clearly
// (heater: 1.5 C in 15 min vs 0.5 needed; humidifier: 2 points in 20 min vs 0.5).
// Chamber rates equal the cabinet's until stage 4 sets them.
struct ModelRates {
  float coolCPerMin      = -0.1f;
  float heatCPerMin      = +0.1f;
  float humidPctPerMin   = +0.1f;
  float driftCPerMin     = 0.006f;   // magnitude, towards the room value
  float roomC            = 20.0f;
  float chamberSwingC    = 6.0f;     // chamber heads for cabinet -/+ this during a cooling / heating run
  float chamberTauS      = 30.0f;    // ...and back to the cabinet value afterwards, this time constant
  float coolVpKpaPerMin  = 0.0f;     // water the compressor takes out (a stripping scenario sets it)
  bool  coolResponds     = true;
  bool  heatResponds     = true;
  bool  humidResponds    = true;
};

class CabinetModel {
 public:
  void begin(const ModelRates& rates, float startC = 20.0f, float startRhPct = 62.0f,
             float ambientC = 20.0f, float ambientRhPct = 50.0f);

  // Advance the model by dtMs with the loads as they are now.
  void step(uint32_t dtMs, const LoadOutputs& loads);

  // Scripted value for a sensor index 0..5 (includes its offset).
  float tempC(int index) const;
  float rhPct(int index) const;

  // Scenarios script the cabinet directly (the chamber still follows the loads).
  void setCabinetC(float c) { cabinetC_ = c; }
  float cabinetC() const { return cabinetC_; }
  void setCabinetRh(float rhPct);        // sets the vapour pressure that gives this RH now
  float cabinetRh() const;
  float cabinetVpKpa() const { return cabinetVp_; }

  // Per-sensor offset, so a scenario can push one sensor away from the rest.
  void setOffset(int index, float dC, float dRhPct);

  ModelRates& rates() { return rates_; }

 private:
  ModelRates rates_;
  float cabinetC_ = 20.0f, chamberC_ = 20.0f, cabinetVp_ = 1.45f;
  float ambientC_ = 20.0f, ambientRh_ = 50.0f;
  float offC_[kNumSensors] = {};
  float offRh_[kNumSensors] = {};
};

// Replace the values of every sensor whose bus read SUCCEEDED with the scripted
// values. Failed bus reads stay failed — the bus checks remain real (spec B3).
// RH: the model's value is corrected RH; the reading gets the raw value that
// calibrates back to it.
void applyScripted(Readings& r, const CabinetModel& m);

}  // namespace wineador

#endif  // WINEADOR_TEST_BUILD
