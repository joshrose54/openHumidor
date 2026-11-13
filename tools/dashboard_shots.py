#!/usr/bin/env python3
"""Screenshots of the web dashboard (stage Q5b), from the Mac review build, for review: every page
at phone and desktop size, in the normal and Fault states (and a few more), with an index page.

    ~/.platformio/penv/bin/pio run -e dashboard_sim       (builds the review server)
    python3 tools/dashboard_shots.py                     (starts it on port 8099, takes the shots)

Writes .pio.nosync/dashboard-shots/*.png and index.html (open it in a browser). Uses Google Chrome,
headless. Made-up data: the cabinet model run by the real control logic.
"""
import html
import os
import subprocess
import sys
import tempfile
import time
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROGRAM = os.path.join(ROOT, ".pio.nosync", "build", "dashboard_sim", "program")
OUT = os.path.join(ROOT, ".pio.nosync", "dashboard-shots")
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
PORT = 8099
PAGES = ["overview", "graphs", "sensors", "events", "settings", "logs", "update"]
SIZES = {"desktop": (1440, 1500, 1), "phone": (390, 3300, 2)}
# (state key, label, pages to capture)
# The "shown" states first; the real ones (a fault takes the 5-minute recovery hold to clear, a
# boot its 5-minute hold) last, so no state leaks into the next.
STATES = [
    ("normal", "Normal", PAGES),
    ("window", "Allow update pressed on the LCD", ["update"]),
    # the Update page through every OTA state (stage Q5d): /sim/ota?name=... on the review build
    ("ota-closed", "Update: window closed", ["update"]),
    ("ota-open", "Update: window open", ["update"]),
    ("ota-compressor", "Update: window open, compressor running", ["update"]),
    ("ota-writing", "Update: writing, 43 %", ["update", "overview"]),
    ("ota-restarting", "Update: written and checked, restarting", ["update"]),
    ("ota-pending", "Update: the new image being checked", ["update"]),
    ("ota-good", "Update: marked good", ["update"]),
    ("ota-rolled-back", "Update: rolled back", ["update"]),
    ("ota-abandoned", "Update: abandoned (stalled)", ["update"]),
    ("ota-corrupt", "Update: corrupt image refused at its check", ["update"]),
    ("ota-interrupted", "Update: interrupted by a power cut", ["update"]),
    ("heat-humid", "Heating and humidifying", ["overview"]),
    ("drying", "Drying (RH strip)", ["overview"]),
    ("updating", "Updating", ["overview"]),
    ("sd-fail", "SD card out", ["events", "logs"]),
    ("fault-sensors", "Fault: ch2 and ch3 unplugged", PAGES),
    ("just-booted", "Just booted", ["overview", "graphs"]),
]


def get(path):
    with urllib.request.urlopen(f"http://127.0.0.1:{PORT}{path}", timeout=30) as r:
        return r.read()


def shot(page, path, w, h, scale, profile):
    # Headless Chrome lays out no narrower than 500 px, so a phone width is drawn in a frame of
    # that width (its own media queries apply) and the picture cropped to it.
    url = f"http://127.0.0.1:{PORT}/#{page}" if w >= 500 else f"http://127.0.0.1:{PORT}/sim/frame?page={page}&w={w}&h={h}"
    subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars", f"--user-data-dir={profile}",
                    f"--window-size={max(w, 500)},{h}", f"--force-device-scale-factor={scale}", "--virtual-time-budget=6000",
                    f"--screenshot={path}", url], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    if w < 500:
        subprocess.run(["magick", path, "-crop", f"{w * scale}x{h * scale}+0+0", "+repage", path], check=True)


def main():
    if not os.path.exists(PROGRAM):
        sys.exit("build the review server first: ~/.platformio/penv/bin/pio run -e dashboard_sim")
    os.makedirs(OUT, exist_ok=True)
    sim = subprocess.Popen([PROGRAM, "--port", str(PORT)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        for line in sim.stdout:
            if "Stop: Ctrl-C" in line:
                break
        profile = tempfile.mkdtemp(prefix="oh-shots-")
        cards = []
        for key, label, pages in STATES:
            get(f"/sim/state?name={'normal' if key == 'window' or key.startswith('ota-') else key}")
            if key == "window":
                get("/sim/allow-update")
            if key.startswith("ota-"):
                get(f"/sim/ota?name={key[4:]}")
            time.sleep(1)
            for p in pages:
                for size, (w, h, scale) in SIZES.items():
                    name = f"{key}-{p}-{size}.png"
                    shot(p, os.path.join(OUT, name), w, h, scale, profile)
                    cards.append((key, label, p, size, name))
                    print("  ", name)
        # the Update page naming the image chosen, before it is sent (spec 5.2): a cabinet build, and
        # the rollback test image's warning (headless Chrome driven by tools/drive_update_page.mjs)
        get("/sim/state?name=normal")
        get("/sim/ota?name=open")
        for key, label, img in (("ota-chosen-cabinet", "Update: a cabinet build chosen", "revq.bin"),
                                ("ota-chosen-rollback", "Update: the rollback test image chosen", "revq_rollback_test.bin")):
            path = os.path.join(ROOT, ".pio.nosync", "images", img)
            if not os.path.exists(path):
                print(f"   (no {img}: build it to see this picture)")
                continue
            name = f"{key}-update-desktop.png"
            subprocess.run(["node", os.path.join(ROOT, "tools", "drive_update_page.mjs"), str(PORT), path, OUT, "0", name],
                           check=True, stdout=subprocess.DEVNULL)
            STATES.append((key, label, ["update"]))
            cards.append((key, label, "update", "desktop", name))
            print("  ", name)
        get("/sim/state?name=normal")
        get("/sim/ota?name=closed")
        rows = []
        for key, label, _ in STATES:
            rows.append(f"<h2>{html.escape(label)}</h2><div class=row>")
            for k, _, p, size, name in cards:
                if k == key:
                    rows.append(f"<figure class={size}><a href='{name}'><img src='{name}' loading=lazy></a>"
                                f"<figcaption>{p} · {size}</figcaption></figure>")
            rows.append("</div>")
        with open(os.path.join(OUT, "index.html"), "w") as f:
            f.write("<!doctype html><meta charset=utf-8><title>openHumidor dashboard screenshots</title><style>"
                    "body{background:#0c0a09;color:#F3EDE7;font:15px -apple-system,sans-serif;margin:24px}"
                    ".row{display:flex;flex-wrap:wrap;gap:18px;align-items:flex-start}figure{margin:0}"
                    "figure.desktop img{width:560px}figure.phone img{width:200px}img{border:1px solid #3A332E;"
                    "border-radius:6px;display:block}figcaption{color:#A69C93;margin-top:6px}h2{margin:36px 0 12px}"
                    "</style><h1>openHumidor dashboard: review screenshots</h1><p style='color:#A69C93'>The Mac review "
                    "build (made-up data from the cabinet model and the real control logic). Click a picture for "
                    "full size.</p>" + "".join(rows))
        print(f"wrote {len(cards)} screenshots and index.html to {os.path.relpath(OUT, ROOT)}/")
    finally:
        sim.terminate()


if __name__ == "__main__":
    main()
