#include "user_store.h"
#include <Preferences.h>
#include "wineador/build_info.h"

namespace userstore {

using wineador::UserSettings;

const char* nvsNamespace() {
  if (wineador::kTestBuild) return "usr_test";
  return wineador::kBuildProfile == wineador::Profile::Bench ? "usr_bench" : "usr_cab";
}

const char* load(UserSettings& u) {
  Preferences p;
  if (!p.begin(nvsNamespace(), true)) return "none saved (defaults)";   // read-only; fails if never written
  if (!p.isKey("temp")) { p.end(); return "none saved (defaults)"; }
  UserSettings v = u;
  // Out of bounds: rejected, default kept. In bounds but off its step: kept, then rounded at boot (5.5).
  wineador::loadTargetTemp(v, p.getFloat("temp", u.targetTempC));
  wineador::loadRhSetpoint(v, p.getFloat("rh", u.rhSetpointPct));
  v.heaterEnable = p.getBool("heater", u.heaterEnable);
  v.humidifierEnable = p.getBool("humid", u.humidifierEnable);
  v.ionEnable = p.getBool("ion", u.ionEnable);        // absent in NVS saved before it existed: the default, on
  v.manualExcludeMask = p.getUChar("excl", 0) & 0x3F;                    // six sensors
  // Rev Q, 4.6c: absent (saved before they existed) or not a valid pair: both keep their defaults
  const uint8_t runs = p.getUChar("ionruns", u.ionRunsPerDay), mins = p.getUChar("ionmin", u.ionMinutesPerRun);
  if (wineador::ionRunsOk(runs, mins)) { v.ionRunsPerDay = runs; v.ionMinutesPerRun = mins; }
  p.end();
  u = v;
  return "saved settings loaded";
}

bool save(const UserSettings& u) {
  Preferences p;
  if (!p.begin(nvsNamespace(), false)) return false;
  bool ok = p.putFloat("temp", u.targetTempC) == sizeof(float);
  ok = (p.putFloat("rh", u.rhSetpointPct) == sizeof(float)) && ok;
  ok = (p.putBool("heater", u.heaterEnable) == 1) && ok;
  ok = (p.putBool("humid", u.humidifierEnable) == 1) && ok;
  ok = (p.putBool("ion", u.ionEnable) == 1) && ok;
  ok = (p.putUChar("excl", u.manualExcludeMask) == 1) && ok;
  ok = (p.putUChar("ionruns", u.ionRunsPerDay) == 1) && ok;
  ok = (p.putUChar("ionmin", u.ionMinutesPerRun) == 1) && ok;
  p.end();
  return ok;
}

}  // namespace userstore
