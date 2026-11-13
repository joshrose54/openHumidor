---
name: openHumidor
description: The cabinet controller's own dashboard, in the cabinet LCD's world, served from flash with nothing from the internet.
colors:
  ground: "#14110F"
  ground-raised: "#1B1714"
  surface: "#2A2420"
  surface-hover: "#352E29"
  line: "#3A332E"
  text: "#F3EDE7"
  muted: "#A69C93"
  faint: "#5E5650"
  accent: "#D9A55B"
  accent-hover: "#E4B36B"
  accent-ink: "#1E1609"
  cool: "#62B8EB"
  heat: "#F28C4E"
  humid: "#46C2AE"
  dry: "#E0B850"
  fault: "#EF5A55"
  warn: "#F2B33D"
  fault-ground: "#4A1E1C"
  warn-ground: "#433317"
  zone-heat: "#3A2519"
  zone-cool: "#1A2E3B"
  zone-humid: "#16332E"
  zone-dry: "#352C15"
  zone-dead: "#221D19"
  zone-heat-here: "#5A3420"
  zone-cool-here: "#1F4560"
  zone-humid-here: "#1B4A42"
  zone-dry-here: "#4D3E17"
  zone-dead-here: "#332B25"
  accent-track: "#4E3B22"
typography:
  display:
    fontFamily: "-apple-system, BlinkMacSystemFont, Segoe UI, Roboto, Helvetica Neue, Arial, sans-serif"
    fontSize: "6rem"
    fontWeight: 600
    lineHeight: 1
    letterSpacing: "-0.035em"
    fontFeature: "tnum"
  headline:
    fontFamily: "-apple-system, BlinkMacSystemFont, Segoe UI, Roboto, Helvetica Neue, Arial, sans-serif"
    fontSize: "2rem"
    fontWeight: 600
    lineHeight: 1.2
    fontFeature: "tnum"
  title:
    fontFamily: "-apple-system, BlinkMacSystemFont, Segoe UI, Roboto, Helvetica Neue, Arial, sans-serif"
    fontSize: "1.5rem"
    fontWeight: 600
    lineHeight: 1.2
    letterSpacing: "-0.01em"
  reason:
    fontFamily: "-apple-system, BlinkMacSystemFont, Segoe UI, Roboto, Helvetica Neue, Arial, sans-serif"
    fontSize: "1.2rem"
    fontWeight: 400
    lineHeight: 1.35
  body:
    fontFamily: "-apple-system, BlinkMacSystemFont, Segoe UI, Roboto, Helvetica Neue, Arial, sans-serif"
    fontSize: "16px"
    fontWeight: 400
    lineHeight: 1.45
    fontFeature: "tnum"
  label:
    fontFamily: "-apple-system, BlinkMacSystemFont, Segoe UI, Roboto, Helvetica Neue, Arial, sans-serif"
    fontSize: "0.95rem"
    fontWeight: 500
    lineHeight: 1.45
  display-phone:
    fontFamily: "-apple-system, BlinkMacSystemFont, Segoe UI, Roboto, Helvetica Neue, Arial, sans-serif"
    fontSize: "4.5rem"
    fontWeight: 600
    lineHeight: 1
    letterSpacing: "-0.035em"
    fontFeature: "tnum"
  lead:
    fontFamily: "-apple-system, BlinkMacSystemFont, Segoe UI, Roboto, Helvetica Neue, Arial, sans-serif"
    fontSize: "1.05rem"
    fontWeight: 500
    lineHeight: 1.35
    fontFeature: "tnum"
  text:
    fontFamily: "-apple-system, BlinkMacSystemFont, Segoe UI, Roboto, Helvetica Neue, Arial, sans-serif"
    fontSize: "1rem"
    fontWeight: 400
    lineHeight: 1.45
  small:
    fontFamily: "-apple-system, BlinkMacSystemFont, Segoe UI, Roboto, Helvetica Neue, Arial, sans-serif"
    fontSize: "0.85rem"
    fontWeight: 400
    lineHeight: 1.4
    fontFeature: "tnum"
  tab:
    fontFamily: "-apple-system, BlinkMacSystemFont, Segoe UI, Roboto, Helvetica Neue, Arial, sans-serif"
    fontSize: "0.75rem"
    fontWeight: 400
    lineHeight: 1.2
  mono:
    fontFamily: "ui-monospace, Menlo, monospace"
    fontSize: "0.88em"
rounded:
  hair: "2px"
  sm: "6px"
  md: "8px"
  lg: "10px"
  xl: "12px"
  panel: "14px"
  pill: "999px"
spacing:
  xs: "8px"
  sm: "10px"
  md: "16px"
  lg: "24px"
  xl: "32px"
  xxl: "40px"
components:
  button:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.text}"
    rounded: "{rounded.md}"
    padding: "0 16px"
    height: "40px"
  button-hover:
    backgroundColor: "{colors.surface-hover}"
  button-primary:
    backgroundColor: "{colors.accent}"
    textColor: "{colors.accent-ink}"
    rounded: "{rounded.md}"
    padding: "0 16px"
    height: "40px"
  button-primary-hover:
    backgroundColor: "{colors.accent-hover}"
  button-disabled:
    backgroundColor: "{colors.ground-raised}"
    textColor: "{colors.faint}"
  button-small:
    rounded: "{rounded.md}"
    padding: "0 12px"
    height: "32px"
  input:
    backgroundColor: "{colors.ground-raised}"
    textColor: "{colors.text}"
    rounded: "{rounded.md}"
    padding: "0 12px"
    height: "40px"
  tab:
    textColor: "{colors.muted}"
    rounded: "{rounded.md}"
    padding: "8px 14px"
  tab-active:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.text}"
  segmented:
    backgroundColor: "{colors.ground-raised}"
    rounded: "{rounded.lg}"
    padding: "3px"
  segmented-selected:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.text}"
  chip:
    backgroundColor: "{colors.ground-raised}"
    textColor: "{colors.muted}"
    rounded: "{rounded.pill}"
    padding: "6px 12px"
  chip-selected:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.text}"
  alert-warn:
    backgroundColor: "{colors.warn-ground}"
    textColor: "{colors.text}"
    rounded: "{rounded.lg}"
    padding: "10px 12px"
  alert-fault:
    backgroundColor: "{colors.fault-ground}"
    textColor: "{colors.text}"
    rounded: "{rounded.lg}"
    padding: "10px 12px"
  status-pill:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.accent}"
    rounded: "{rounded.lg}"
    padding: "10px 14px"
  panel:
    backgroundColor: "{colors.ground-raised}"
    rounded: "{rounded.panel}"
    padding: "16px"
  zone-band:
    rounded: "{rounded.xl}"
    height: "52px"
  tag:
    backgroundColor: "{colors.warn-ground}"
    textColor: "{colors.warn}"
    rounded: "{rounded.sm}"
    padding: "2px 8px"
---

# Design System: openHumidor

## Overview

**Creative North Star: "The Cabinet's Own Screen, Opened Up"**

The dashboard is the cabinet LCD's world carried to a phone and a Mac: a warm near-black room, warm off-white type, and one colour for each thing the cabinet can be doing (cooling, heating, humidifying, drying). Nothing is decoration. A colour appears because a load is running, a reading sits in that load's zone, or something needs attention. The palette's single source of truth is `lib/lcdgfx/src/lcdgfx/palette.h`; the web values in this file must match it, and if it changes, this file and `web/app.css` change with it.

The signature is the **zone band**: each reading is a big tabular number, a reason line in words ("Cooling, started above 20.5 °C"), and a horizontal band of its control zones with the set point dashed and a marker at the reading. Lists are ruled rows with no cards. Panels are tonal steps on the ground, not floating boxes. Density is calm: one reading per band, a quiet side column of alerts, loads and the week's counts.

The system works offline by construction. System fonts only, icons vendored as inline SVG, uPlot vendored, everything served compressed from flash.

**Key Characteristics:**
- Dark only (`color-scheme: dark`), warm neutrals from the LCD palette.
- One colour per mode, used for the mode word, its icon, its zone and its graph series.
- Big tabular numerals in one system sans; the reason always in words beside the number.
- Flat tonal layering: ground, raised ground, surface. A single shadow in the whole system (the band marker).
- Material Symbols Rounded, filled, weight 500 (the LCD's icon set), inlined as SVG.
- Phone: the same content in one column, the seven tabs as a fixed bottom bar under 760px.

## Colors

Warm, dim, low-glare neutrals with saturated mode colours used sparingly. Authority: `lib/lcdgfx/src/lcdgfx/palette.h` (kBg, kSurface, kLine, kText, kMuted, kFaint, kCool, kHeat, kHumid, kDry, kAccent, kFault, kWarn, kFaultBg, kWarnBg). Tokens not in that file (`ground-raised`, the hover steps, `accent-ink`, the five zone grounds) are web-only derivations of it and must stay on the same hues.

### Primary
- **Cabinet Amber** (accent): the LCD's kAccent. Starting, Updating, Allow update, Save: the primary button, the selected phone tab, focus rings, a changed setting value, the update countdown, the switch thumb when on, the progress bar, links, text selection. Accent hover is a lighter step (accent-hover); ink on accent is the near-black accent-ink.

### Secondary (the mode colours)
- **Compressor Blue** (cool): cooling, the cool zone, the compressor.
- **Heater Orange** (heat): heating, the heat zone, the heater.
- **Humidifier Teal** (humid): humidifying, the RH line on graphs, healthy-sensor dots, success messages.
- **Strip Gold** (dry): drying (an RH strip), the strip zone.

### Tertiary (attention)
- **Fault Red** (fault) on **Fault Ground** (fault-ground): faults only: the Fault mode word, fault alert rows, refused-action messages.
- **Warning Amber** (warn) on **Warning Ground** (warn-ground): other alerts, a held load's text, stale freshness, untrusted clock times, excluded sensors, the BENCH / TEST tag, the board note strip.

### Neutral
- **Humidor Night** (ground): the page, the top bar, the phone tab bar; also the 1px seam between band zones.
- **Raised Night** (ground-raised): chart and table panels, inputs, segmented control and chip tracks, disabled buttons.
- **Cedar Surface** (surface): buttons, the active tab, selected segments and chips, status pills, the switch track, the progress track.
- **Rule Brown** (line): every divider: the bar's bottom rule, row rules in lists and tables, input borders, the dashed file drop.
- **Paper White** (text): readings and values.
- **Ash** (muted): captions, section headings, units, set-point ticks, inactive tabs, explanatory notes.
- **Ember Grey** (faint): what can't be pressed, and icons of loads that are off.

### Zone grounds
- **zone-heat, zone-cool, zone-humid, zone-dry, zone-dead**: each mode colour sunk almost to the ground, used only as the band's zones. The zone the reading is in steps up one brightness level (`#5A3420`, `#1F4560`, `#1B4A42`, `#4D3E17`, `#332B25`) and its label turns Paper White.

### Named Rules
**The LCD Is the Authority Rule.** Every colour is palette.h's or a stated derivation of it. A new colour means a new entry in palette.h first, approved for the LCD, then here.

**The One Colour per Mode Rule.** Blue is always cooling, orange heating, teal humidifying, gold drying, across the mode word, its icon, its zone, its graph fill and its event dot. No mode colour is reused for an unrelated meaning.

**The Earned Alarm Rule.** Fault red and warning amber appear only where a fault or alert actually exists. "No active alerts" is plain muted text with no colour and no icon.

## Typography

**Display Font:** the system sans (-apple-system, BlinkMacSystemFont, Segoe UI, Roboto, Helvetica Neue, Arial)
**Body Font:** the same
**Label/Mono Font:** ui-monospace, Menlo, monospace, only for file paths and code

**Character:** one plain system sans at every size, with tabular numerals set on the body so every figure lines up and nothing jitters when a reading changes.

### Hierarchy
- **Display** (600, 6rem, line-height 1, -0.035em): the two readings on Overview; 4.5rem on phones. The unit sits beside it at 2rem, 400, Ash, raised to the top. A missing reading shows "--" in Ash.
- **Headline** (600, 2rem): the settings stepper value and the update countdown.
- **Title** (600, 1.5rem, -0.01em): page titles (Graphs, Sensors, Update...). Overview has no visible title.
- **Reason** (400, 1.2rem, 1.35, max 62ch): the reason line beside each reading, its leading mode word bold (600) in the mode colour; 1.05rem on phones.
- **Body** (400, 16px, 1.45): rows, values, messages.
- **Label** (500, 0.95rem, Ash): section headings (Alerts, Loads, Last 7 days, Cabinet temperature). Sentence case, no letter-spacing. Sub-section headings inside a page (Door test, Send firmware, Tuning) are 1.05rem in Paper White.
- **Small** (0.82 to 0.9rem, Ash): band ticks, chart captions and keys, hints, table headers.

### Named Rules
**The Number and the Reason Rule.** A reading is never shown alone on Overview: the reason it is or isn't driving a load sits beside it in words.

**The Tabular Rule.** Figures are tabular everywhere (`font-variant-numeric: tabular-nums` on body); never set proportional figures on a live value.

## Layout

A sticky 64px top bar (logo, seven tabs, then right-aligned modes, time and freshness) over a centred `main` of at most 1440px with 32px padding. Page heads put the title and its control (range selector, type filter) on one baseline row, 24px above content.

- **Overview:** two columns, the readings at `minmax(0, 1fr)` and a 380px side column, 40px apart; the readings stack 44px apart, the side blocks 28px apart. Under 1100px it becomes one column.
- **Forms and reading pages** are capped: settings and tuning 720px, update 760px, door test 640px, prose 70ch.
- **Lists** are ruled rows (11px vertical padding, line rule below), label left, value right.
- **Events** is a three-column grid (time 90px, type 130px, detail), with a bold day header breaking the list.
- **Spacing rhythm:** 8 / 10 / 16 / 24 / 32 / 40px, with 44px between the two readings.

**Phone (under 760px):** the bar wraps and loses height (28px logo); the tabs become a fixed seven-column bottom bar on the ground with a top rule, icon above a 0.75rem label, safe-area padding; the active tab turns Cabinet Amber with no fill; main gets 16px gutters and 96px of bottom room. The mode word is hidden when more than one mode shows. The sensors table becomes one block per channel (name and state, location, temp and RH large, then raw, read age, CRC). Events drop to two columns (time, then type over detail). The band is 46px tall and zone labels shorten ("Heat < 18.5").

## Elevation & Depth

Flat, tonal layering. Depth is three steps of warm ground (ground, ground-raised, surface) and 1px Rule Brown dividers. The one shadow in the system lifts the band's reading marker above the zones.

### Shadow Vocabulary
- **Marker lift** (`box-shadow: 0 2px 8px rgba(0, 0, 0, 0.45)`): the zone band's reading marker only.

### Named Rules
**The Tonal Step Rule.** To separate a region, step its ground up (ground-raised for panels, surface for controls), never add a shadow or a border box.

## Shapes

Soft, small radii that grow with the element: 6px tags and focus rings, 8px buttons, inputs and tabs, 10px pills, alert rows and the segmented track, 12px the band track, 14px chart and table panels, full round on chips, switches and status dots. Dots of 8px (sensor state, event type) carry colour where a full pill would shout. Dashed strokes mean "a target or a place to put something": the set point on bands and graphs, the firmware file drop.

## Components

### Buttons
- **Shape:** gently rounded (8px), 40px tall, 16px sides; small variant 32px tall, 12px sides; icon variant 44px square with a 24px icon.
- **Default:** Cedar Surface with Paper White text; hover one step lighter.
- **Primary:** Cabinet Amber with accent-ink text at 600, for Save and Send update; hover lighter amber.
- **Active / Disabled:** press nudges 1px down (100ms); disabled sinks to Raised Night with Ember Grey text and no movement.
- **Focus:** 2px Cabinet Amber outline, 2px offset.

### Chips (sensor selection on Graphs)
- **Style:** full-round, Raised Night, Ash text, a 10px ring in the chip's colour.
- **State:** checked fills the ring and lifts to Cedar Surface with Paper White text.

### Segmented control (graph range)
- Raised Night track (10px radius, 3px inset); the selected segment is Cedar Surface with Paper White text.

### Cards / Containers
- **Panels** (charts, tables, the update window): Raised Night, 14px radius, 16px padding, no border, no shadow. The update window turns a warm amber ground (`#2E2416`) with an amber icon and a 2rem countdown when Allow update is open.
- **Alert rows:** 10px radius, warning ground (or fault ground), a 20px warning icon in warn (or fault), the time right-aligned in Ash.
- **Status pills** (Starting, Updating, Update allowed): Cedar Surface, amber icon, Paper White words.

### Inputs / Fields
- **Style:** Raised Night, 1px Rule Brown border, 8px radius, 40px tall; placeholder in Ash.
- **Focus:** 2px Cabinet Amber outline, 1px offset.
- **Switch:** 52 by 30 track in Cedar Surface with an Ash thumb; on, the track warms (`#4E3B22`) and the thumb slides 22px and turns amber.
- **Stepper:** label and hint left; minus, a 2rem value (amber when changed and unsaved), plus.
- **File drop:** 56px, dashed Rule Brown, Ash text; hover brightens border and text.

### Navigation
- **Desktop:** text tabs in Ash at 0.95rem, 8px radius; hover Paper White on Raised Night; current page Paper White on Cedar Surface. Icons are hidden.
- **Phone:** the fixed bottom bar described in Layout, icons shown, current page in amber.
- **Header status:** each active mode as its 22px icon in its colour plus the word, the BENCH / TEST tag, the UK local time in 500 weight, and freshness ("updated 6 s ago"), which turns warn when stale.

### Zone Band (signature)
A 52px track (12px radius, clipped) of absolutely placed zones, one per control region: heat, dead zone, cool for temperature; humidify, holding, strip for humidity. Each zone is its zone ground with its label centred (or pinned to the open end when the zone runs off the scale), separated by a 1px ground-coloured seam. The zone holding the reading steps brighter and its label turns Paper White. Ticks below mark the zone edges and the set point ("20.0 set" in Paper White); a 2px dashed line at 45% Paper White marks the set point across the track. The reading marker is a 4px Paper White bar, 76px tall, overhanging the track, with the value bold above it; it glides to a new reading over 600ms (`cubic-bezier(0.16, 1, 0.3, 1)`). When the reading is unknown the track drops to 45% opacity and no marker is drawn. Only the control edges are drawn: safety limits (the RH hard ceiling) are never shown.

### Graphs
uPlot 1.6.32 in Raised Night panels. Cabinet mean temperature in Paper White at 2px, cabinet RH in teal at 2px, the set point dashed in Ash, the coldest-to-warmest band as translucent blue, and the heat / cool (humid / strip) zones as 10% tints of their mode colours behind the plot. "What ran" is a stepped fill per load in its mode colour. Axes and ticks in Ash and Rule Brown; legend hidden in favour of the caption key; all times UK local (Europe/London).

### Motion
Short and functional: 150ms ease-out on tab and button colour, 180ms on the switch, 200ms on zone colour and the progress bar, 600ms glide on the band marker, a 900ms amber fade on newly arrived event rows. All motion is off under `prefers-reduced-motion`.

## Do's and Don'ts

### Do:
- **Do** take every colour from `lib/lcdgfx/src/lcdgfx/palette.h`, or from a derivation of it recorded here.
- **Do** show each reading with its reason in words and its zone band, with the mode word in the mode colour.
- **Do** use Material Symbols Rounded, filled, weight 500, inlined as SVG from `web/vendor/material-symbols-0.49.0`.
- **Do** use the system sans with tabular numerals; mono only for paths and code.
- **Do** separate regions by tonal steps (ground, ground-raised, surface) and 1px Rule Brown rules.
- **Do** give times in UK local time.
- **Do** switch the tabs to a fixed bottom bar under 760px.

### Don't:
- **Don't** load anything from the internet: no web fonts, no CDN scripts or styles, no remote images.
- **Don't** show a safety value anywhere, including as a marker on a band or a line on a graph (no 70 % RH ceiling).
- **Don't** use fault red or warning amber for anything that is not a fault or an alert.
- **Don't** add shadows to panels or rows; the band marker is the only lifted element.
- **Don't** add a light theme; the LCD's world is dark.
