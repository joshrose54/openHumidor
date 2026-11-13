#!/usr/bin/env python3
"""Packs the web dashboard (web/) into the one compressed page file the board serves (spec 5.5:
"one page file holding all seven pages", "small and compressed", nothing from the internet).

    python3 tools/make_web_assets.py

  web/index.html        the page; <link rel="stylesheet" href="..."> and <script src="..."> to local
                        files are put inline; <!-- ICONS --> becomes the icon sprite; <!-- LOGO -->
                        the openHumidor logo
  web/app.css, app.js   the dashboard's own style and code
  web/vendor/           uPlot 1.6.32 (MIT) and Material Symbols Rounded 0.49.0 (Apache 2.0), pinned
                        copies with their licences; see web/LICENSES.md
  docs/brand/openhumidor-logo.svg

Writes:
  .pio.nosync/web/index.html           the packed page, uncompressed (to read, or to serve with
                                       dashboard_sim --page while working on web/)
  lib/webui/src/webui/page.cpp, .h     the packed page, gzip -9, as bytes for the firmware and the
                                       Mac review build (GENERATED: never edit by hand)

Refuses a page that would load anything from outside: no http(s):// or // URLs in src, href, url().
"""
import gzip
import io
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB = os.path.join(ROOT, "web")
ICONS_DIR = os.path.join(WEB, "vendor", "material-symbols-0.49.0")
LOGO = os.path.join(ROOT, "docs", "brand", "openhumidor-logo.svg")
OUT_HTML = os.path.join(ROOT, ".pio.nosync", "web", "index.html")
OUT_DIR = os.path.join(ROOT, "lib", "webui", "src", "webui")

# The icons the pages use (the LCD's set, spec 5.5, plus the few the web pages need), by file name.
ICONS = ["ac_unit", "local_fire_department", "water_drop", "humidity_low", "mode_standby", "hourglass_top",
         "system_update_alt", "warning", "wifi", "wifi_off", "settings", "add", "remove", "arrow_back",
         "keyboard_arrow_down", "download", "upload", "thermometer", "air", "sensors", "schedule", "show_chart", "list"]


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def icon_symbol(name):
    svg = read(os.path.join(ICONS_DIR, name + ".svg"))
    vb = re.search(r'viewBox="([^"]+)"', svg).group(1)
    body = re.sub(r"^.*?<svg[^>]*>|</svg>\s*$", "", svg, flags=re.S).strip()
    return f'<symbol id="i-{name}" viewBox="{vb}">{body}</symbol>'


def logo_svg():
    svg = read(LOGO)
    svg = re.sub(r"<!--.*?-->", "", svg, flags=re.S)
    svg = re.sub(r'\s(width|height)="[^"]*"', "", svg, count=2)
    svg = svg.replace("<svg ", '<svg class="logo" role="img" aria-label="openHumidor" fill="currentColor" ', 1)
    svg = svg.replace('fill="#FFFFFF"', "", 1)
    return re.sub(r">\s+<", "><", svg).strip()


def inline(html):
    def css(m):
        path = os.path.join(WEB, m.group(1))
        return "<style>\n" + read(path) + "\n</style>"

    def js(m):
        path = os.path.join(WEB, m.group(1))
        return "<script>\n" + read(path).replace("</script", "<\\/script") + "\n</script>"

    html = re.sub(r'<link rel="stylesheet" href="([^":]+)">', css, html)
    html = re.sub(r'<script src="([^":]+)"></script>', js, html)
    sprite = '<svg xmlns="http://www.w3.org/2000/svg" style="display:none">' + "".join(icon_symbol(n) for n in ICONS) + "</svg>"
    html = html.replace("<!-- ICONS -->", sprite)
    html = html.replace("<!-- LOGO -->", logo_svg())
    return html


def check_offline(html):
    # the SVG namespace is a name, not a fetch; everything else from outside is refused
    probe = html.replace('xmlns="http://www.w3.org/2000/svg"', "")
    bad = re.findall(r'(?:src|href)\s*=\s*["\'](?:https?:)?//[^"\']+|url\(\s*["\']?(?:https?:)?//[^)]+', probe)
    if bad:
        sys.exit("make_web_assets: the page would load from outside: " + "; ".join(bad[:5]))


def check_scripts():
    # a script that doesn't parse stops the whole page: refuse to pack it (needs node, as the tests do)
    import shutil
    if not shutil.which("node"):
        print("make_web_assets: node not found; the scripts' syntax is not checked")
        return
    for name in ["image_check.js", "app.js"]:
        r = subprocess.run(["node", "--check", os.path.join(WEB, name)], capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit(f"make_web_assets: web/{name} doesn't parse:\n{r.stderr}")


def main():
    check_scripts()
    html = inline(read(os.path.join(WEB, "index.html")))
    check_offline(html)
    os.makedirs(os.path.dirname(OUT_HTML), exist_ok=True)
    with open(OUT_HTML, "w", encoding="utf-8") as f:
        f.write(html)
    raw = html.encode("utf-8")
    buf = io.BytesIO()
    with gzip.GzipFile(fileobj=buf, mode="wb", compresslevel=9, mtime=0) as g:   # mtime 0: the same input, the same bytes
        g.write(raw)
    gz = buf.getvalue()
    os.makedirs(OUT_DIR, exist_ok=True)
    rows = ",\n".join("  " + ",".join(str(b) for b in gz[i:i + 24]) for i in range(0, len(gz), 24))
    with open(os.path.join(OUT_DIR, "page.cpp"), "w") as f:
        f.write(f"""// GENERATED by tools/make_web_assets.py from web/ - do not edit. Rebuild with that script.
// The web dashboard (spec 5.5): one page file, gzip -9: {len(raw)} bytes packed, {len(gz)} compressed.
// Licences: uPlot (MIT), Material Symbols Rounded (Apache 2.0): web/LICENSES.md.
#include "webui/page.h"

namespace webui {{

const uint8_t kPageGz[] = {{
{rows}
}};
const size_t kPageGzLen = sizeof(kPageGz);
const size_t kPageRawLen = {len(raw)};

}}  // namespace webui
""")
    with open(os.path.join(OUT_DIR, "page.h"), "w") as f:
        f.write("""#pragma once
// The web dashboard's one page file, gzip-compressed (GENERATED by tools/make_web_assets.py).
// Served as-is with "Content-Encoding: gzip" (spec 5.5).
#include <stdint.h>
#include <stddef.h>

namespace webui {

extern const uint8_t kPageGz[];
extern const size_t kPageGzLen;
extern const size_t kPageRawLen;   // the packed page before compression

}  // namespace webui
""")
    print(f"make_web_assets: {len(raw):,} bytes packed, {len(gz):,} compressed "
          f"({100 * len(gz) / len(raw):.0f}%) -> lib/webui/src/webui/page.cpp; {os.path.relpath(OUT_HTML, ROOT)}")


if __name__ == "__main__":
    main()
