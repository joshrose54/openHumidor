#include "ion_store.h"
#include "wineador/build_info.h"

#if defined(WINEADOR_BOARD_REVQ)
#include <Preferences.h>

namespace ionstore {

const char* nvsNamespace() {
  if (wineador::kTestBuild) return "ion_test";
  return wineador::kBuildProfile == wineador::Profile::Bench ? "ion_bench" : "ion_cab";
}

size_t load(uint8_t* out, size_t n) {
  Preferences p;
  if (!p.begin(nvsNamespace(), true)) return 0;   // read-only; fails if never written
  size_t got = 0;
  if (p.isKey("runs")) got = p.getBytes("runs", out, n);
  p.end();
  return got;
}

bool save(const uint8_t* blob, size_t n) {
  Preferences p;
  if (!p.begin(nvsNamespace(), false)) return false;
  const bool ok = p.putBytes("runs", blob, n) == n;
  p.end();
  return ok;
}

}  // namespace ionstore

#else   // Rev N: no ion generator

namespace ionstore {
const char* nvsNamespace() { return ""; }
size_t load(uint8_t*, size_t) { return 0; }
bool save(const uint8_t*, size_t) { return false; }
}  // namespace ionstore

#endif
