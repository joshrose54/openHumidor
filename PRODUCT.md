# Product

<!-- impeccable:product-schema 1 -->

## Platform

web

## Stack

Plain HTML, CSS and JavaScript: no framework. Graphs use **uPlot** (MIT), a pinned copy kept in
the repo and served from flash with the page (Josh, Q5 plan). The pages are stored compressed in
the ESP32-S3's flash and served by the firmware; nothing is loaded from the internet.

## Users

- **Now:** Josh, the owner and builder of the wineador (an electriQ wine cooler converted into a
  cigar humidor with a custom controller). He is not a professional developer. He uses the
  dashboard on his phone and on his Mac, at home.
- **Later:** other people who build an openHumidor cabinet of their own. So nothing in the
  dashboard should depend on Josh personally (his name, his particular cabinet) beyond what the
  firmware reports.
- Anyone on the home network can open the pages (no password, spec 5.5), but they are not the
  people it is designed for.

## Product Purpose

The web dashboard is the full view of the controller. It shows everything that doesn't fit on the
cabinet's LCD (spec 5.5): readings now and over time, what each load is doing and why, sensor
health, events, log downloads and OTA updates. It is also the place to change the **user**
settings.

It has four jobs, all of them confirmed:
1. **Quick check on a phone:** is everything all right, what are the temperature and RH, are
   there any alerts.
2. **Looking at trends on the Mac:** graphs over an hour, day, week and month; compressor and
   humidifier duty; how well the setpoints are held; RH stripping, crossovers, humidifier runs,
   disturbances.
3. **Something is wrong:** find which sensor or load is the cause, and read the events.
4. **Maintenance:** exclude a sensor by hand, download logs, send an OTA update.

It succeeds when a glance gives the answer, and a problem can be traced to its cause without
opening the serial console.

## Positioning

It is the controller's own dashboard, served by the board on the local network, and it works with
no internet and no cloud. The figures come straight from the firmware that runs the cabinet, so
they are the real state, not a copy. Control never depends on the dashboard or WiFi: the pages
only show the state and change the user settings.

## Operating Context

- Reached at `wineador.local` on the home WiFi only; plain http; no access from outside.
- The page fetches new figures every **30 s**.
- The cabinet's LCD (480 × 320) is the walk-past view and the place to press **Allow update**.
  The dashboard can send an OTA image only inside that 5-minute window, so an update means using
  the LCD and then the browser.
- Mac-side tools (`tools/`) use the same JSON API as the dashboard.
- Measured values, serials and calibration live in `docs/wineador-build-log.md`; the design is in
  `docs/wineador-spec.md` (chapter 5.5 for the dashboard and the API).

## Capabilities and Constraints

From spec 5.5 (the spec wins over this file):
- **Seven pages:** Overview (now) · Graphs · Sensors (health per channel, and the manual exclude) ·
  Events · Settings (user settings to change, tuning settings read-only) · Logs (downloads) ·
  Update (OTA).
- **What it shows:** current temperature and RH for all six sensors (channels **0–5**, named by
  position and probe label); cabinet mean, min, max, spread; compressor, heater, humidifier, fan
  PWM and RPM, interlock; graphs (hour and day from the readings, week and month from the hourly
  rows); compressor and humidifier duty; time since the last compressor stop and any guard
  blocking; current zone; last week's crossovers, RH stripping events, humidifier runs and
  disturbance detections; active faults; sensor health.
- **Settings in three tiers:** user settings can be changed (within their bounds and steps:
  0.5 °C and 1 point for the setpoints); tuning settings are shown read-only; **safety values are
  never shown**.
- **OTA:** accepted only inside the LCD's Allow update window, and only for a Rev Q cabinet build;
  the firmware refuses anything else before writing.
- **Limits:** small and compressed in flash; at most **2 browser connections** at once; log
  downloads sent in pieces; the firmware's 50 ms loop rule and 30 KB heap floor apply to serving.
- **No internet:** no web fonts, CDN scripts or chart libraries; system fonts or files stored on
  the board.
- **Phone and computer** both.
- The JSON API is documented in the repo and versioned; the pages use it like any other client.
- **What the API accepts and refuses** is plain C++ in lib/core with native tests (stage Q5).
- Terminology follows the spec and the LCD: Cooling, Heating, Humidifying, Drying, Standby,
  Starting, Updating, Fault; "Allow update"; channels 0–5.
- **Name:** the pages show **openHumidor** (Josh). The board's network name stays
  `wineador.local` (spec 5.5).

## Brand Commitments

- **Name: openHumidor**, written as one word: "open" in a light weight, "Humidor" in bold.
- **Logo: `docs/brand/openhumidor-logo.svg`** (from Josh). It shows the wordmark over a line that
  starts as a damped wave and settles, at a dot, into a flat line: control settling onto its
  setpoint. It is drawn as white outlines (no font needed), 302 × 84, for a dark background.
  Use it as supplied; don't redraw it.
- **Same colours and icons as the LCD** (spec 5.5). The palette is `lib/lcdgfx/src/lcdgfx/palette.h`,
  approved by Josh at stage Q4; the icons are Material Symbols Rounded (Apache 2.0) and the LCD
  font is Inter (SIL OFL 1.1), licences in `lib/lcdgfx/LICENSES`.
- Words are plain and short, as on the LCD and in the spec.

## Evidence on Hand

- The LCD screens as PNGs: `pio test -e native -f test_lcd_screens` writes them to
  `.pio.nosync/lcd-screens/`.
- Real readings: the B4 soak logs (`logs/`), checked by `tools/check_logs.py`.
- No board to run the pages on until Rev Q arrives: they are reviewed from a native or Mac-side
  build first (CLAUDE.md, stage Q5).
- No users other than Josh yet. No screenshots or examples from other builders; don't invent them.

## Product Principles

1. **The answer at a glance.** Is the cabinet all right? The first view on a phone says so before
   anything else.
2. **Show what is really happening.** Loads that are actually running, guards that are actually
   holding, readings with their age. Never a guess presented as a fact; "--" when a figure isn't
   known.
3. **A fault leads to its cause.** Every alert can be followed to the sensor, load or event behind
   it.
4. **Safe to touch.** Only the user settings can be changed, always within their bounds; safety
   values never appear; OTA only inside the LCD's window.
5. **Small and self-contained.** Everything comes from the board, works without internet, and fits
   the flash and memory limits.

## Accessibility & Inclusion

No needs beyond good general practice: readable text, enough contrast, works with phone zoom.
Mode colours always come with an icon or a word as well (they come from the LCD, where colour
is never the only signal).
