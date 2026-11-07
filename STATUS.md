# Where we're up to

*Updated 2026-11-07.*

## Stage 1 — passed (Josh, 2026-11-07)

Tests 11 and 14 run and seen passing. Test 11 is logged at stage 2 (needs a load held off),
14 at stage 6. Spec findings went to the spec chat; its updates are committed (`3d87c1c`, `f2b2e9a`).

## Stage 2 — built; board tests next

Runtime guards, interlock switching sequence, relay exercise. **Code not committed yet.**

- **Mac side:** five ESP32 builds compile with no warnings; native tests **97/97** pass (55 new).
- **Cabinet and `bench` builds:** no control logic yet, so nothing asks a load to run — all off.
- **Test builds:** `run 1`, `run 2`, `run 3`, `run 13` ask for loads; the guards decide.
- **`bench_test_boot`:** heat + humidify asked for from boot (tests 11 and 12).

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
- **`run 1`** freezes the cabinet model so the real profile's 60 min heater run doesn't hit the
  cabinet ceiling. **`run 2`** kicks the fan every 10 s after J2 is replugged (stage 3 does this
  by itself).

### Board tests (one at a time, in this order)

| Order | B3 test | Build | Wiring | Result |
|---|---|---|---|---|
| 1 | 13 relay exercise | `bench_test` | bulb (KSD in series) J10 3–4, humid fan J11 | — |
| 2 | 3 interlock | `bench_test` | same; meter J10 1 and 3 | — |
| 3 | 1 guard timing | `bench_test` | same | — |
| 4 | 2 heater guards (logged at stage 3, with the sensor guard) | `bench_test` | same; unplug J2 when told | — |
| 5 | 11 watchdog, load held off | `bench_test_boot` | same | — |
| 6 | 12 power interruption | `bench_test_boot` | same; **USB unplugged first** | — |

Test 13 is checked on serial at stage 2 and logged at stage 6. Test 2 is logged at stage 3, once
the sensor guard can be tripped on the board; same for any test needing a sensor fault.

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
