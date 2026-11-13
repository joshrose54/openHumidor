# Licences of what the web dashboard is built from

Everything here is served from the board's flash with the page; nothing is loaded from the
internet (spec 5.5). Pinned copies, kept in `web/vendor/`.

| What | Version | Licence | Where | Used for |
|---|---|---|---|---|
| uPlot | 1.6.32 | MIT, Copyright (c) 2022 Leon Sorokin | `web/vendor/uplot-1.6.32/` (`uPlot.iife.min.js`, `uPlot.min.css`, `LICENSE`); from the npm package `uplot@1.6.32` | The graphs |
| Material Symbols Rounded | 0.49.0 (filled, weight 500: the LCD's icons) | Apache License 2.0, Copyright Google LLC | `web/vendor/material-symbols-0.49.0/` (one SVG per icon, `LICENSE`); from the npm package `@material-symbols/svg-500@0.49.0`, and `mode_standby` from github.com/google/material-design-icons (symbols/web) | The icons, the same set as the LCD (lib/lcdgfx/LICENSES) |
| The openHumidor logo | — | The project's own | `docs/brand/openhumidor-logo.svg` | The header |

Checksums (SHA-256) of the uPlot files as fetched:

```
19c8d4c6ad88929a79f4ae49d6f7161566dfd0ba3d15cc495e974f787eb78f1f  uPlot.iife.min.js
df630c6a8d6f8eeaff264b50f73ce5b114f646ffd9a0bb74f049b0a00135fa04  uPlot.min.css
8f989229699b4fe2f1a0432d0e9edc338a8a911e250e2d1b01ecd770a5f5b1bd  LICENSE
```

The packed page (`tools/make_web_assets.py`) keeps uPlot's own licence comment and names both
licences in an HTML comment at its top.
