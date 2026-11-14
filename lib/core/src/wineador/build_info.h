#pragma once
// Which build this is: profile (real or bench), test build or cabinet build,
// and the version string (spec B3: the board's version, plus -BENCH and -TEST).
//
// The profile and test switches are compile-time, set in platformio.ini:
//   -DWINEADOR_BENCH_PROFILE   bench timers (default: real)
//   -DWINEADOR_TEST_BUILD      scripted readings + cabinet model (default: cabinet build)
//   -DWINEADOR_BOOT_DEMAND     test builds only: heat + humidify asked for from boot
// With neither flag you get the real-profile cabinet build — the only one for the cabinet.

#include "wineador/types.h"

namespace wineador {

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

// B7 test 6 ONLY (spec): the rollback test image, its own build (env revq_rollback_test), never
// the default. A real-profile cabinet identity (so the OTA check accepts it), version
// "2.0.0-ROLLBACK-TEST"; it holds every load off and resets itself before it can be marked good.
#if defined(WINEADOR_ROLLBACK_TEST)
#if defined(WINEADOR_TEST_BUILD) || defined(WINEADOR_BENCH_PROFILE)
#error "WINEADOR_ROLLBACK_TEST is a Rev Q real-profile build only (env revq_rollback_test)"
#endif
constexpr bool kRollbackTest = true;
#else
constexpr bool kRollbackTest = false;
#endif

const char* profileName(Profile p);   // "REAL" or "BENCH"

// The version (spec B3 "Version string"): the board's base version (include/board.h: "1.0.0" on
// Rev N, "2.0.0" on Rev Q) with -BENCH and -TEST added when they apply, e.g. "2.0.0-BENCH-TEST".
// Returns out.
// The rollback test image adds -ROLLBACK-TEST.
const char* makeVersionString(char* out, size_t n, const char* base, Profile p, bool testBuild, bool rollbackTest = false);

}  // namespace wineador
