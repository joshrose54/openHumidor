# Where we're up to

*Updated 2026-11-07.*

## Stage 1 — passed (Josh, 2026-11-07)

Tests 11 and 14 run and seen passing. Test 11 is logged at stage 2 (needs a load held off),
14 at stage 6. Spec findings went to the spec chat; its updates are committed (`3d87c1c`, `f2b2e9a`).

## Stage 2 — board tests done except test 12 (hardware fault); waiting on the spec chat

Runtime guards, interlock switching sequence, relay exercise. Committed (`019a368` + the fan readback).

- **Mac side:** five ESP32 builds compile with no warnings; native tests **100/100** pass.
- **Cabinet and `bench` builds:** no control logic yet, so nothing asks a load to run — all off.
- **Test builds:** `run 1`, `run 2`, `run 3`, `run 13` ask for loads; the guards decide.
- **`bench_test_boot`:** heat + humidify asked for from boot (tests 11 and 12).

### Board results (2026-11-07)

| B3 test | Build | Result | Logged at |
|---|---|---|---|
| 13 relay exercise | `bench_test` | Passed: 3 x 1.0 s energised / 1.0 s released, loads off throughout, ended cooling; 6 clicks, bulb dark | Stage 6 |
| 3 interlock | `bench_test` | Passed: cooling J10 pin 1 12 V / pin 3 0 V, bulb dark; heating pin 1 0 V / pin 3 12 V, bulb lit; checked every pass | Stage 2 |
| 1 guard timing | `bench_test` | Passed twice: min on / min off / max run exact for all three loads; crossover 30 s | Stage 2 |
| 2 heater guards | `bench_test` | Passed: off next pass for cabinet ceiling, chamber ceiling, fan (J2 pulled) | Stage 3 (with the sensor guard) |
| 11 watchdog | `bench_test_boot` | Passed: `stall` -> `task_wdt` ~5 s, `TASK_WATCHDOG`, heat + humid held 30 s then on | Stage 2 |
| 12 power interruption | `bench_test_boot` | **Failed — hardware: the ESP32 does not run without USB** (EN held low by the DevKit) | After the fix |

### Found on the board

- **Rev N does not run without USB.** USB out: EN 0 V (cold start) or 2.37 V (unplugged while
  running) — the ESP32 held in reset, loads off, fan full speed. USB in: EN 3.3 V, runs. Cause
  inferred: the DevKit's auto-reset circuit. Spec finding; suggested bench fix 4.7 kOhm EN to 3V3.
- **Plenum fan ran at full speed ~2 min** with 20% commanded (first `run 1`). Not repeated in a
  wiggle test or a second `run 1`. Josh: a bad connection. The status line now shows the PWM
  hardware readback (`pwm-hw 51/255`) and tach edges per pulse, so a repeat can be told apart.
- Uncalibrated room RH read 68-71% raw; the 70% humidifier ceiling uses raw RH until stage 3.

### What was added (lib/core unless noted)

| File | What |
|---|---|
| `guards.h/.cpp` | Runtime guards (4.4): stop guards (settings, boot hold, exercise, sensors, fan, ceilings, max run, stripping, compressor run, disabled) and start guards (min off, crossover both ways, other load, exercise waiting); min on only against a dropped demand |
| `interlock.h/.cpp` | Switching sequence (3.2): drop load → move K1 → 20 ms settle (a state) → assert. K1 rests on the cooling side |
| `relay_exercise.h/.cpp` | Weekly, or `exercise`: waits for compressor and heater off, 3 × (1 s energised, 1 s released), ends cooling |
| `controller.h/.cpp` | The minimum job each pass: boot hold → exercise → guards → sequence. Guard text for the status line |
| `scenario.h/.cpp` | Test builds only: `run 1/2/3/13`, `stop`; prints PASS/FAIL itself |
| `src/drivers/outputs` | `apply()`: loads going off → K1 → loads going on |
| `src/main.cpp` | Minimum job first in every pass; status line shows demand and why a load is held |

### Decisions taken in building (worth knowing)

- **Stage 2 "healthy sensor"** = its last bus read was OK, and the readings are under 30 s old.
  The 4.3 health rules replace this at stage 3.
- **Both asked for at once:** cooling goes first (the safe, de-energised side); heat waits.
- **At boot every load counts as just stopped** (spec 4.4, updated): boot hold, min off and
  crossover all run from boot. Real profile: compressor and heater first allowed at 10 min, humidifier at 5.
- **Exercise waiting** lets a running compressor/heater finish by its own rules (at most its
  max run); it only stops new starts.
- **`stall`** now leaves the outputs as they are (a real lock-up); the watchdog reset turns them off.
- **Run 2 at stage 3:** keep the heater on 5 s before each ceiling trip, so the bulb is seen to
  go out (this time it tripped the instant the heater started).
- **`run 1`** freezes the cabinet model so the real profile's 60 min heater run doesn't hit the
  cabinet ceiling. **`run 2`** kicks the fan every 10 s after J2 is replugged (stage 3 does this
  by itself).

### Next

1. Spec chat: test 12 / USB finding and the fix (4.7 kOhm EN pull-up to try on the bench, or other).
2. After the fix: repeat the USB-out check (EN ~3.3 V, loads keep running), then test 12.
3. Josh calls stage 2 passed -> stage 3.

## Talking to the board

- `pio` is not on the shell path: use `~/.platformio/penv/bin/pio`.
- Plugging USB in restarts the board. Opening `pio device monitor` does not.
- My own serial script (`$CLAUDE_JOB_DIR/tmp/noreset.py`) clears RTS before DTR.

## Builds

| Build | Profile | Readings | Use |
|---|---|---|---|
| `esp32dev` | real | real | **The only build for the cabinet** |
| `bench` | bench | real | B3 physical tests |
| `bench_test` | bench | scripted | B3 scenario tests |
| `bench_test_boot` | bench | scripted | B3 tests 11, 12: heat + humidify from boot |
| `real_test` | real | scripted | B4 soak |
| `native` | — | — | Mac tests: `pio test -e native` |

Build output goes to `.pio.nosync/` so iCloud Drive doesn't copy it.
