# B7 test 6: OTA, the commands

Spec B7 test 6. **Loads unplugged; Josh's go before each flash** (the cabinet build switches loads
by itself). Rev Q: 12V on for flashing and the monitor; the monitor cable on J23 uses GND, TX and
RX only, never its VCC wire. Every command runs from the project folder on the Mac, with the Mac on
the same WiFi as the board.

## 0. Build the images

```sh
~/.platformio/penv/bin/pio run -e revq -e revq_bench -e revq_real_test -e revq_rollback_test
```

Every board build runs `tools/check_identity.py`: it must print `Identity check: revq block at
0x120: 2.0.0` (and the matching line for each build); a wrong or missing block fails the build.
The images are copied to `.pio.nosync/images/`: `revq.bin`, `revq_bench.bin`,
`revq_real_test.bin`, `revq_rollback_test.bin`, and - made from `revq.bin` by
`tools/make_bad_image.py`, which the revq build runs - `revq-corrupt.bin` (and
`revq-esp32-chip.bin`, the native tests' made-up Rev N image; not used here).

**The Rev N image** is the last real one, kept outside the project: `~/wineador-logs/rev-n-last/esp32dev.bin`
(built from git tag `rev-n-last`; its `README.md` there gives the commit and SHA-256). Rev N is
retired and can't be built from `main`.

Start the cabinet build by USB first (Josh's go): `~/.platformio/penv/bin/pio run -e revq -t upload`.
Then every case below is sent from the dashboard (the Update page) or with `curl`. `up` is short for:

```sh
up() { curl -sS -i --max-time 150 -X POST -H 'Content-Type: application/octet-stream' \
       --data-binary @"$1" http://wineador.local/api/update; echo; }
```

In a second Terminal, watch the board's own view: `curl -s http://wineador.local/api/update`, and
the serial monitor's status line (`loop max`) for the worst loop pass during an update.

## 1. Refused, nothing written

| Case | Do | Expect |
|---|---|---|
| Outside the window | (don't press Allow update) `up .pio.nosync/images/revq.bin` | `403 window_closed` |
| Bench build | Allow update on the LCD, then `up .pio.nosync/images/revq_bench.bin` | `422 wrong_image`, `"image":"bench_build"` |
| Test build | `up .pio.nosync/images/revq_real_test.bin` | `422`, `"image":"test_build"` |
| Rev N image | `up ~/wineador-logs/rev-n-last/esp32dev.bin` | `422`, `"image":"rev_n_image"` |
| Compressor running | flash the bench test build by USB (`~/.platformio/penv/bin/pio run -e revq_bench_test -t upload`, loads unplugged), type `run 3` in the monitor; when it prints `COOLING ON` (the compressor is held on for 60 s), press Allow update and `up .pio.nosync/images/revq.bin` | `409 compressor_running`; then `stop`, and flash `revq` again by USB |

Each refusal leaves the window open (5.5) and is logged ("Update refused: ...", at most 10 an hour).

## 2. Stopped part way

Sends the first 600 KB of the image, says it is the whole size, then waits. The board abandons it
after 60 s with no data:

```sh
head -c 600000 .pio.nosync/images/revq.bin > /tmp/half.bin
curl -sS -i --max-time 150 -X POST -H 'Content-Type: application/octet-stream' \
     -H "Content-Length: $(stat -f%z .pio.nosync/images/revq.bin)" \
     --data-binary @/tmp/half.bin http://wineador.local/api/update; echo
```

Expect: the LCD shows Updating for about 60 s, then `408 stalled`; loads back under normal control;
"Update abandoned: ..., no data for 60 s" logged; the Update page shows `abandoned`. The window is
used up (press Allow update again).

## 3. Corrupt image

Allow update, then `up .pio.nosync/images/revq-corrupt.bin`.
Expect: written to the end, then `422 corrupt_image` (its own check: checksum and SHA-256);
nothing set to boot; "Update failed: ..., the image's own check failed (corrupt)" logged.

## 4. The cabinet build installs

Allow update, then `up .pio.nosync/images/revq.bin`.
Expect: `200 {"api":5,"installed":true,"version":"2.0.0","restarting":true}`; the board restarts
1.5 s later; the boot hold runs; "New firmware running, being checked" logged; after 60 s
"New firmware started properly: 2.0.0 marked good"; the Update page then shows `installed ...
started properly`.

While it is being checked (the first 60 s), Allow update and `up .pio.nosync/images/revq.bin`
again: `409 being_checked`, "try again in N s", with a `Retry-After` header.

## 5. A second upload while one runs

Allow update; start `up .pio.nosync/images/revq.bin` in one Terminal and, while it is sending,
the same in a second: `503 busy` at once. The first carries on.

## 6. The rollback test image rolls back

Only after step 4 (the board must be running an image installed by OTA, with an older one to go
back to). Allow update, then `up .pio.nosync/images/revq_rollback_test.bin`.
Expect: installed, restart; "Rollback test image: every load held off; at 20 s the hardware
watchdog line stops..." logged; about 21 s after boot U5 resets the board (if it didn't, the image
resets itself 10 s later); the bootloader goes back to 2.0.0; "Update rolled back from
2.0.0-ROLLBACK-TEST to 2.0.0: it didn't start properly" logged; the Update page shows
`rolled_back`.

**Never by USB.** Flashed by mistake, it finds nothing to roll back to, holds every load off and
says so ("not sent by OTA ... Flash a normal build"); flash `revq` again.

## Record

The worst loop pass during an update (status line `loop max`, and the readings rows' `loop_max_ms`
while Updating); the time each upload took; each reply and its event.
