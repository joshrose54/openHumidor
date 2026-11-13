# Where we're up to

*Updated 2026-11-13.*

## Port to Rev Q: stage Q3 (accepted 2026-11-13)

**BP2 test sketches** (`src/bp2/`, one PlatformIO build each, `pio run -e <name> -t upload`; they use the
firmware's own drivers, nothing copied; the firmware builds never compile them):
`bp2_15_boot_state` (tests 15-17) · `bp2_18_outputs` · `bp2_19_interlock` · `bp2_20_full_load` ·
`bp2_21_wifi_rails` · `bp2_22_blowers` · `bp2_23_blower_reset` · `bp2_24_watchdog` (24-25) · `bp2_26_sd_power` ·
`bp2_27_12v` · `bp2_28_sensors` · `bp2_29_lcd_touch` · `bp2_30_rtc` · `bp2_35_bulb`.
Each: g to start, n per step, x = all off; NOW LIVE then CHECK NOW, by printed labels and TP numbers.
Every banner and step: `docs/bp2-sketch-steps.md` (generated from the sketches by `tools/bp2_steps.py`).
Next: BP2 on the bench when Rev Q arrives (Josh says when).

**Firmware:** every build clears DS3231 EOSC at boot if set, and logs it (3A.11). Test-build commands
`hwdog off|on` (B3 test 11's second run) and `rtc`, `rtc eosc on|off` (B3 tests 16, 24). An
`SD STRESS START` event marks where a stress run begins.

**check_logs.py:** `--bulb` (test 35, the sketch's `/bp2/bulb.csv`: two runs, the hardware watchdog paused
then `hwdog on`); `--test24` on a Rev Q card requires the 12V dip with its recovery and WiFi lost / back;
on Rev Q cards `--stress` checks WiFi
stayed connected and no power cycle in the run, `--outage` checks one power-cycle event, then SD FAIL,
then the card back (test 38). Event files read in time order.

Native tests **304/304**; 24 builds (10 firmware, 14 sketches) without warnings.

## Port to Rev Q: stage Q2 (accepted 2026-11-13)

Q1 accepted and committed (`c18774d`); the channel names and labels (`e7f7924`); docs (`ffbef5f`).

**Q2 — the Rev Q drivers** (no board yet: native tests and builds only):
- Hardware watchdog (GPIO1): an input until the loop runs, then toggled every pass from the loop;
  the longest gap is in the status line, an event over 0.9 s. `hwdog::pause()` / `resume()` for later.
- SD on SPI3 with the power switch (GPIO39): soft-on at boot; when the retry's remount fails, the
  card is power-cycled once (release lines, cut, 150 ms, soft on, 10 ms, remount) before SD FAIL.
- I2C0 (mux, sensors, 25 kHz) and I2C1 (RTC and touch, 100 kHz); I2C1 restarted after a failed RTC step.
- 12V sense (GPIO8, 12 dB): 100 ms readings, 1 s averages; `v12_mean`, `v12_min_1s` columns; dip and
  recovered events. `psram_min` column. All three in the readings AND hourly rows, empty on Rev N.
  Heap measured as internal RAM only.
- Blowers full speed (GPIO15 high), ion off (GPIO17 low, every pass), backlight 5 kHz PWM off, the LCD
  cleared to black in its own task at boot. LCD drawing and touch reads exist for the test 29 sketch.
- WiFi always on: the NTP window starts and stops SNTP; "WiFi lost" / "back after N s" events.
- Outage buffers 1,000 / 1,440 / 168 in PSRAM (964,000 bytes); without PSRAM, an event and Rev N's sizes.
- Native tests **295/295**.

## Port to Rev Q: stage Q1 (accepted 2026-11-13)

Rev N is no longer available: build and native tests only until Rev Q arrives.

- **Rev Q environments** (spec 5.1): `revq`, `revq_bench`, `revq_bench_test`, `revq_bench_test_boot`,
  `revq_real_test` on `esp32-s3-devkitc1-n8r2`, `default_8MB.csv`, console on UART0. **Not for
  flashing until Q2** (the banner says so). Rev N's five keep building, with `-DWINEADOR_BOARD_REVN`.
- **Pins and board settings:** `include/pins.h` and `include/board.h` hold both boards (`pins_revn`,
  `pins_revq`, `board_revn`, `board_revq`); the board flag picks one. Rev N values unchanged.
- **Channels 0-5 everywhere** (spec 2.3). The mux map is only in `board.h` (Rev N 2-7, Rev Q 0-5),
  used once in `i2c_bus`. Commands `exclude`/`crc`/`timeout` take 0-5 and echo the sensor's name and
  probe label.
- **Log schema 2:** ch0-ch5 columns, a `# board` header line, `-s2` in every file name
  (`hourly-s2.csv` too). `tools/check_logs.py` reads schema 1 (B1-B4 cards) and 2.
- Native tests **254/254** (new: `test_board_map`, `test_sample_card`, which writes sample schema 2
  cards under `.pio.nosync/sample-cards/` for `check_logs.py`).

## Phase closed: B3 and B4 passed on Rev N (2026-11-11)

**B3 (all 24 tests) and B4 have passed on the Rev N bench board.** Work now waits for the next
board: no next-board code until the board designer's proposal is back and the spec has been
updated for it. The planning note was `NEXT-BOARD-PORT.md` (now in archive/; superseded by CLAUDE.md and the spec).

**B4:**
- **Step 1, soak: PASS.** 21 h on `real_test` (accepted against "at least 20 h"), one boot, scenario
  verdict PASS, guard audit clean, no CRC failures, heap/SD/loop trends flat. Clock test after the
  soak: PASS.
- **SD criterion: PASS** after a fix. The soak's two SD write failures were traced to WiFi current
  bursts resetting the card on the DevKit's shared 3.3 V rail; 5 in 6,000 writes were also found to
  be lost silently at file close. Fixed and proven: stress run 6,000 writes and a final 2,000, both
  with an NTP window mid-run — 0 failures, 0 retries, 0 size mismatches; card pull across the top of
  an hour — every held event, readings row and hourly row written in order, none dropped.
- **Loop time:** 3.8–4.7 ms, including through the radio switching (one 9.4 ms interval).
- **Heap floor (5.2, 30 KB):** met outside NTP windows (41.7 KB lowest after a card write). During a
  window WiFi's own buffers take it to 29.3 KB at lowest — logged, and accepted on Rev N (spec chat
  decision, option a). The next board must meet it at all times (BP2).
- Reports: `~/wineador-logs/B4-report-final-2026-11-11.md` (with the interim SD investigation
  report and all logs under `~/wineador-logs/`). Results are in the build log (B4.1).

**Firmware state** (`main`; the only branch):
- Stages 1–6 complete, as the spec's 5.x for Rev N. Six builds: `esp32dev` (cabinet), `bench`,
  `bench_test`, `bench_test_boot`, `real_test`, `native`. All compile with no warnings; native tests
  **243/243**.
- Since stage 6: every write verified and retried once after a remount (5.4); Rev N per-board
  settings in `include/board.h` — WiFi only for the NTP sync (radio switched by its own task), no
  card writes while WiFi is on, outage buffers 60 events / 30 readings rows / 24 hourly rows; SD
  clock 1 MHz; lowest-ever heap in the status line, the readings and hourly rows (`heap_min`) and
  `log`, with a `heap` event the first time it drops below 30 KB. Back on the framework's own SD
  library.
- Test-build tools: `sdstress`, `ntpsync`; Mac checker `tools/check_logs.py` (`--test24`, `--soak`,
  `--stress`, `--outage`).
- The bench board runs the latest `main` (`real_test`), with the card in.

**Open** (build log, section 4):
- **The SD cure in hardware is unproven:** on Rev N the errors were avoided (WiFi only for NTP,
  writes paused), not cured. The next board's SD slot and supply are proven by BP2's deep SD test
  with WiFi on all the time.
- **The heap floor while WiFi connects:** 29.3 KB on Rev N, accepted there; the next board, with
  PSRAM, must meet 30 KB at all times (BP2).
- **A freeze the ESP32's own watchdogs can't catch** (the 21 W bulb's surge with USB out). Not seen
  in B4; watch for it on the next board (it has a supply supervisor on EN).
- **RTC:** the HW-084's LIR2032 is flat (it can't charge from 3.3 V) — the clock can lose its time at
  a power-up; NTP corrects it. No action on Rev N; replaced on the next board. Also the older
  unexplained 2 h 27 m loss, and the DS3231 temperature register reading garbage (never used).
- **For B6:** log download and settings over WiFi, a "door opened" marker, folding run-fan step 1
  into the step 2 mapping test.

---

# History, stage by stage

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

### Stage 4 — passed (Josh, 2026-11-08)

Temperature control. Spec answers committed (`73f9687`, `f10b7af`); stage 4 checkpoint `7d30b47`.

- **Mac side:** five ESP32 builds compile with no warnings (cabinet 77.6 %, test builds 81.1 % flash);
  native tests **189/189** pass.
- **New:** `temp_control.h/.cpp` (deadband + run ends, crossover count, harvest + fan duty, RH
  stripping, ineffective checks, events), `psychro.h` (Magnus vapour pressure), `heater_pwm.h`
  (1 Hz cap, default 100 %), `scenario_stage4.cpp` (`run 7, 8, 9, 10, 17, 23`), guard
  `heater-ineffective`, test-build `fan <n>|auto`, status line control part, model with chamber
  swing and vapour pressure.

### Board results (bench_test, bulb J10 3-4, SSR input J10 1-2, no mains, USB in)

| B3 test | Result |
|---|---|
| 7 deadband | PASS 8:24. Heater on 17.37, off 19.23; compressor on 20.71, max-run cut, on 21.57, off 19.71; heater on 18.22. Nothing started 18.5-20.5 |
| 8 crossover | PASS 3:48. Heater held by crossover, on exactly 30.0 s after cooling stopped; cooling likewise; 2 crossovers counted. (First attempt stopped: scenario baseline bug, fixed `run 8` and re-flashed) |
| 9 stripping | PASS 5:26. Persistence 30.0 s, reset by a dip; strip released at vp 1.441 <= 1.447 with RH 64.7 % |
| 10 floor | PASS 5:09. Floor stop at ch2 17.88 (mean 18.63); no persistence for 60 s below; restart exactly 30 s after floor cleared |
| 17 ineffective | PASS 5:16. Heater off at 1:00.0, held 2:00.0, retried, flag cleared; cooling flagged at 1:00.0 and kept running |
| 23 fan rules | PASS 5:09. 3 kickstarts each 1.0 s; 40 % / 60 % with loads; cooling harvest 100-75-55-45 %, both harvests ended on convergence (59.9 s); 10 % refused (scenario and console) |

### Decisions taken in building

- Stage 2-3 scenarios ask for loads directly: the control then asks for nothing, and stripping and
  the ineffective checks are suspended (run 1 freezes the model, which would trip the heater check).
- Idle fan duty 0 (B6 option / `fan 0`): an asked-for load gets its fan duty before it starts
  (now spec 4.6). No change at the 20 % default.
- Cooling harvest fan in 5 % steps, never rising; holds its duty if the chamber is unreadable.
- Stripping won't start if vapour pressure is already at target (it would release at once).
- Fan-only changes print one short line, not the live block.
- Ineffective event text says "chamber changed +/-x C" (after the board run; not yet flashed).

## Stage 5 — passed (Josh, 2026-11-08) (humidifier, B3 19-22)

Spec answers committed (`4ae15c0`): humidifier check on vapour pressure (~0.012 kPa = 0.5 points at
setpoint); a call stays on through guard pauses; persistence only outside strips; test 20 excludes
sensors one at a time by scripted readings; test-build toggle for "humidify during cooling runs".

- **Mac side:** five ESP32 builds compile with no warnings (cabinet 77.8 %, test builds 82.1 % flash);
  native tests **216/216** pass.
- **New:** `humid_control.h/.cpp` (call with persistence, release at setpoint, call held through guard
  pauses, persistence only outside strips, vapour-pressure ineffective check with retry hold, events,
  status text), guard `humid-ineffective`, `setHumidifyDuringCooling` + test-build `humidcool on|off`,
  `scenario_stage5.cpp` (`run 19, 20, 21, 22`; run 21 adds a direct humidify demand), banner line,
  status line humid part. `test_humid_control` (16) + simulated-board scenario tests.
Stage 5 checkpoint `2237daf`; scenario humidifier rate 1 -> 2 points/min after test 19 (margin for
the 1 min check with 15 s reads), re-flashed before test 20.

### Board results (bench_test, humidifier fan on J11; bulb and SSR input connected, no mains)

| B3 test | Result |
|---|---|
| 19 humidifier guards | PASS 3:36. On exactly 30.0 s after RH 56 % seen; off at max run 2:00.0 (still asked for); on again after exactly 20.0 s min off, same call; off at the next read when RH 62.6 % after 9.9 s on (no min on); one-read dip did not start it |
| 20 ceiling and sensors | PASS 5:12. Off (rh-ceiling) at the first read with ch3 at 70.7 %, mean 55.5 %, call kept; resumed after min off, same call; ch2 excluded -> 3 of 4, kept running; ch4 excluded -> off (sensors); both recovered -> on exactly 30.0 s later (the hold) |
| 21 exclusivity | PASS 2:15. Held by stripping 20 s; held (compressor-run) 20 s with the setting off; on at once with `humidify during cooling` on, compressor running |
| 22 ineffective | PASS 4:06. Off at exactly 1:00.0, "vapour pressure changed +0.0000 kPa (needs +0.0117)", flagged, held 2:00.0 with countdown, retried, "EFFECTIVE AGAIN +0.0117 kPa", flag cleared |

### Decisions taken in building

- The call is judged only on fresh readings from >= 3 healthy cabinet sensors; otherwise it is left
  as it is (the sensor guard pauses the humidifier).
- Test scenarios that need RH exactly at the strip release target use 61.5 % (62 % at 20 C sits on
  the target, and float rounding can leave it a hair above).

## Stage 6 — B3 part passed (Josh, 2026-11-08): B3 complete. B4 next (SD logging, serial interface; B3 18, 24, then B4)

Spec answers committed (`861584a`, `b89d4cc`); stage 6 checkpoint `3e0983e`.

- **Partition:** `min_spiffs` (two ~1.9 MB firmware slots, OTA possible later; NVS unchanged at
  0x9000). Builds: cabinet 53.6 %, test builds 56.7 % flash; RAM ~30 %.
- **Mac side:** five ESP32 builds compile with no warnings; native tests **232/232** pass.
- **New, logic (lib/core):** `logbook` (paths by UTC month + schema, self-describing headers with each
  serial's gain/offset, readings/events rows, CSV quoting, retention plan, CRC/timeout limit 10 per
  channel per hour, guard-block tracker), `hourly` (min/max/mean per hour + load on-time),
  `guard_audit` (independent guard check from what was live), `commands` rewritten (serial
  interface), `scenario_stage6` (`run 24`, `run soak`).
- **New, firmware (src):** `drivers/sd_log` (SD task on core 0 at idle priority: 16-row batches,
  events at once, folders + headers, retention at start, failure -> events kept in RAM, remount every
  30 s), `drivers/user_store` (NVS per build kind: usr_cab / usr_bench / usr_test), `app/eventlog`
  (one path: serial + RAM + SD), `app/serial_ui` (status, sensors, events, log, settings), I²C
  test-build `crc chN` / `timeout chN`.
- **Mac tools:** `tools/check_logs.py` (files, headers, RH = gain x raw + offset, event types,
  guard audit from the events file, soak trends), `tools/fill_card.py` (dummy files for the
  retention check, sized from the card).
- **Model:** drift towards the room now applies all the time (a strong drift can outrun a load —
  the soak uses it for the max runs).

### Decisions taken in building

- Events carry `uptime_ms` as well as the timestamp, so timing checks survive a clock correction.
- Readings carry `chN_rh_raw` (and `_real` columns in test builds) so the file itself proves RH =
  gain x raw + offset.
- Logging starts at the first sensor sweep after the clock is set from the RTC (or 10 s without
  it); the boot event is the first row.
- Scenarios refuse to start if the user settings differ from the defaults (`set defaults` first).
- The soak: 10 phases, ~9.5 h a cycle, to 24 h; on the Mac: starts 17/16/18, max-run stops 6/2/4,
  strips 2, crossovers 9, audit clean; minima set below that (12/6/4, 2/1/2, 2, 4).

### Board results (bench_test)

| B3 test | Result |
|---|---|
| 24 logging | PASS (28/28 checks, `tools/check_logs.py --test24`). Card reformatted (FAT32/MBR/WINEADOR) and filled to 8 % free: boot deleted 2025-09 (-> 14.6 %) then 2025-10 (-> 21.2 %) and stopped. Cell out: RTC LOST TIME, NTP correction (DS3231 -847478700 s, OSF cleared). `run 24` PASS (11:55) + by hand: crc/timeout ch4, set temp/defaults, J4 unplugged (absent/recovered), J2 unplugged (stall/kick/recovered), J5 SDA short (bus stuck/excluded/recovered), card pulled (flush failure, remount). 97 events, every 5.4 type; 49 rows in 16-row batches; RH = gain x raw + offset every row; files by UTC month; run 30:45 |
| 18 SD card failure | PASS. Card pulled during `run 8`: "SD FLUSH FAILURE: write failed ... control unaffected" at the next write (1:21), status `SD FAIL`; run 8 PASSED with the card out (crossover 30.0 s both ways, loop max <= 6.5 ms; the failed write took 4.1 s inside the SD task only). Card back: remounted within 30 s, 16 kept events written in order, 6 readings rows dropped while out, 0 events dropped |

Found in test 24 and fixed (re-flashed, lost-time boot repeated): logging started before the RTC's
lost-time flag was read, so the first boot's first events and row were marked trusted; the NTP
correction event was marked untrusted. Also: tools/fill_card.py wrote one 46.9 GB file — FAT32
holds nothing of 4 GiB or more; now parts under 3.9 GB. The logger now starts a -p2 part file if a
log file would pass 3.9 GB (Mac-tested; never near it in practice).

## B4 (2026-11-09 to 2026-11-11)

See "Phase closed" at the top and the build log, B4.1.
