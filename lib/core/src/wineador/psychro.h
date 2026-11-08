#pragma once
// Vapour pressure (spec 4.7). RH stripping triggers on RH but releases on
// vapour pressure, because cooling air raises its RH even when no water leaves;
// vapour pressure only moves when water enters or leaves.
//
// Saturation vapour pressure by the Magnus formula (Alduchov & Eskridge
// coefficients, good to about 0.1 % between -40 and +50 C):
//   es(T) = 0.61094 * exp(17.625 * T / (T + 243.04))   kPa
// Vapour pressure = es(T) * RH / 100.  62 % at 20 C -> 1.447 kPa (the spec's 1.45).

#include <math.h>

namespace wineador {

inline float saturationVpKpa(float tC) {
  return 0.61094f * expf(17.625f * tC / (tC + 243.04f));
}

inline float vapourPressureKpa(float tC, float rhPct) {
  return saturationVpKpa(tC) * rhPct / 100.0f;
}

// The RH that a vapour pressure gives at temperature tC (used by the test-build model).
inline float rhFromVp(float tC, float vpKpa) {
  return 100.0f * vpKpa / saturationVpKpa(tC);
}

}  // namespace wineador
