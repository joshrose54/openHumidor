# Where we're up to

*Updated 2026-11-07.*

## Stage 1 — board steps done; waiting for Josh to call it passed

Stage 1 of six (CLAUDE.md): drivers, settings struct, real and bench profiles, test-build
switch, boot banner, non-blocking loop, watchdog.

- **Mac side:** all four ESP32 builds compile with no warnings; native tests 42/42 pass.
- **Loads:** stage 1 cannot switch any load on. Only the plenum fan runs (1 s kickstart, then 20%).

### Board results (2026-11-07)

| Step | Build | B3 test | Result | Logged at |
|---|---|---|---|---|
| 2 | `bench` | 14 | Seen passing: banner, bench warning, `BENCH-PROFILE` on every status line. Also: six serials match, pull-ups OFF, SD 60 ms, loop max 1.2–4.5 ms | Stage 6 |
| 3 | `bench_test` | 11 | Watchdog part passed: `stall` → `task_wdt` + backtrace, `Boot reason: TASK_WATCHDOG`, boot hold ran. "Load held off" needs stage 2 | Stage 2 |
| 4 | — | 12 | **Moved to stage 2** (spec finding 1) | Stage 2 |

### Fixed during the board steps

- **Core error messages could hold up the loop** — one pass took 175 ms with the RTC not
  answering. Fixed (`src/drivers/core_log.h`, `CORE_DEBUG_LEVEL=0`). Proved: J9 unplugged with the
  board running → `RTC FAIL`, loop max 2.7–3.1 ms; RTC back on replug. Commit `80581e4`.
- **Opening the monitor reset the board** with `monitor_rts = 0` / `monitor_dtr = 0`. Changed to
  `1` / `1`. Proved: `pio device monitor` opened and closed twice, uptime carried on every time
  (4:08 → 5:45 → 6:51 → 7:15 → 8:03, no `rst:` line).

### Spec findings (agreed with Josh; spec chat to update the spec and CLAUDE.md)

1. **B3 test 12 can't be done as written on Rev N**: plugging USB in restarts the board, so the
   power-cut boot can't be read afterwards. Agreed: moved to stage 2 — a test build with heat and
   humidify demand from boot, bulb on J10 3–4, humidifier fan on J11; USB unplugged first, cut and
   restore 12 V; both must stay off for the 30 s hold, then start. No stored boot history (the
   stage 6 SD log covers it).
2. **Spec 5.1 power rules**: plugging USB into a running controller restarts it (seen: 2:45 → 0:06
   with 12 V on throughout). Applies in service too.
3. **Spec 5.1 / CLAUDE.md monitor settings**: should be `monitor_rts = 1`, `monitor_dtr = 1`.

### For the build log (Josh)

- RTC lost its time (reads 2000-01-01) — first boot showed `RTC FAIL`; after reseating the lead it
  read 2000-01-01. Then held time through a 12 V cut.
- Fan at 20%: 414–426 RPM, against 438–450 baseline.

## Next

1. Stage 1 report to the spec chat.
2. Josh calls stage 1 passed → stage 2 (guards, interlock sequence, relay exercise; B3 1, 2, 3, 13,
   plus 11 and 12 re-run).

## Talking to the board

- `pio` is not on the shell path: use `~/.platformio/penv/bin/pio`.
- Plugging USB in restarts the board. Opening `pio device monitor` does not (since the fix).

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

Findings 1–3 above. Stage 2 notes already agreed: a settings problem keeps all loads off and
shows on every status line.
