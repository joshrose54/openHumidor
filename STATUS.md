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

### Next

Stage 3 — in progress (see below).

## Stage 3 — passed (Josh, 2026-11-08)

Sensor health (4.3), serial check and calibration (5.3), control-source figures (4.2), fan stall
kickstarts (5.6), I2C stuck-bus recovery (5.6), RTC lost time (OSF) and NTP (5.4, 5.6), clock
summary on `boot`.

- **Mac side:** five ESP32 builds, no warnings; native tests **138/138**.
- **Flash ~78%** (WiFi + SNTP); partition decision at stage 6 (agreed).

### Board results

| B3 test | Result |
|---|---|
| 2 heater guards | PASS: off next pass (from the reading reaching the guards) for cabinet ceiling, chamber ceiling, sensors (chamber implausible; recovery + 30 s hold), fan |
| 4 sensor failure | PASS: (a) run 4 chamber/3-of-4/2-of-4 + recovery hold; (b) swapped plugs -> wrong-serial both; (c) run 41 heater off at 2 of 4; (d) run 42 cooling off with chamber out, J10 1-2 12.3 V / 0 V |
| 5 divergence | PASS: excluded after 10:15 diverging (first sweep at/after 10 min), recovered 2:45 later |
| 6 fan failure | PASS: kickstart at once and 30.0 s later; heater off, humidifier unaffected; resumed on replug |
| 15 I2C bus fault | PASS after a fix: internal pull-ups now switched off after every bus step (ESP-IDF re-enables them on a bus reset) |
| 16 RTC lost time | PASS: cell out, 12 V cut, booted with USB out: OSF set, NTP at 15 s, DS3231 26.9 years out written, OSF cleared, hold 30 s; cell refitted with power on, OSF clear after |

Logged by the spec chat: B3 2, 4, 5, 6, 13, 14, 15, 16 (with 1, 3, 11, 12 from stage 2). "Logged" is proven once, in test 24.

## Next

Stage 4 plan (temperature control: deadband, run-end rules, crossover, harvest, RH stripping,
ineffective checks; B3 7, 8, 9, 10, 17, 23) — plan first, no code until Josh agrees.

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
