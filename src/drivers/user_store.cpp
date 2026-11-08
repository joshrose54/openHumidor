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
  wineador::setTargetTemp(v, p.getFloat("temp", u.targetTempC));        // out of bounds: rejected, default kept
  wineador::setRhSetpoint(v, p.getFloat("rh", u.rhSetpointPct));
  v.heaterEnable = p.getBool("heater", u.heaterEnable);
  v.humidifierEnable = p.getBool("humid", u.humidifierEnable);
  v.manualExcludeMask = p.getUChar("excl", 0) & 0x3F;                    // six sensors
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
  ok = (p.putUChar("excl", u.manualExcludeMask) == 1) && ok;
  p.end();
  return ok;
}

}  // namespace userstore
