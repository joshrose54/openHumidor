---
version: 1
slug: "web-index-html"
primary_target: "web/index.html"
related_targets: []
---

# Surface brief: the openHumidor web dashboard

Scope: `web/` (one page file, seven pages: Overview, Graphs, Sensors, Events, Settings, Logs, Update). Mode: **Operate**.

Audience and job: the cabinet's owner on a phone (a glance: is it all right) and a Mac (trends, tracing a fault, maintenance). Content and constraints: spec 5.5 and docs/api.md; nothing from the internet; system fonts; the LCD's palette and Material Symbols Rounded icons; uPlot for graphs; fetch only what the open page shows, nothing while hidden.

Chosen structure: "Where it sits, and why" (Josh, Q5b decision round, 2026-11-13).

## Direction contract

THESIS: The Overview shows each reading on its control band — heat · dead zone · cool, and humidify · holding · strip — with the reading marked and the reason in words, so a glance says not only "20.6" but "cooling, because it is above 20.5". It refuses the category's grid of equal stat cards.

OWN-WORLD: The LCD's world, kept: warm near-black ground #14110F, raised surfaces #2A2420, rules #3A332E, warm off-white text #F3EDE7 and muted #A69C93; one colour per mode (cool #62B8EB, heat #F28C4E, humid #46C2AE, dry #E0B850, accent #D9A55B); fault #EF5A55 and warn #F2B33D only where they mean something. Filled rounded Material Symbols at weight 500. Big tabular numerals, one system sans.

STORY: The visitor sees the two readings and where they sit, reads why each load is or isn't running, sees any alert at once, then goes to Graphs, Sensors or Events to trace it.

FIRST VIEWPORT: Desktop: top bar with the logo, the seven tabs, the time, freshness. Left two-thirds: temperature then RH, each a big number, the reason line in its mode colour, and its band with the zones, set and edges labelled and a marker at the reading. Right column: alerts, loads with what holds them, the week's counts. Phone: the same top to bottom, tabs as a bottom bar.

FORM: chosen structure 3 of 7 on the grounded list (the roll's lead was 4, cabinet section); seed:60b7279f5a8b774a

FINISH: unreviewed and undocumented is unfinished; this build ends with the finish review, the verdict, DESIGN.md, and every shipping raster carrying its provenance

## Unresolved

- The RH hard ceiling (70 %) is a safety value: never drawn on the band.
