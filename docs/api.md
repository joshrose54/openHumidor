# openHumidor dashboard API

API version 5

**Changes in version 5** (the words sweep, spec 5.5 "Words on screen"): a humidity difference's
tuning `unit` is `"%"`, no longer `"points"` (`humidify_band_pts`, `humid_ineffective_rise_pts`,
`blower_boost_above_rh_pts`, `blower_idle_below_rh_pts`; the names keep `_pts`). The tuning
`label`s, the alert titles, the `message`s and the event `sentence`s are reworded too; those are
words for the page, not fields, so they alone would not change the version.

**Changes in version 4** (stage Q6, the blower and ion generator rules): `loads.blowers` and
`loads.ion` in `GET /api/status` rewritten (`mode`, `why`, the spread and the idle countdown; the
ion generator's `state`, its next run and the last skipped run), both null on a board without them;
`ion_runs_per_day`, `ion_minutes_per_run` and `ion_minutes_per_day_max` in the user settings
(`ion_enable` no longer has `built`); the runs and minutes as writes, with the error code
`ion_too_much`; six blower tuning settings.

**Changes in version 3** (stage Q5d, OTA): `phase` and `verify_s` in `GET /api/update`; the
upload installs (no more `not_built`); error codes `being_checked`, `corrupt_image` and
`flash_failed`.

**Changes in version 2** (stage Q5b): `ion_enable` in the user settings (read and write); the
`board` object in `GET /api/status`; `source`, `note` and `sentence` in `GET /api/events`.
Stage Q5c added the error code `no_answer` and the save limit `too_soon` (with `Retry-After`),
each with the usual error body.

The JSON endpoints the board serves for the web dashboard and for tools on the Mac (spec 5.5).
Every endpoint and every field is listed here. The firmware's own constant (`kApiVersion` in
`lib/core/src/wineador/web_api.h`) must match the version above: the native test
`test_web_api` fails if this document and the firmware disagree on the version, the endpoints,
or the codes and names below.

**The version changes whenever any endpoint changes** — a field added, renamed or removed, a
new endpoint, a changed meaning. Every JSON reply carries it as `"api": 3`, so a Mac tool can
check it and stop with a clear message rather than misread a newer reply.

The board answers on the home network only, as `http://wineador.local/` (plain http). Rev Q
only (Rev N, retired, had no dashboard).

## General rules

- **Replies** are JSON (`application/json; charset=utf-8`), except the page itself and log
  file downloads. Text is UTF-8; `°C` is written with the degree sign.
- **Numbers:** temperatures in °C, RH in % (corrected: gain × raw + offset, spec 5.3), times as
  whole seconds. Trailing zeros are dropped (`20` not `20.0`).
- **`null`** means "not known now": a reading the LCD shows as "--", a time while the clock isn't
  trusted, a count not yet made after a boot.
- **Times:** `utc_s` and `utc` fields are UTC (seconds since 1970, or ISO 8601 with a trailing Z,
  as in the logs). Every time shown to a person comes from the board as **UK local time**
  (Europe/London, summer time included; spec 5.5) in the `uk` fields — never the browser's own
  zone.
- **Writes are JSON only.** Every `POST` except the firmware upload must send
  `Content-Type: application/json` and one flat JSON object (names and plain values; no nested
  objects or arrays), at most 1,024 bytes. Anything else is refused with `not_json` or
  `bad_json`. The board grants no other website permission to send to it (no CORS headers), so a
  web page open elsewhere can't change a setting: browsers won't send a JSON body to another site
  without that permission. There is no password (spec 5.5).
- **All or nothing:** a write with any refused field changes nothing.
- **At most one saved change every 10 s** (spec 5.5): a settings change or manual exclude that
  comes sooner after the last one saved from the dashboard is refused with `too_soon` (the wait
  in its message and in a `Retry-After` header, whole seconds). The values
  are checked first (a wrong value is told as such); a write that changes nothing isn't refused
  or counted. The door test isn't limited, nor are the LCD and the serial console.
- **Connections:** at most **2** browser connections at once; every reply closes its connection.
  A third connection is closed at once by the board: try again a few seconds later.
- **One long transfer at a time:** a log download or a firmware upload. Another is refused with
  `busy` at once (its body isn't read: an upload sent anyway sees the reply, then the connection
  cut); a new one is refused with `low_memory` while free internal RAM is near the 30 KB floor
  (the margin is set at BP2 test 39).
- **Methods:** `GET` reads, `POST` writes. Another method on a known path gives
  `method_not_allowed`; an unknown path gives `not_found`.

## Endpoints

| Endpoint | What | Page |
|---|---|---|
| `GET /` | The dashboard page (all seven pages in one file) | — |
| `GET /api/info` | Who the board is: build and version | Update, tools |
| `GET /api/status` | Everything now: readings, modes, loads, alerts, sensors | Overview, Sensors, every header |
| `GET /api/history` | Graph columns for an hour, day, week or month | Graphs |
| `GET /api/events` | Events, 50 at a time, newest first | Events |
| `GET /api/settings` | User settings with bounds; tuning, read only | Settings |
| `GET /api/logs` | The card and its log files | Logs |
| `GET /api/logs/file` | One log file, downloaded | Logs |
| `GET /api/update` | The Allow update window, the running build, the last update | Update |
| `POST /api/settings` | Change user settings | Settings |
| `POST /api/sensors/exclude` | Exclude a sensor by hand, or bring it back | Sensors |
| `POST /api/door-test` | Log a door test | Events |
| `POST /api/update` | Send a firmware image | Update |

## Errors

A refused request gets an HTTP status and this body:

```json
{"api":5,"error":"off_step","field":"target_temp_c","message":"Target temperature goes in steps of 0.5°C.","min":15,"max":22,"step":0.5}
```

`field` is present when the refusal is about one field; `min`, `max` and `step` with
`out_of_bounds` and `off_step`. `message` is a plain sentence for the page, in spec 5.5's form: what
happened, then what to do ("Too soon. Try again in 7 s."). `error` is one of:

| `error` | Status | When |
|---|---|---|
| `not_found` | 404 | No such endpoint, or no such log file |
| `method_not_allowed` | 405 | Another method on a known path |
| `not_json` | 415 | A write not sent as `application/json` |
| `bad_json` | 400 | Not one flat JSON object of at most 1,024 bytes |
| `empty` | 400 | Nothing to change (`{}`), or a required field missing |
| `unknown_field` | 400 | Not a field this endpoint takes. Safety values are never named anywhere, so they are simply unknown |
| `read_only` | 400 | A tuning setting: shown, but changed only on the B6 tuning dashboard |
| `wrong_type` | 400 | e.g. text where a number belongs |
| `out_of_bounds` | 400 | Outside the setting's bounds |
| `off_step` | 400 | Not a whole number of the setting's steps |
| `unknown_channel` | 400 | No such sensor |
| `too_long` | 400 | A text value too long (the door-test note: 120 bytes) |
| `bad_query` | 400 | A query value missing or wrong (e.g. `range=year`) |
| `bad_path` | 400 | Not a log file on the card |
| `updating` | 409 | An OTA update is running: settings and excludes wait until it has finished (spec 5.5) |
| `compressor_running` | 409 | Upload refused: an update never cuts a cooling run short (spec 5.2) |
| `already_updating` | 409 | An update is already running |
| `window_closed` | 403 | Upload outside the LCD's Allow update window |
| `busy` | 503 | Another long transfer is running |
| `low_memory` | 503 | Free internal RAM is near the floor |
| `too_big` | 413 | The upload is larger than the app slot |
| `wrong_image` | 422 | The image isn't a Rev Q cabinet build; `image` gives the reason (below) |
| `stalled` | 408 | No upload data for 60 s: the half-written image is discarded (spec 5.2) |
| `card_unavailable` | 503 | The SD card is failed or out (logs and downloads; events are then served from RAM) |
| `no_answer` | 503 | The control loop didn't answer a write within 3 s (it answers on its next pass, a few ms; not expected) |
| `too_soon` | 429 | A settings change or manual exclude less than 10 s after the last change saved from the dashboard (spec 5.5); `message` says how long to wait ("Try again in 7 s."), and the reply carries the same wait as a `Retry-After` header in whole seconds (`Retry-After: 7`) for tools |
| `being_checked` | 409 | Upload refused: the image now running was installed by the last update and hasn't yet started properly (spec 5.2); `message` and `Retry-After` say how long to wait |
| `corrupt_image` | 422 | The whole image arrived but failed its own check (checksum, SHA-256): nothing changed |
| `flash_failed` | 500 | Writing the image to flash failed: nothing changed |
| `ion_too_much` | 400 | Ion generator runs per day × minutes per run over 10 (spec 5.5, 4.6c): "at most 10 minutes a day in total"; `field` is the minutes (or the runs, when only they were sent) |

## Reads

### `GET /`

The dashboard page: one compressed file holding all seven pages (spec 5.5). Not JSON.

### `GET /api/info`

Who the board is. Small: for tools, and for the Update page to see the new version after a
restart.

```json
{"api":5,"name":"openHumidor","build":{"board":"rev-q","profile":"real","test":false,"version":"2.0.0"},"uptime_s":3725,"boot_reason":"POWERON"}
```

- `build.board`: `"rev-q"`. `build.profile`: `"real"` or `"bench"`. `build.test`: a test build
  (scripted readings). `build.version`: spec B3's version string, e.g. `"2.0.0"`,
  `"2.0.0-BENCH-TEST"`.
- `boot_reason`: as the boot event logs it (spec 3A.11: an EN reset reads `POWERON`).

### `GET /api/status`

Everything the Overview and Sensors pages and every page's header show, in one reply (about
2 KB). The page asks for it every 30 s while Overview or Sensors is open and the tab is visible
(spec 5.5).

```json
{"api":5,
 "time":{"utc_s":1794573120,"uk":"12:32","uk_date":"2026-11-13","trusted":true},
 "build":{"board":"rev-q","profile":"real","test":false,"version":"2.0.0"},
 "uptime_s":7200,
 "modes":["cooling"],"fault":null,"boot_hold_s":0,
 "cabinet":{"t":20.6,"rh":62.1,"t_min":20.3,"t_max":20.9,"spread_t":0.6,"control_t":20.6,"zone":"cool","healthy":4,"age_s":7},
 "setpoints":{"t":20,"rh":62},
 "loads":{"compressor":{"on":true,"stripping":false,"for_s":420,"stopped_ago_s":1830},
          "heater":{"on":false,"duty_pct":0},"humidifier":{"on":false},
          "fan":{"pwm":40,"rpm":1230},"interlock":"cooling",
          "blowers":{"pwm":100,"mode":"boost","why":null,"spread_t":0.42,"spread_rh":1.3,"idle_in_s":215,"idle_is_full":true},
          "ion":{"on":false,"state":"waiting","why":null,"left_s":null,"next":"12:00","next_in_s":null,
                 "last_skipped":{"why":"boot hold","at":"00:00"}}},
 "guards":[{"load":"heater","reason":"crossover","left_s":120}],
 "alerts":[{"title":"Excluded: Middle rear","since":"12:00","rank":3,"fault":false}],
 "sensors":[{"ch":0,"name":"top_front","probe":"09ED","role":"cabinet","state":"healthy","reason":null,
             "t":20.5,"rh":62,"rh_raw":63.4,"age_s":7,"crc_hour":0,"timeouts_hour":0}],
 "week":{"crossovers":2,"strips":1,"humid_runs":14,"disturbances":null,"ready":true},
 "update":{"window_open":false,"window_s":0,"updating":false,"pct":0},
 "card":{"ok":true,"free_bytes":59000000000},
 "wifi":{"rssi":-61},
 "board":{"version":"2.0.0","uptime_s":7200,"restart_reason":"POWERON","v12_now":12.08,"v12_low_today":11.96,
          "wifi_rssi":-61,"heap_free_bytes":182000,"card_free_bytes":59000000000,"card_total_bytes":59700000000}}
```

(`sensors` has all six channels; one is shown.)

- `time.uk`: UK local time, `"--:--"` while the clock isn't trusted (`trusted` false; then
  `uk_date` is null). Same rule as the LCD.
- `modes`: the LCD's mode icons, from the LCD's own logic, so the two always agree (spec 5.5):
  any of `cooling`, `heating`, `humidifying`, `drying` (RH stripping, instead of cooling),
  `standby`, `starting` (the boot hold; `boot_hold_s` counts down), `updating` (an OTA update;
  `update.pct`), `fault`. `fault` replaces every other icon; then `fault` (the field) is the
  most serious alert's title, otherwise null.
- `cabinet`: the mean of the healthy cabinet sensors (ch0–3). **All null** — the LCD's "--" — when
  fewer than 3 are healthy or there's no fresh reading. `t_min`/`t_max` are the coldest and
  warmest healthy cabinet sensors; `spread_t` their difference; `control_t` the control
  source (spec 4.2); `zone` where it sits (spec 4.5): `"cool"` above setpoint + cooling
  hysteresis, `"heat"` below setpoint − heating deadband, `"dead"` between. `healthy`: how many
  of the four; `age_s`: since the last sensor read.
- `loads`: what is actually running (the outputs, not the calls). `compressor.for_s` while it
  runs; `stopped_ago_s` since its last stop (null before its first stop since boot).
  `heater.duty_pct` is its PWM cap while on. `interlock`: the side K1 feeds. `blowers` and
  `ion` are null on a board without them (none now; Rev N, retired, had neither).
- `loads.blowers` (spec 4.6b): `pwm` the duty driven now (100 = full speed). `mode`: `"idle"` (the
  idle duty), `"boost"` (full speed: the spread was wide, or the rule has just come back from full
  speed, and it waits for 10 min of low spread), `"full"` (full speed: `why` is `"fault"`,
  `"no fresh reading"` or `"no reading yet since boot"`; otherwise `why` is null), `"updating"`
  (the idle duty during an OTA update, a fault included). `spread_t` (°C) and `spread_rh`
  (%): highest minus lowest of the healthy cabinet sensors at the last sweep the rule judged.
  `idle_in_s`: in boost with the spread low, seconds until idle if it stays low (null otherwise).
  `idle_is_full`: the idle duty is still full speed (until BP2 test 22 chooses it).
- `loads.ion` (spec 4.6c): `on` the output now. `state`: `"running"` (`left_s` to go),
  `"waiting"` (the next run: `next`, UK local `"12:00"`, from the clock; or `next_in_s` when the
  time isn't trusted and the runs count from boot — one of the two, the other null), `"held"`
  (blocked now, `why`: `"boot hold"`, `"update"` or `"fault"`; a run due now is skipped),
  `"off"` (the enable switched off), `"no_runs"` (runs per day or minutes per run 0),
  `"starting"` (the first moments after a boot). `last_skipped`: the last run that came due and
  was skipped (`why`: `"boot hold"`, `"update"`, `"fault"` or `"at most 10 minutes in 24 hours"`;
  `at` its slot, UK local, null when it counted from boot), or null.
- `guards`: each load that is asked for but held, why (`reason`, the guard's name as in the
  events), and how long until it may start (`left_s`, null if there is no countdown).
- **Codes and words.** `reason` (guards and sensors), `name` (a sensor's position),
  `restart_reason`, the blowers' and the ion generator's `why`, and an event's `type` are codes,
  as the logs write them; the pages show a word for each (spec 5.5 "Words on screen": `"min-off"`
  is "rest", `"mid_rear"` "Middle rear", `"POWERON"` "Power on or reset"). The words are defined
  once in the firmware (`lib/core/src/wineador/words.cpp` and the sensor table) and reach the page
  as `web/words.js`, made by `tools/make_web_words.py`; the native test `test_web_words` fails if
  it is out of date. The alert titles, the messages and the event sentences are already words.
- `alerts`: every active alert, most serious first — the LCD's alerts screen. `since`: `"14:32"`
  today, `"13 Nov 14:32"` earlier, `"for 12 min"` while the clock isn't trusted. `rank` 0 is a
  fault.
- `sensors`: each channel 0–5 by number, position name and probe label; `role` `"cabinet"`,
  `"chamber"` or `"ambient"`. `state`: `"healthy"`, `"excluded"` (`reason` says why, as the
  logs do: `"crc"`, `"absent"` …), `"manual"` (excluded by hand), `"not_read"` (before the
  first read). `t`, `rh`, `rh_raw` (before calibration) are null when the last read failed.
  `crc_hour`, `timeouts_hour`: this hour's counts.
- `week`: the last 7 days (168 hours), counted from the card's event files at boot, then kept up
  live (spec 5.5). All null until the boot count has finished (`ready` false). What counts, by
  the event wording in `event_text.h`: `crossovers` — each `crossover` event; `strips` — each
  `strip` event starting `RH STRIP START`; `humid_runs` — each time the humidifier switched on
  (a `humidifier` event starting `humidifier ON`); `disturbances` — each `disturbance` event, and
  null while disturbance detection is off (until B6). Events whose time wasn't trusted are left
  out: they can't be placed in the week.
- `update`: the Allow update window (`window_s` seconds left) and an update in progress.
- `card.ok`: the SD card is mounted and working. `wifi.rssi`: signal strength in dBm.
- `board`: the Overview's Board section (spec 5.5). `version` the firmware; `uptime_s` since the
  last restart; `restart_reason` as the boot event logs it (spec 3A.11: an EN reset reads
  `POWERON`); `v12_now` the 12V supply's last 1 s average and `v12_low_today` its lowest 1 s
  average since UK midnight (or since the restart, if later), both null without the 12V sense;
  `wifi_rssi` in dBm; `heap_free_bytes` free internal RAM now (spec 5.2: never below 30 KB);
  `card_free_bytes`, `card_total_bytes` the SD card, null while it is failed or out.

### `GET /api/history?range=hour|day|week|month&since=<utc_s>`

The graphs (spec 5.5): hour and day from the readings, week and month from the hourly rows.
Columns ready for uPlot: `t`, then one array per line, all the same length.

```json
{"api":5,"range":"day","step_s":60,"now":1794573120,
 "duty":{"compressor":0.231,"heater":0,"humidifier":0.05},
 "t":[1794486780,1794486840,...],
 "cab_t":[19.82,19.85,...],"cab_t_min":[...],"cab_t_max":[...],"cab_rh":[...],
 "ch0_t":[...],"ch1_t":[...],"ch2_t":[...],"ch3_t":[...],"ch4_t":[...],"ch5_t":[...],
 "ch0_rh":[...],"ch1_rh":[...],"ch2_rh":[...],"ch3_rh":[...],"ch4_rh":[...],"ch5_rh":[...],
 "compressor":[...],"heater":[...],"humidifier":[...],"fan":[...]}
```

- `range` (required): `hour` — every readings row of the last hour (15 s while the compressor or
  heater runs, 60 s otherwise); `day` — one point a minute for the last 24 hours, the mean of
  that minute's rows; `week` — the hourly rows of the last 7 days; `month` — of the last 31 days.
  `step_s` is the usual spacing (15, 60, 3600, 3600).
- `since` (optional): only points after this time. The hour graph asks for its newest points
  every 30 s this way.
- `t`: each point's time (UTC seconds; a minute's or an hour's start).
- The columns: `cab_t` cabinet mean; `cab_t_min`, `cab_t_max` the coldest and warmest cabinet
  sensor (over the minute, or the hour); `cab_rh` cabinet mean RH; `ch0_t`, `ch1_t`, `ch2_t`,
  `ch3_t`, `ch4_t`, `ch5_t` and `ch0_rh`, `ch1_rh`, `ch2_rh`, `ch3_rh`, `ch4_rh`, `ch5_rh` each
  sensor, drawn only while its flag in the readings row was `ok` (null otherwise, so a failing
  sensor's numbers never reach the graph); `compressor`, `heater`, `humidifier` the fraction of
  the time each ran (a readings row: 0 or 1). The heater is shown as on or off, not as its PWM
  duty. `fan` the plenum fan's duty, %.
- **Breaks:** where points are further apart than 5 min (hour), 3 min (day) or 3 h (week,
  month) — a power cut, a card outage — one row of nulls sits between them, its `t` halfway,
  so the graph shows a gap rather than a line across it.
- `duty`: the fraction of the time each load was on over the whole range (spec 5.5, "duty over
  range") — with `since` too, so the page can always show it: hour by time (each row counts
  until the next, at most 120 s), day and week/month by minute and hour. Null when there are no
  points.
- With `since`, a break between the newest point the page already has and the first new one is
  sent too (its null row comes first).
- After a boot the history refills from the card in the background; until then the graphs hold
  only what has been logged since the boot. Rows whose time isn't trusted (`time_trusted` 0) are
  left out: they can't be placed on a time axis.
- The month reply is about 80 KB; it is sent in pieces.

### `GET /api/events?n=50&before=<cursor>|after=<cursor>`

Events (spec 5.4), newest first, 50 at a time (spec 5.5).

```json
{"api":5,"source":"card","note":null,
 "events":[{"utc":"2026-11-13T14:32:05Z","uk":"13 Nov 2026 14:32:05","trusted":true,"type":"settings",
            "sentence":"Target temperature 20.0 → 20.5°C (dashboard)",
            "detail":"settings changed (dashboard): target_temp_c 20.0 -> 20.5"}],
 "older":"/logs/2026/2026-11-events-s2.csv@81234",
 "newest":"/logs/2026/2026-11-events-s2.csv@90412"}
```

- No cursor: the newest `n` (at most 50).
- `before=<older>`: the next page back, through the card's monthly event files in order (spec
  5.4: by month, then schema). `older` is null when there is nothing older.
- `after=<newest>`: only events written since (the Events page checks every 30 s).
- A cursor is a file and a byte offset, `"<path>@<offset>"`; pass it back unchanged
  (URL-encoded).
- `trusted` false: the time was written while the clock wasn't trusted.
- `sentence`: the event as a plain sentence (spec 5.5), e.g. "Humidifying started: humidity 59.4%,
  below 60.0%", made by the board from `type` and `detail` (`lib/core/src/wineador/event_sentence.h`;
  every event type has one). `detail` is the text as logged on the card, which tools read; the
  page shows it on hover or tap.
- `source`: `"card"` normally. **While the SD card is failed or out** the reply is the newest
  events kept in RAM since the boot (at most 50, newest first), with `"source":"ram"`,
  `"note":"card unavailable: recent events only"`, and `older` and `newest` null (there is
  nothing to page through; cursors sent are ignored). The page shows the note.
- `type` and the start of `detail` use the event wording defined once in
  `lib/core/src/wineador/event_text.h` (also in `tools/event_text.json`): e.g. a humidifier run
  is a `humidifier` event starting `humidifier ON`.

### `GET /api/settings`

The settings page (spec 5.5): the user settings with their bounds and steps, and the tuning
settings, read only. **Safety values are never sent** (spec 5.5).

```json
{"api":5,
 "user":{"target_temp_c":{"value":20,"min":15,"max":22,"step":0.5,"unit":"°C"},
         "rh_setpoint_pct":{"value":62,"min":55,"max":70,"step":1,"unit":"%"},
         "heater_enable":{"value":true},
         "humidifier_enable":{"value":true},
         "ion_enable":{"value":true},
         "ion_runs_per_day":{"value":2,"min":0,"max":4,"step":1},
         "ion_minutes_per_run":{"value":2,"min":0,"max":5,"step":1,"unit":"min"},
         "ion_minutes_per_day_max":10},
 "manual_exclude":[],
 "tuning":[{"name":"cooling_hysteresis_c","label":"Cooling starts at target +","value":0.5,"unit":"°C"}]}
```

- `ion_enable`: the ion generator may run (spec 5.5, 4.6c "Switched off": off means no runs at
  all; the run settings keep their values).
- `ion_runs_per_day` (0–4, in fixed UTC slots 24/N hours apart from 00:00) and
  `ion_minutes_per_run` (0–5): spec 4.6c. Either at 0 means no runs. Together at most
  `ion_minutes_per_day_max` (10) minutes a day: runs × minutes.
- `manual_exclude`: the channels excluded by hand (change them with `POST /api/sensors/exclude`).
- `tuning`: each with `name`, `label`, `value` (true/false for on/off settings) and `unit`.
  The names, in this order: `cooling_hysteresis_c`, `heating_deadband_c`,
  `heating_stop_below_c`, `crossover_delay_min`, `fan_idle_pct`, `fan_compressor_pct`,
  `fan_cool_harvest_start_pct`, `fan_cool_harvest_end_pct`, `fan_heating_pct`,
  `harvest_convergence_c`, `harvest_backstop_min`, `dosed_control`, `rh_strip_enable`,
  `rh_strip_offset_pct`, `rh_strip_persist_min`, `rh_strip_floor_c`, `humidify_band_pts`,
  `humidify_persist_min`, `humidify_during_cooling`, `heater_pwm_cap_pct`,
  `heater_ineffective_rise_c`, `heater_ineffective_window_min`, `heater_ineffective_retry_min`,
  `cooling_ineffective_fall_c`, `cooling_ineffective_window_min`, `humid_ineffective_rise_pts`,
  `humid_ineffective_window_min`, `humid_ineffective_retry_min`, `disturbance_detection`,
  `log_steady_s`, `log_active_s`, `heater_pwm_period_s`, `sensor_read_s`, then the blowers (spec
  4.6b): `blower_idle_pct` (100, full speed, until BP2 test 22 chooses it), `blower_boost_above_c`,
  `blower_boost_above_rh_pts`, `blower_idle_below_c`, `blower_idle_below_rh_pts`,
  `blower_idle_hold_min`.

### `GET /api/logs`

The Logs page: the card and every log file on it, newest first.

```json
{"api":5,"card":{"ok":true,"free_bytes":59000000000,"total_bytes":60000000000},
 "files":[{"path":"/logs/2026/2026-11-readings-s2.csv","name":"2026-11-readings-s2.csv","kind":"readings","month":"2026-11","schema":2,"part":1,"bytes":4567890},
          {"path":"/logs/hourly-s2.csv","name":"hourly-s2.csv","kind":"hourly","month":null,"schema":2,"part":1,"bytes":12345}]}
```

- `kind`: `readings`, `events` or `hourly`; `schema` from the name (`-s2`; none = 1); `part`
  from `-p2` … (none = 1). Only log files are listed.
- Refused with `card_unavailable` while the card is failed or out.

### `GET /api/logs/file?path=<path>`

Downloads one log file, as CSV, in pieces (it can take a few minutes for a month of readings,
spec 5.5). `path` must be one from `GET /api/logs`: `/logs/hourly…csv`, or
`/logs/<year>/<year>-<month>-readings…csv` / `-events…csv` with the folder's year matching the
name's. Anything else is refused with `bad_path`; a log file not on the card with `not_found`.
A long transfer (above). Not JSON.

### `GET /api/update`

The Update page.

```json
{"api":5,"window_open":true,"window_s":212,"compressor_running":false,"updating":false,"phase":"idle","pct":0,
 "slot_bytes":3342336,"check_bytes":352,
 "build":{"board":"rev-q","profile":"real","test":false,"version":"2.0.0"},
 "pending_verify":false,"verify_s":null,
 "last":{"result":"installed","detail":"2.0.0 (was 1.9.0)","utc_s":1794573120}}
```

- `window_open`, `window_s`: the LCD's Allow update window and its seconds left. The page counts
  down by itself from this (spec 5.5).
- `slot_bytes`: the largest image accepted. `check_bytes`: how much of the start of an image
  the board reads to check it (the page reads the same bytes to warn early).
- `updating`, `phase`, `pct`: an update in progress: `phase` is `idle`, `writing` (with `pct` of
  the image written) or `restarting` (written and checked; the board restarts in a moment).
- `build`: the image running now. `pending_verify`: it hasn't yet "started properly" (spec 5.2);
  a reset now would roll back. `verify_s`: while pending, the seconds until a further upload may
  be sent (to 60 s once only the 60 s are left, otherwise to the 4 min 30 s deadline); null otherwise.
- `last.result`: `none`, `installed`, `refused`, `abandoned` (stalled or cut off), `rolled_back`,
  `failed` (corrupt, a flash write, or interrupted by a reset or power cut); `detail` says which,
  with the versions, in the page's words (`"2.0.0 (was 1.9.0)"`, `"2.0.0: upload stalled"`,
  `"2.0.0 didn't start; back on 1.9.0"`); `utc_s` when (null if unknown). Kept across restarts
  (NVS), so a record written by older firmware keeps its older wording.

## Writes

### `POST /api/settings`

Changes user settings (spec 5.5). Any of these, all or nothing:

| Field | Value | Bounds, step |
|---|---|---|
| `target_temp_c` | number | 15–22 °C, steps of 0.5 |
| `rh_setpoint_pct` | number | 55–70 %, steps of 1 |
| `heater_enable` | true / false | |
| `humidifier_enable` | true / false | |
| `ion_enable` | true / false | |
| `ion_runs_per_day` | whole number | 0–4 |
| `ion_minutes_per_run` | whole number | 0–5; runs × minutes at most 10 (judged on the settings as they would be, so one field alone is checked with the other as it is now) |

```json
{"target_temp_c": 20.5}
```

Accepted: the reply is the whole settings, as `GET /api/settings`. The change is applied by the
control loop on its next pass and logged as a settings event with the old and new values, "from
dashboard" (spec 5.4). Refused: `out_of_bounds`, `off_step`, `wrong_type`, `unknown_field`,
`read_only` (a tuning name), `empty`, `bad_json`, `not_json`, `updating`, `too_soon` (within 10 s
of the last change saved from the dashboard), `ion_too_much` (runs × minutes over 10).

### `POST /api/sensors/exclude`

Excludes a sensor by hand for maintenance, or brings it back (the manual sensor override, a user
setting).

```json
{"channel": 3, "excluded": true}
```

- `channel`: the number 0–5, or as on the serial console the position name (`"mid_rear"`),
  `"ch3"`, or the probe label (`"A1C5"`), any case.
- The reply echoes what it acted on, as the serial console does:
  `{"api":5,"ch":3,"name":"mid_rear","probe":"A1C5","excluded":true}`.
- Excluding enough sensors to leave fewer than 3 cabinet sensors causes a Fault (spec 4.3): the
  page asks first; the board doesn't refuse it.
- Refused: `unknown_channel`, `wrong_type`, `unknown_field`, `empty`, `updating`, `too_soon` (it
  counts as a saved change: one every 10 s with the settings).

### `POST /api/door-test`

Logs a "door test" event with an optional note (spec 5.5), for disturbance-detection tests.

```json
{"note": "door open 30 s"}
```

`{}` logs it with no note. The note is text of at most 120 bytes (`too_long`), with no control
characters such as newlines (`wrong_type`). Reply:
`{"api":5,"logged":true}`.

### `POST /api/update`

Sends a firmware image (`firmware.bin` from the `revq` build) as the raw request body
(`Content-Type: application/octet-stream`), with its `Content-Length`. A long transfer.

**Before a byte of the body is read** the board refuses it, nothing written and the window left
open (spec 5.5), with: `already_updating`, `window_closed` (Allow update not pressed on the LCD,
or its 5 minutes are up), `compressor_running`, `busy`, `too_big`; and `wrong_image` if it is
shorter than an image's first 352 bytes.

**Then the first 352 bytes are checked** (spec 5.2, 5.5); only a Rev Q cabinet build is
accepted — real profile, not a test build. Anything else is refused with `wrong_image`, nothing
written, the window left open, and `image` saying why:

| `image` | The file is |
|---|---|
| `too_short` | Shorter than 352 bytes |
| `not_firmware` | Not an ESP32 firmware image at all |
| `rev_n_image` | A Rev N image (ESP32 chip) |
| `wrong_chip` | For another chip |
| `no_app_description` | An image, but not an app built by the framework |
| `no_identity` | Carries no openHumidor identity block: a BP2 test sketch, or firmware from before stage Q5 |
| `unknown_identity_layout` | An identity block in a newer layout |
| `wrong_board` | Built for another board |
| `bench_build` | The bench profile (short timers): never for the cabinet |
| `test_build` | A test build (scripted readings): never for the cabinet |

How it is told (`lib/core/src/wineador/image_identity.h`): byte 0 is 0xE9; bytes 12–13 the chip
(0 = ESP32, 9 = ESP32-S3); at byte 0x20 the framework's app description (magic 0xABCD5432); at
byte 0x120, straight after it, the 64-byte identity block every Rev Q build carries from stage Q5d on
(ESP-IDF's `.rodata_custom_desc` section; every board build is checked for it at this place by
`tools/check_identity.py`, and `test_image_check` judges every built image):

| Bytes | Field | Value |
|---|---|---|
| 0–15 | magic | `openHumidor-ID`, NUL-padded |
| 16–23 | board | `rev-q` (or `rev-n`), NUL-padded |
| 24–31 | profile | `real` or `bench`, NUL-padded |
| 32 | test build | 0 or 1 |
| 33 | layout | 1 (this table) |
| 34–35 | API version | little-endian |
| 36–63 | version | spec B3's string, e.g. `2.0.0`, NUL-padded |

Only a positive match is accepted: no block, or any field other than `rev-q`, `real`, 0, layout 1,
is refused.

**Accepted:** the update starts (the window is used up, spec 5.5): on that pass the loop checks
again that the compressor isn't running, then every load goes off as spec 5.2 (blowers at idle,
plenum fan at its idle setting, no sensor sweeps, no fan-stall check), the LCD shows Updating
with its %, and the image is written in 4 KB pieces, one per control-loop pass, so the hardware
watchdog line changes between any two flash steps. Then:

- **No data for 60 s**, or the upload stops short of its `Content-Length`: abandoned (`stalled`),
  the half-written image discarded, normal control at once. If the writing ever stops moving for
  70 s, the loop ends the update by itself.
- **The image's own check** (checksum and SHA-256) at the end: if it fails, `corrupt_image`,
  nothing changed. The window stays used up: press Allow update again.
- **Installed:** the board replies `{"api":5,"installed":true,"version":"2.0.0","restarting":true}`
  and restarts 1.5 s later. The page then asks `GET /api/info` every few seconds until it answers
  with the new version. The normal boot hold applies.
- **The new image** must start properly (spec 5.2: 60 s of the loop, the settings check passed,
  the hardware watchdog toggling, the first sweep of all six sensors finished) to be marked good.
  A reset before then rolls back to the previous image; so does not being marked good within
  4 min 30 s of boot (inside the 5-minute boot hold, so the rollback never cuts a run short).
  `GET /api/update` then shows `rolled_back`. Until it is marked good a further upload is refused
  with `being_checked`.
- **No readings rows are written while Updating** (no sensor sweeps then): the graphs show a
  break, and the update events say why.

## For tools on the Mac

- Read `api` from every reply and stop if it isn't the version the tool was written for.
- Downloads: `GET /api/logs`, then `GET /api/logs/file?path=…` for each file, one at a time.
- Nothing here needs a password, and nothing here can reach a safety value.
