# Wineador controller firmware

ESP32 firmware for Josh's wineador: an electriQ wine cooler converted into a cigar humidor, with a
custom controller board. This project builds the **production firmware**, stage by stage, and runs
the **B3 tests** that prove its safety logic on the bench.

Josh is not a professional developer. Explain plainly, keep code readable, and ask when unsure:
don't guess and don't answer with false confidence.

## Where the truth lives — read these first

| File | What it is | You may |
|---|---|---|
| `docs/wineador-spec.md` | **The design.** Sections 4 (control rules) and 5 (firmware) say what the firmware must do; Track B → **B3** is the test list and how tests are run; 2.3–2.4 and 3.1–3.3 describe the hardware and pins | Read only |
| `docs/wineador-build-log.md` | **Measured facts**: sensor serials and RH calibration (gain, offset), fan duty curve, board measurements, gotchas | Read only |
| `docs/wineador-project-instructions.md` | How Josh likes to work | Read only |
| `docs/reference/*.cpp` | Test sketches that **ran and passed on this exact board**. Proven driver code for the fan PWM and tach (pulse-width check), SHT40 + mux, mux reset, SD, RTC, the four load outputs and the relay | Copy and adapt code |
| `docs/next-board-changes.md` | Josh's list of changes for the next board — context only | Read only |

**Never edit the spec or the build log.** Another Claude chat owns them. If you find something
wrong, missing or contradictory in the spec, **stop and report it** — what, where, and a proposed
fix. Josh takes it to that chat, which updates the spec. Don't work around a spec problem silently.

## Hardware on the bench

- Board: **Rev N** (the design is Rev P; see the BP section of the spec and the build log's Rev N
  note). Rev N has **no D7, no R17–R20, no TP7**. Pins and behaviour are otherwise as the spec.
- ESP32 DevKit (ESP-WROOM-32D, CP2102, AMS1117), port `/dev/cu.SLAB_USBtoUART`. **Modified** with
  2.2kΩ 3V3–EN and 2.2kΩ 3V3–P0 (spec 3.3): without them it can't run without USB.
- Pins: spec 3.1. Loads switch through U9 (74HCT08) to IRLZ44N MOSFETs; K1 relay in the 12V feed is
  the heat/cool interlock (spec 3.2).

### Power rules — Rev N (spec 5.1)

- **Keep 12V on whenever USB is connected: 12V on before USB in, USB out before 12V off.** Flashing
  on the board is then fine.
- **Any test that cuts the 12V (B3 test 12) runs with USB unplugged first.** Tell Josh this
  explicitly every time.
- With the DevKit pull-ups fitted (above), plugging USB in or out doesn't restart the board, and it
  runs with no USB.
- **The 12V bulb only with USB connected.** With USB out its switch-on surge crashes or freezes the
  ESP32; use the heater pad (spec B3).
- Bench instructions to Josh always state the power-up order.

### Test sketches and test builds

- Anything that switches loads **waits for Josh's go-ahead** ("type n to start") so he can meter
  first. Every step is plain and says what to check and what to record.
- Serial output shows **what is live right now** after each step, never "next" before it's applied.
  See `docs/reference/bp12_outputs_test.cpp` for the format Josh found clear.

## Build environment (spec 5.1)

- pioarduino platform **pinned to 55.03.311** (Arduino-ESP32 3.3.11, ESP-IDF 5.5.5). Board
  `esp32dev`. `platformio.ini` is set up for this, plus a `native` environment for the Mac-side tests.
- `monitor_rts = 1` and `monitor_dtr = 1`: opening or closing the monitor must not reset the board
  (proved on the bench; at 0 it resets). Any serial script of your own clears RTS before DTR.
- `pio` is not on the shell path: use `~/.platformio/penv/bin/pio`.
- Arduino-ESP32 3.x attaches PWM by pin: `ledcAttach(25, 25000, 8)`, `ledcWrite(25, duty)`.
- **Build and run the native tests before every hand-over.** Nothing goes to Josh that doesn't build.

## WiFi credentials

`include/wifi_time.h` holds Josh's WiFi name and password. **Never print, log, copy, quote or commit
them.** It is in `.gitignore`. Test sketches may call its `syncTime()` (blocking, at boot only). The
**production firmware must not block** (spec 5.2): write a background time module for it that reads
the credentials from the same file.

## Architecture rules (spec 5.2, 5.5, B3)

- **Control logic is plain C++ with no hardware calls**: it takes the time and the readings and
  returns what each load should do. Drivers (I²C, SD, PWM, outputs, RTC, WiFi) are separate.
  **All pins in one header.** This is what makes the native tests possible and the next board
  (ESP32-S3 module, on-board RTC, LCD with SD) a driver swap, not a rewrite.
- **Nothing blocks. `delay()` is never used.** All timing from `millis()`; explicit, resumable
  state machines. Relay settle (20ms, spec 3.2) is a state, not a delay.
- **Watchdog: 5 s.**
- **Minimum job every pass**: evaluate compressor, heater and humidifier guards, and turn off any
  load whose guards fail — never dependent on a sensor read, a log write or the network.
- **One settings struct**; logic reads the struct, never a literal. Safety values fixed; user and
  tuning values with the **starting values in spec 5.5**.
- **Profiles:** real and bench (spec B3's table). The bench profile prints an unmissable banner,
  appears on every status line and in the version string.
- **Test builds** (spec B3, "How the tests are run"): scripted readings replace the sensor values,
  driven by a **simple cabinet model** that responds to the loads; bus checks (CRC, timeout, absent,
  serial) stay real. Compiled out of the cabinet build entirely; a test build says so on its banner
  and every status line.
- I²C internal pull-ups **disabled** (spec 5.3) — the Arduino core enables them by default.

## The plan — six stages, in the spec's build order

| Stage | Adds | B3 tests |
|---|---|---|
| 1 | Drivers from the reference sketches, settings struct, real/bench profiles, test-build switch, boot banner, non-blocking loop, watchdog | 11, 14 (11 logged at stage 2) |
| 2 | Runtime guards, interlock switching sequence, relay exercise | 1, 2, 3, 13, plus 11 (a load held off) and 12 (new method) |
| 3 | Sensor health, serial check, calibration, fan monitoring, I²C bus recovery, RTC lost-time and NTP | 4, 5, 6, 15, 16 |
| 4 | Temperature control: deadband, run-end rules, crossover, harvest, RH stripping, ineffective checks | 7, 8, 9, 10, 17, 23 |
| 5 | Humidifier | 19, 20, 21, 22 |
| 6 | SD logging per 5.4, serial interface, soak scenario for B4 | 18, 24, then B4 |

"Logged" in a B3 pass criterion is proven once, in test 24; earlier tests pass on the serial
event (spec B3). **Guards before control logic.** One stage at a time: build it, run its native tests, then hand
Josh the on-board tests step by step. Don't start the next stage until Josh says the current one
passed.

## Reporting back at the end of each stage

Give Josh a short report he can paste to the spec chat:

1. **Built:** what the stage added, in a few lines.
2. **Native tests:** which ran, pass/fail.
3. **B3 tests on the board:** for each, in spec order — the test number, what was done, the
   readings or serial output that show the pass criteria were met. Only tests that passed are
   logged; say plainly if one failed and what was changed (code or hardware) before it passed.
4. **Spec findings:** anything wrong, missing or contradictory, with a proposed fix.
5. **Gotchas** worth adding to the build log.