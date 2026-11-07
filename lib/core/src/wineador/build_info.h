#pragma once
// Which build this is: profile (real or bench), test build or cabinet build,
// and the version string (spec B3: "0.1.0-stage<n>", plus -BENCH and -TEST).
//
// The profile and test switches are compile-time, set in platformio.ini:
//   -DWINEADOR_BENCH_PROFILE   bench timers (default: real)
//   -DWINEADOR_TEST_BUILD      scripted readings + cabinet model (default: cabinet build)
// With neither flag you get the real-profile cabinet build — the only one for the cabinet.

#include "wineador/types.h"

namespace wineador {

constexpr uint8_t kFirmwareStage = 1;

#if defined(WINEADOR_BENCH_PROFILE)
constexpr Profile kBuildProfile = Profile::Bench;
#else
constexpr Profile kBuildProfile = Profile::Real;
#endif

#if defined(WINEADOR_TEST_BUILD)
constexpr bool kTestBuild = true;
#else
constexpr bool kTestBuild = false;
#endif

const char* profileName(Profile p);   // "REAL" or "BENCH"

// Writes e.g. "0.1.0-stage1", "0.1.0-stage1-BENCH-TEST". Returns out.
const char* makeVersionString(char* out, size_t n, uint8_t stage, Profile p, bool testBuild);

}  // namespace wineador
