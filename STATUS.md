# Where we're up to

*Updated 2026-11-08.*

## Stage 1 — passed (Josh, 2026-11-07)

Tests 11 and 14 run and seen passing. Test 11 is logged at stage 2 (needs a load held off),
14 at stage 6. Spec findings went to the spec chat; its updates are committed (`3d87c1c`, `f2b2e9a`).

## Stage 2 — passed (Josh, 2026-11-08)

Runtime guards, interlock switching sequence, relay exercise. Plus (test builds) the reset record.

- **Mac side:** five ESP32 builds compile with no warnings; native tests **102/102** pass.
- **Cabinet and `bench` builds:** no control logic yet, so nothing asks a load to run — all off.
- **Test builds:** `run 1`, `run 2`, `run 3`, `run 13`; `resets` / `resets clear` (reset record).
- **`bench_test_boot`:** heat + humidify asked for from boot (tests 11 and 12).

### Board results

| B3 test | Result | Logged |
|---|---|---|
| 1 guard timing | Passed twice: min on / min off / max run exact for all three loads; crossover 30 s | ✅ stage 2 |
| 2 heater guards | Passed: off next pass for cabinet ceiling, chamber ceiling, fan | at stage 3 (with the sensor guard) |
| 3 interlock | Passed: pin 1 12 V / pin 3 0 V cooling; 0 V / 12 V heating; checked every pass | ✅ stage 2 |
| 11 watchdog | Passed: `task_wdt` ~5 s after `stall`, `TASK_WATCHDOG`, both loads held 30 s | ✅ stage 2 |
| 12 power interruption | Passed after the DevKit pull-up modification, on the heater pad: on at 30 s, 5:45 uptime from one boot | ✅ stage 2 |
| 13 relay exercise | Passed: 3 x 1.0 s / 1.0 s, loads off, ended cooling | at stage 6 |

### Found on the board (all now in the spec / build log via the spec chat)

- **DevKit can't run without USB** (EN and GPIO0 held low by its USB chip) — fixed with 2.2 kOhm
  pull-ups on EN and GPIO0 (spec 3.3). Auto-reset flashing still works.
- **"USB in restarts the board" was a misreading** — the board was held in reset while USB was out.
  With the pull-ups, plugging USB in or out doesn't restart it (proved three times).
- **12 V bulb with USB out** crashes (`INT_WATCHDOG`, recovers) or freezes (no reset, only EN
  recovers) the ESP32 at switch-on; no brownout recorded. Heater pad + humidifier fan: 10:45 with no
  reset of any kind. Bulb only with USB in. Next board: supply supervisor on EN.
- **Plenum fan at full speed ~2 min once** (20% commanded); not repeated. Status line shows
  `pwm-hw` readback and tach edges per pulse.
- Uncalibrated room RH reads 68-71% raw; the 70% ceiling uses raw RH until stage 3.

### What was added (lib/core unless noted)

| File | What |
|---|---|
| `guards.h/.cpp` | Runtime guards (4.4): stop guards (settings, boot hold, exercise, sensors, fan, ceilings, max run, stripping, compressor run, disabled) and start guards (min off, crossover both ways, other load, exercise waiting); min on only against a dropped demand |
| `interlock.h/.cpp` | Switching sequence (3.2): drop load → move K1 → 20 ms settle (a state) → assert. K1 rests on the cooling side |
| `relay_exercise.h/.cpp` | Weekly, or `exercise`: waits for compressor and heater off, 3 × (1 s energised, 1 s released), ends cooling |
| `controller.h/.cpp` | The minimum job each pass: boot hold → exercise → guards → sequence. Guard text for the status line |
| `scenario.h/.cpp` | Test builds only: `run 1/2/3/13`, `stop`; prints PASS/FAIL itself |
| `reset_log.h/.cpp`, `src/drivers/reset_record` | Test builds only: each boot's reset reason in flash, heartbeat in RTC memory; `resets`, `resets clear` |
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

### Stage 4 — built, board tests next

Temperature control. Spec answers committed (`73f9687`).

- **Mac side:** five ESP32 builds compile with no warnings (cabinet 77.6 %, test builds 81.1 % flash);
  native tests **187/187** pass.
- **New:** `temp_control.h/.cpp` (deadband + run ends, crossover count, harvest + fan duty, RH
  stripping, ineffective checks, events), `psychro.h` (Magnus vapour pressure), `heater_pwm.h`
  (1 Hz cap, default 100 %), `scenario_stage4.cpp` (`run 7, 8, 9, 10, 17, 23`), guard
  `heater-ineffective`, test-build `fan <n>|auto`, status line control part, model with chamber
  swing and vapour pressure.
- **Board tests:** 7, 8, 9, 10, 17, 23 — all scenarios on `bench_test`, bulb on J10 3-4, SSR input on
  J10 1-2, humidifier fan on J11, USB in.

### Decisions taken in building

- Stage 2-3 scenarios ask for loads directly: the control then asks for nothing, and stripping and
  the ineffective checks are suspended (run 1 freezes the model, which would trip the heater check).
- Idle fan duty 0 (B6 option / `fan 0`): an asked-for load gets its fan duty before it starts, or
  the fan guard would hold it for ever. No change at the 20 % default.
- Cooling harvest fan in 5 % steps, never rising; holds its duty if the chamber is unreadable.
- Stripping won't start if vapour pressure is already at target (it would release at once).
- Fan-only changes print one short line, not the live block.

## Next

Stage 4 board tests, one at a time.

## Talking to the board

- `pio` is not on the shell path: use `~/.platformio/penv/bin/pio`.
- With the DevKit pull-ups, plugging USB in or out doesn't restart the board; nor does the monitor.
- 12 V bulb only with USB in; with USB out use the heater pad.
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
