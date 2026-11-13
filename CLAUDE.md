# Wineador controller firmware

ESP32 firmware for Josh's wineador: an electriQ wine cooler converted into a cigar humidor, with a
custom controller board. The production firmware was built stage by stage on the **Rev N** bench
board and has passed **B3** (all 24 tests) and **B4** (soak). Rev N is no longer available: until
Rev Q arrives there is **no board to flash** — build and native tests only. The job now is the **port to Rev Q**,
the cabinet board (ESP32-S3), and the **BP2** test sketches for its bring-up.

Josh is not a professional developer. Explain plainly, keep code readable, and ask when unsure:
don't guess and don't answer with false confidence.

## Where the truth lives — read these first

| File | What it is | You may |
|---|---|---|
| `docs/wineador-spec.md` | **The design.** Chapter **3A** is Rev Q (parts, pin map 3A.4, circuits, firmware rules **3A.11**); 4 control rules; 5 firmware; **BP2** is Rev Q's bring-up test list. 2.3–2.4 and 3.1–3.3 are Rev P/N | Read only |
| `docs/wineador-build-log.md` | **Measured facts** from Rev N: sensor serials and RH calibration, fan duty curve, board values, gotchas | Read only |
| `docs/wineador-project-instructions.md` | How Josh likes to work | Read only |
| `docs/revq/6-revQ-design-report.md` | Rev Q board notes from the PCB engineer: every part and connection | Read only |
| `docs/revq/7-revQ-netlist-report.md` | Every Rev Q net and its pins | Read only |
| `docs/revq/wineador-revQ-schematic.pdf` | Rev Q schematic | Read only |
| `docs/reference/*.cpp` | Test sketches that ran and passed on **Rev N**: patterns for the serial format, go-ahead prompts, load tests | Copy and adapt |

**Never edit the spec or the build log.** Another Claude chat owns them. If you find something
wrong, missing or contradictory, **stop and report it** — what, where, and a proposed fix. Josh
takes it to that chat. Don't work around a spec problem silently. Where the spec and the design
report disagree, the spec wins; report the difference.

## Boards

| | Rev N (**no longer available** — build only) | Rev Q (ordered, not yet arrived) |
|---|---|---|
| Module | ESP32 DevKit (ESP-WROOM-32D, CP2102), modified with 2.2kΩ 3V3–EN and 3V3–P0 | **ESP32-S3-WROOM-1-N8R2** on the board: 8MB flash, 2MB quad PSRAM |
| Pins | Spec 3.1 | **Spec 3A.4** |
| Console | CP2102 USB, `/dev/cu.SLAB_USBtoUART` | **UART0 on J23** (GPIO43 TX, GPIO44 RX) through a 3.3V USB-serial cable. **Native USB (GPIO19/20) is for flashing only** — a monitor on it can reset the chip (3A.5) |
| Sensor mux channels | Wired to mux 2–7 | Wired to mux 0–5 |

**Sensor channels are numbered 0–5 everywhere** (spec 2.3): control rules, settings, log columns
(`ch0_t` …), tools. Rev Q wires channel n to mux channel n. Rev N's board settings map 0–5 to its
mux 2–7. The firmware currently uses 2–7: renumbering is part of the port, with a **log schema
bump** and `tools/check_logs.py` updated to match.

## Power rules

- **Rev N:** keep 12V on whenever USB is connected — 12V on before USB in, USB out before 12V off.
  Any test that cuts the 12V runs with USB unplugged first. The 12V bulb only with USB connected.
- **Rev Q:** USB powers nothing. **Any order is safe**; flashing and the monitor need **12V on**.
  The monitor cable on J23 uses three wires only — GND, TX, RX — **never its VCC wire**.
- Bench instructions to Josh always state the power-up order for the board in use.

## Test sketches and test builds

- Anything that switches loads **waits for Josh's go-ahead** ("type g to start") so he can meter
  first. Every step is plain and says what to check and what to record.
- The `bench` and cabinet builds switch loads by themselves from the real sensors. On the bench,
  flash **test builds only** unless Josh has asked for a non-test build; before flashing one, say
  plainly what it will switch on and when, and wait for his go.
- Serial output shows **what is live right now** after each step. See
  `docs/reference/bp12_outputs_test.cpp` for the format Josh found clear.
- **BP2 sketches** name the test points by their **printed labels** as well as TP numbers (spec
  3A.12: the board prints "5V", "3V3RF", "BLW−" …, not TP numbers).

## Build environment (spec 5.1)

- pioarduino platform **pinned to 55.03.311** (Arduino-ESP32 3.3.11, ESP-IDF 5.5.5) for both boards.
  Keep the existing `esp32dev` environment (Rev N) building, and the `native` environment for the
  Mac-side tests.
- **Rev Q environment: spec 5.1** (`[revq_common]` and the builds on it).
- Rev N: `monitor_rts = 1`, `monitor_dtr = 1`. Rev Q: the monitor is on J23 (no RTS/DTR wires), so
  it cannot reset the board.
- `pio` is not on the shell path: use `~/.platformio/penv/bin/pio`.
- Arduino-ESP32 3.x attaches PWM by pin: `ledcAttach(pin, freq, bits)`, `ledcWrite(pin, duty)`.
- **Build both board environments and run the native tests before every hand-over.**

## WiFi credentials

`include/wifi_time.h` holds Josh's WiFi name and password. **Never print, log, copy, quote or commit
them.** It is in `.gitignore`. Never overwrite it.

## Architecture rules (spec 5.2, 5.5)

- **Control logic is plain C++ with no hardware calls** (lib/core, native tests). Drivers are
  separate. **All pins in one header; per-board settings in `include/board.h`.** Rev Q is a driver
  and board-file change, not a rewrite of the control logic.
- **Nothing blocks. `delay()` is never used.** All timing from `millis()`; explicit state machines.
- **Software watchdog 5s** (5.2). **Rev Q also has a hardware watchdog** (3A.11, below).
- **Minimum job every pass**: evaluate the guards and turn off any load whose guards fail, never
  dependent on a sensor read, a log write or the network.
- **One settings struct**; logic reads the struct, never a literal.
- Profiles real and bench; test builds with the cabinet model — as now.
- I²C internal pull-ups **disabled** after every bus step (5.3).
- **Free internal heap never below 30 KB** (5.2) — on Rev Q **at all times**, WiFi connects included.

## Rev Q firmware rules (spec 3A.11 — read it in full; the key ones)

- **Hardware watchdog, GPIO1:** an input until the control loop runs, then change its level every
  pass, from the loop, never a timer; never more than 0.9s between changes. To pause it, set GPIO1
  to an input with **both internal pulls off** (never pad hold) — and pause it for an OTA erase.
- **SD card on SPI3, power on GPIO39:** high cuts power; to switch back on, set GPIO39 to an
  **input with its pull-up off** (never drive it low); SD lines low or inputs before cutting; wait
  at least 100ms before switching back on.
- **Blowers, GPIO15:** PWM about 100kHz, high = on, duty = speed. Their rules are spec 4.6b;
  **in stages Q1–Q3 run them at full speed.**
- **Ion generator, GPIO17:** high = on. Its rules are spec 4.6c; **in stages Q1–Q3 keep it off.**
- **LCD on SPI2 (GPIO10–12, 9), 15MHz or less.** GPIO48 resets LCD and touch together: low at
  least 5ms, then 120ms before Sleep Out and 300ms before the first touch read. Backlight GPIO13.
  **Screens: spec 5.5** (walk-past, settings, alerts), built in stage Q4. **Drawing never holds the
  loop more than 50ms** (3A.11).
- **12V sense, GPIO8, ADC1 at 12dB attenuation.**
- I²C0 (GPIO35/36) mux only, 25kHz; I²C1 (GPIO21 SDA / GPIO14 SCL) RTC and touch, 100kHz. The
  DS3231 has no EEPROM on Rev Q.
- **WiFi stays connected** (5.4); card writes are not paused for it; outage buffers in PSRAM.
- 32kHz crystal stays disabled (GPIO15/16 are its pins).

## The plan for this phase

| Stage | Adds | Proven by |
|---|---|---|
| Q1 | Rev Q build environment and board files; pins per 3A.4; channels renumbered 0–5 everywhere, log schema bump, `check_logs.py`; Rev N still builds and behaves as before | Both builds; native tests; `check_logs.py` on the B4 card copies |
| Q2 | Rev Q drivers: hardware watchdog, SD power switch and SPI3, both I²C buses, 12V sense, blower PWM (full speed), ion output (off), backlight, minimal LCD and touch for test 29, UART0 console, WiFi always on with PSRAM buffers | Native tests; builds |
| Q3 | BP2 test sketches for tests 15–30, and the test-build commands BP2 needs (`stall`, the DS3231 EOSC lost-time command for B3 test 16) | Builds; Josh's review of each sketch's steps |
| Q4 | The LCD screens (spec 5.5): walk-past, settings and alerts, touch, backlight dimming, the **Allow update** window. **What the screen shows** — modes, alerts and their order, the countdown, local time, settings steps and bounds, timeouts — **is plain C++ in lib/core with native tests**; drawing and touch are the driver side. Rev Q builds only | Native tests; builds; **every screen rendered to a PNG on the Mac by the same drawing code** for Josh's review; checked on the board at BP2 |
| Q5 | The web dashboard and its JSON API (spec 5.5): the seven pages, the documented and versioned API, OTA from the browser inside the LCD's Allow update window, build-identity check on every image. **What the API accepts and refuses** (bounds, the window, the build check) **is plain C++ in lib/core with native tests**. Web pages designed with the **impeccable** skill, within the flash and no-internet limits | Native tests; builds; the pages served from a native or Mac-side build for Josh's review on his Mac and phone; checked on the board after BP2 |

Then, when the board arrives: BP2 on the bench with Josh, test by test. **One stage at a time**:
don't start the next until Josh says the current one is done. The blower and ion rules (4.6b, 4.6c)
come after BP2 (blower idle speed needs test 22). OTA behaviour (5.2) is built with Q5.
Where the spec leaves something open, **don't invent it**: report it.

## Web pages (Q5)

Use the **Impeccable** plugin (installed by Josh; its design context is set with `/impeccable init`) for the dashboard's design. Its usual choices give way to
the spec: **nothing loaded from the internet** (no web fonts, no CDN scripts), small compressed files
served from flash, phone and computer, the LCD's colours and icons.

## Git

**Commit at the end of every stage Josh accepts**, as its own commit, before starting the next.
Updated docs from Josh (spec, build log, CLAUDE.md) go in their own commit. Never commit
`include/wifi_time.h` (it is in `.gitignore`); check before every commit.

## Reporting back at the end of each stage

Give Josh a short report he can paste to the spec chat:

1. **Built:** what the stage added, in a few lines.
2. **Native tests:** which ran, pass/fail.
3. **Bench tests** (if any): test number, what was done, the readings that show the pass criteria
   were met. Say plainly if one failed and what was changed before it passed.
4. **Spec findings:** anything wrong, missing or contradictory, with a proposed fix.
5. **Gotchas** worth adding to the build log.