#pragma once
// Which build this is: profile (real or bench), test build or cabinet build,
// and the version string (spec B3: "0.1.0-stage<n>", plus -BENCH and -TEST).
//
// The profile and test switches are compile-time, set in platformio.ini:
//   -DWINEADOR_BENCH_PROFILE   bench timers (default: real)
//   -DWINEADOR_TEST_BUILD      scripted readings + cabinet model (default: cabinet build)
//   -DWINEADOR_BOOT_DEMAND     test builds only: heat + humidify asked for from boot
// With neither flag you get the real-profile cabinet build — the only one for the cabinet.

#include "wineador/types.h"

namespace wineador {

constexpr uint8_t kFirmwareStage = 3;

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

// TEST BUILDS ONLY (B3 tests 11 and 12): heat and humidify are asked for from
// the moment the board powers up, so a power cut or watchdog reset can be
// tested with nothing typed. -DWINEADOR_BOOT_DEMAND, env bench_test_boot.
#if defined(WINEADOR_BOOT_DEMAND)
#if !defined(WINEADOR_TEST_BUILD)
#error "WINEADOR_BOOT_DEMAND is only allowed in a test build"
#endif
constexpr bool kBootDemand = true;
#else
constexpr bool kBootDemand = false;
#endif

const char* profileName(Profile p);   // "REAL" or "BENCH"

// Writes e.g. "0.1.0-stage1", "0.1.0-stage1-BENCH-TEST". Returns out.
const char* makeVersionString(char* out, size_t n, uint8_t stage, Profile p, bool testBuild);

}  // namespace wineador
