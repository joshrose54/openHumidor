# Where we're up to

*Updated 2026-11-07.*

## Stage 1 — built, not yet tested on the board

Stage 1 of six (CLAUDE.md): drivers, settings struct, real and bench profiles, test-build
switch, boot banner, non-blocking loop, watchdog.

- **Mac side:** done. All four ESP32 builds compile with no warnings; native tests 42/42 pass.
- **Board:** nothing flashed yet. No B3 test run yet.
- **Loads:** stage 1 cannot switch any load on. Only the plenum fan runs (1 s kickstart, then 20%).

## Next: the stage 1 board steps

Bench setup: sensors, RTC, SD card, fan and U9 fitted. **Nothing on J10 or J11; mains
extension unplugged.** Bench PSU 12.0 V, ~1 A limit, through the 4 A fuse.
**12 V on before USB in; USB out before 12 V off.**

| Step | Build | Checks | B3 test | Logged at |
|---|---|---|---|---|
| 1 | — | Connect up, power on in order | — | — |
| 2 | `bench` | Banner, bench warning, profile on every status line; all six serials match; fan RPM; I²C pull-ups OFF; SD mount time; loop max (~3 ms expected) | 14 | Stage 6 |
| 3 | `bench_test` | `stall` → reset, reason TASK_WATCHDOG, boot hold runs | 11 | Stage 2 |
| 4 | `bench_test` | **USB unplugged first.** Cut and restore 12 V; reconnect USB; `boot` → POWERON, boot hold ran | 12 | Stage 2 |

A test is logged only once every pass criterion is met (spec B3), so 11 and 12 are re-run at
stage 2 and 14 at stage 6.

## Builds

| Build | Profile | Readings | Use |
|---|---|---|---|
| `esp32dev` | real | real | **The only build for the cabinet** |
| `bench` | bench | real | B3 physical tests |
| `bench_test` | bench | scripted | B3 scenario tests |
| `real_test` | real | scripted | B4 soak |
| `native` | — | — | Mac tests: `pio test -e native` |

Build output goes to `.pio.nosync/` so iCloud Drive doesn't copy it.

## Waiting on the spec chat

Nothing blocking. Stage 2 notes already agreed: a settings problem keeps all loads off and
shows on every status line.

## Git

`dd73a6a` project setup · `fc2fa93` stage 1 · `6df08e3` no-blocking fixes ·
`5e5a102` build output to `.pio.nosync` · then this file.
