#pragma once
// Cabinet aggregates from the four cabinet sensors, ch2-ch5 only (spec 4.2, 5.4).
// Stage 1-2: every sensor whose bus read succeeded counts as healthy. Health rules (4.3)
// and calibration arrive at stage 3.

#include "wineador/types.h"

namespace wineador {

struct CabinetSummary {
  int   sensorsOk  = 0;     // of all six
  int   cabinetOk  = 0;     // of ch2-5
  float meanC = 0, minC = 0, maxC = 0, meanRhPct = 0, maxRhPct = 0;
  bool  chamberOk  = false;
  float chamberC   = 0;
  bool  ambientOk  = false;
  float ambientC   = 0, ambientRhPct = 0;
};

CabinetSummary summarise(const Readings& r);

}  // namespace wineador
