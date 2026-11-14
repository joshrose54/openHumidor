#!/usr/bin/env python3
"""Builds docs/ui-strings.csv, the list of every string a user reads on the LCD and the dashboard
(spec 5.5 "Words on screen"; CLAUDE.md: update its row whenever a string changes).

    python3 tools/make_ui_strings.py

Made at the words sweep: `current` is the text now, `was` the text before round 2 (the round-1 list,
read from git commit a72a977). To change a string: edit its r(...) row below (the text in `prop`,
or an entry in OVERRIDE), and NEWLOC if the automatic line search can't find it; then run this.
Needs a C++ compiler for tools/measure_lcd_text.cpp (built into .pio.nosync on first use). Each row names its file and a snippet that
must occur exactly once there (or the nth time); the line number is looked up, so it is never typed.
LCD rows give the font and the room in pixels: the proposed text (worst case) is measured with the
firmware's own font tables by ./measure."""
import csv, os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HERE = os.path.dirname(os.path.abspath(__file__))
# The LCD texts are measured with the firmware's own font tables (lib/lcdgfx).
MEASURE = os.path.join(ROOT, ".pio.nosync", "measure_lcd_text")
if not os.path.exists(MEASURE) or os.path.getmtime(MEASURE) < max(os.path.getmtime(os.path.join(ROOT, p)) for p in (
        "tools/measure_lcd_text.cpp", "lib/lcdgfx/src/lcdgfx/canvas.cpp", "lib/lcdgfx/src/lcdgfx/assets.cpp")):
    os.makedirs(os.path.dirname(MEASURE), exist_ok=True)
    subprocess.run(["c++", "-std=c++17", "-O1", "-I", os.path.join(ROOT, "lib/lcdgfx/src"), "-I", os.path.join(ROOT, "lib/core/src"),
                    os.path.join(ROOT, "tools/measure_lcd_text.cpp"), os.path.join(ROOT, "lib/lcdgfx/src/lcdgfx/canvas.cpp"),
                    os.path.join(ROOT, "lib/lcdgfx/src/lcdgfx/assets.cpp"), "-o", MEASURE], check=True)
rows = []
errors = []
_cache = {}


def text_of(path):
    if path not in _cache:
        with open(os.path.join(ROOT, path), encoding="utf-8") as f:
            _cache[path] = f.read()
    return _cache[path]


def line_of(path, snippet, nth=None):
    t = text_of(path).replace("\xa0", " ")   # app.js puts no-break spaces before units
    at, found = -1, []
    while True:
        at = t.find(snippet, at + 1)
        if at < 0:
            break
        found.append(at)
    if not found:
        errors.append(f"not found: {path}: {snippet!r}")
        return 0
    if nth is None and len(found) > 1:
        errors.append(f"{len(found)} matches: {path}: {snippet!r}")
    nth = nth or 1
    if nth > len(found):
        errors.append(f"no match #{nth}: {path}: {snippet!r}")
        return 0
    return t.count("\n", 0, found[nth - 1]) + 1


def width(font, s):
    out = subprocess.run([MEASURE], input=f"{font}|{s}\n", capture_output=True, text=True).stdout
    w, miss, _ = out.split("\t", 2)
    return int(w), int(miss)


def cell(v):
    if v is None:
        return "(removed)", 0
    if isinstance(v, tuple):
        tmpl, ex = v
        return f"{tmpl} → “{ex}”", len(ex)
    return v, len(v)


# ---- round 2: the row's text now is `prop` (as approved, with Josh's changes), the old text is `was`
import re as _re
OLD = {}
import io as _io
_round1 = subprocess.run(["git", "show", "a72a977:docs/ui-strings.csv"], cwd=ROOT, capture_output=True, text=True, check=True).stdout
for _row in csv.DictReader(_io.StringIO(_round1)):    # the round-1 list: its line numbers guide the search
    OLD[_row["id"]] = _row
T2 = lambda t, e: (t, e)
OVERRIDE = {
  "ALR-05": "Fault: settings error",
  "RST-02": "Software restart",
  "RST-07": "Other",
  "ION-06": "storage error",
  "DB-OV-58": ("Waiting · {why}", "Waiting · storage error"),
  "EVT-ION-06": ("Ion generator paused: {why}", "Ion generator paused: storage error"),
  "EVT-DR-01": ("Drying started: humidity {rh:.1f}%, above {x:.1f}%", "Drying started: humidity 68.1%, above 67.0%"),
  "EVT-HU-01": ("Humidifying started: humidity {rh:.1f}%, below {x:.1f}%", "Humidifying started: humidity 59.4%, below 60.0%"),
  "EVT-HU-02": ("Humidifying ended: humidity {rh:.1f}%, at target", "Humidifying ended: humidity 62.0%, at target"),
  "EVT-BL-01": ("Blowers to boost: spread {t:.1f}°C, {rh:.1f}% / Blowers to boost after {start|the update|full speed}", "Blowers to boost: spread 1.2°C, 2.0%"),
  "DB-SE-06": "Bad reads / no answers, this hour",
  "DB-SE-06b": "Bad reads / no answers, this hour:",
  "ALR-12": ("Excluded: {Position}", "Excluded: Bottom front"),
  "GRD-01": ("Waiting · {reason}", "Waiting · settings error"),
  "DB-EV-13": "Note (optional) · placeholder “e.g. door open 30 s”",
  "EVT-SN-01": ("Bad read: {Position}", "Bad read: Middle rear"),
}
OVERRIDE_NOTE = {
  "ALR-05": "Round 2: Josh's word (spec word list: Settings error).",
  "RST-02": "Round 2: Josh's word.",
  "RST-07": "Round 2: “Woke from sleep” isn't in the spec's word list; this firmware never sleeps, so “Other”.",
  "ION-06": "Round 2: Josh's word (spec word list: Storage error).",
  "DB-OV-58": "Round 2: Josh's word.", "EVT-ION-06": "Round 2: Josh's word.",
  "EVT-DR-01": "Round 2 (F4): event sentences keep one decimal.", "EVT-HU-01": "Round 2 (F4): one decimal, the spec's own example.",
  "EVT-HU-02": "Round 2 (F4): one decimal.", "EVT-BL-01": "Round 2 (F4): one decimal; humidity differences in %.",
  "DB-SE-06": "Round 2: “No answer” is the word list's word for a timeout.", "DB-SE-06b": "Round 2: as DB-SE-06.",
  "ALR-12": "Round 2: the names from the sensor table (capitalised, spec word list).",
  "GRD-01": "Round 2: as ALR-05.", "DB-EV-13": "Round 2: a visible label; the example stays as the placeholder.",
}
LCD_WORST = {"ALR-12": "Excluded: Bottom front", "ALR-05": None}
WORDS_CPP = "lib/core/src/wineador/words.cpp"
WORD_LIST = {"GRD": "kGuards", "EXC": "kExclusions", "RST": "kRestarts", "ION": "kIonWhy", "BLW": "kBlowerWhy", "TYP": "kTypeNames"}
NEWLOC = {}      # id -> (path, snippet[, nth]) where the automatic search can't tell
_L, _C, _O, _W, _J, _E = "lib/core/src/wineador/lcd_ui.cpp", "lib/lcdgfx/src/lcdgfx/screens.cpp", "lib/core/src/wineador/ota_update.cpp", "lib/core/src/wineador/web_api.cpp", "web/app.js", "lib/core/src/wineador/event_sentence.cpp"
NEWLOC.update({
  "LCD-W-17": (_L, 'snprintf(v.alertText, sizeof(v.alertText), "%s", title + 7);'),
  "LCD-S-04": (_C, '(double)kTargetTempMinC, kDash'), "LCD-S-05": (_C, '(double)kRhSetpointMinPct, kDash'),
  "DB-OV-43": (_J, ': "Off", !L.compressor.on && gc);'), "DB-OV-45": (_J, 'L.heater.on ? `On · ${L.heater.duty_pct}%`'),
  "DB-OV-46": (_J, 'L.humidifier.on ? "On"'), "DB-OV-47": (_J, '`${L.fan.pwm}% · ${num(L.fan.rpm)} rpm`'),
  "DB-OV-48": (_J, '`${cap(L.interlock)} side`'), "DB-OV-49": (_J, '? `Idle · ${B.pwm}%`'),
  "DB-OV-50": (_J, '? `Idle · ${B.pwm}%`'), "DB-OV-51": (_J, '? `Idle · ${B.pwm}%`'),
  "DB-OV-54": (_J, '`On · ${mss(I.left_s)} left`'), "DB-OV-61": (_J, '      : "Off";'),
  "DB-OV-67": (_J, '(w.ready ? "Off" : "Counting…")'), "DB-GR-11": (_J, 'Object.values(WORDS.positions)'),
  "DB-SE-06c": ("web/app.css", 'content: "Raw "'), "DB-SE-07": (_J, '<td class="ch">${x.ch}</td>'),
  "DB-SE-10": (_J, 'fmt(x.t, 2) + "°C"'), "DB-SE-14": (_J, 'this causes a Fault and stops every load.'),
  "DB-EV-02": ("web/index.html", '<option value="">All</option>'), "DB-EV-05": (_J, '(clock not set)'),
  "DB-ST-03": (_J, '$("[data-k=hint]", el).textContent'), "DB-ST-05": (_J, '$("[data-k=hint]", el).textContent'),
  "DB-ST-10b": (_J, '$("[data-k=hint]", el).textContent'), "DB-LG-04": (_J, 'esc(cap(f.kind))'),
  "DB-UP-04": (_J, '<b>Checking new firmware</b>'), "DB-UP-08": (_J, '<b>Updates closed</b>'),
  "DB-UP-19": (_J, 'm.textContent = text;'),
  "OTA-D-01": (_O, 'snprintf(r.detail, sizeof(r.detail), "%s", r.version);'),
  "OTA-D-02": (_O, '"%s (was %s)", r.version, r.from);', 1), "OTA-D-04": (_O, '"%s (was %s)", r.version, r.from);', 2),
  "OTA-D-03": (_O, '"%s: %s", r.version, otaOutcomeWords(o)'),
  "API-34": (_W, '"%u min a day. The limit is %.0f min."'), "API-40": (_W, 'Runs per day is a whole number.'),
  "EVT-HP-01": (_E, 'Memory low: below 30%sKB'),
})
OVERRIDE["API-40"] = "Runs per day is a whole number. · Minutes per run is a whole number."
OVERRIDE_NOTE["API-40"] = "Round 2: as API-39 (the labels sit under the Ion generator switch)."
NEWLOC.update({
  "OTA-O-01": (_O, 'return "upload stalled";'), "OTA-O-02": (_O, 'return "upload cut off";'), "OTA-O-03": (_O, 'return "file damaged";'),
  "OTA-O-04": (_O, 'return "couldn\'t write it";'), "OTA-O-05": (_O, 'return "writing stalled";'),
  "EVT-ST-02": (_E, '"Settings from the cabinet screen not saved"'), "EVT-I2-01": (_E, '"Sensor bus stuck: %s excluded"'),
  "EVT-RT-02": (_E, '"Clock battery setting fixed"'), "EVT-NT-03": (_E, '"Clock right to %lu%ss"'),
  "EVT-LC-01": (_E, 'return snprintf(o, n, "Allow update pressed");'), "EVT-WF-03": (_E, '"%lu more WiFi drops this hour"'),
})
OVERRIDE["API-26"] = "Last update still being checked. Try again shortly."
OVERRIDE_NOTE["API-26"] = "Round 2: the default can't know the seconds; the firmware's own message (API-31) says them."
# after Josh's review of round 2
OVERRIDE["DB-OV-48"] = ("Power to · {Cooling|Heating}", "Power to · Cooling")
OVERRIDE["EVT-IL-01"] = ("Power switched to {cooling|heating}{ (relay check)}", "Power switched to heating (relay check)")
OVERRIDE["TYP-04"] = "Power to"
OVERRIDE["DB-ST-T12"] = "Timed cooling runs"
NEWLOC["DB-OV-48"] = (_J, 'row("Power to", "settings"')
NEWLOC["DB-ST-T12"] = (_W, '"Timed cooling runs"')
OVERRIDE["DB-OV-73"] = ("12V supply · {v:.1f}V · lowest today {v:.1f}V", "12.1V · lowest today 11.9V")
OVERRIDE_NOTE["DB-OV-73"] = "Rule 8: volts to one decimal."
OVERRIDE["EVT-12-02"] = ("12V supply back after {s} s, lowest {v:.1f}V", "12V supply back after 5.3 s, lowest 10.5V")
OVERRIDE_NOTE["EVT-12-02"] = "Rule 8: volts to one decimal."
OVERRIDE["OTA-D-05"] = ("{v}", "2.0.1")
NEWLOC["OTA-D-05"] = (_O, 'snprintf(b.lastDetail, sizeof(b.lastDetail), "%s", f.runningVersion);')
OVERRIDE_NOTE["OTA-D-05"] = "While it is being checked the row reads “Installed · 2.0.1 · 14 Nov 10:08”: the Checking box says “being checked”."
OVERRIDE_NOTE["EVT-OTA-06"] = "The event now logs the refusal code only (“OTA upload refused: window_closed”); the sentence turns the code into words."
OVERRIDE_NOTE["DB-ST-19"] = "The humidity units come from the API as “%” (API version 5)."
_P1 = "Spec word list: “Power to”, not “Interlock” (approved after round 2). "
OVERRIDE_NOTE["DB-OV-48"] = _P1 + "Row “Power to · Cooling” (the K1 relay feeds the cooling or the heating side)."
OVERRIDE_NOTE["EVT-IL-01"] = _P1 + "Sentence “Power switched to cooling”, “Power switched to heating (relay check)”."
OVERRIDE_NOTE["TYP-04"] = _P1 + "Filter name “Power to”."
OVERRIDE_NOTE["DB-ST-T12"] = ("Spec word list: “Timed cooling runs”, not “Dosed control” (approved after round 2). Spec 4.5b: "
  "the controller sets each cooling run's length in advance (a dose, from the temperature error and the room-cabinet gap) "
  "and corrects it run by run, instead of running until the target is reached; shorter runs dip the humidity less. Off until B6.")
unresolved = []

def _norm(t):
    for a, b in (("\xa0", " "), ("\\xC2\\xB0", "°"), ('" "', ""), ("%%", "%"), ("\\xC2\\xB7", "·"), ("\\xE2\\x80\\x93", "–"),
                 ("\\xE2\\x88\\x92", "−"), ("\\xE2\\x86\\x92", "→"), ("\\xC2\\xA0", " "), ("\\u00a0", " "), ("\\u2212", "−")):
        t = t.replace(a, b)
    return t

def _finds(path, snip):
    t = _norm(text_of(path))
    out, at = [], -1
    while True:
        at = t.find(snip, at + 1)
        if at < 0: return out
        out.append(t.count("\n", 0, at) + 1)

def _oldline(id):
    o = OLD.get(id)
    try: return int(o["file:line"].rsplit(":", 1)[1])
    except Exception: return 0

def _code_of(id, was):
    w = was[1] if isinstance(was, tuple) else was
    if id.startswith("GRD"): return w.split("held: ", 1)[1]
    return w

def locate(id, path, snippet, nth, was, new):
    if id in NEWLOC:
        p, s = NEWLOC[id][:2]
        n = NEWLOC[id][2] if len(NEWLOC[id]) > 2 else None
        f = _finds(p, s)
        if f and (n or len(f) == 1): return f"{p}:{f[(n or 1) - 1]}"
        unresolved.append(f"NEWLOC wrong: {id} {s!r} ({len(f)})"); return f"{p}:?"
    pre = id.split("-")[0]
    if pre in WORD_LIST and new is not None:
        t = _norm(text_of(WORDS_CPP))
        start = t.find("CodeWord " + WORD_LIST[pre])
        code = _code_of(id, was)
        at = t.find(f'{{"{code}", ', start)
        if start >= 0 and at >= 0: return f"{WORDS_CPP}:{t.count(chr(10), 0, at) + 1}"
    if new is None:
        return f"{path} (removed)"
    f = _finds(path, _norm(snippet))
    if f and (nth or len(f) == 1):
        return f"{path}:{f[min(nth or 1, len(f)) - 1]}"
    # the new text's own words, longest fragment first; nearest the old line when there are several
    txt = new if isinstance(new, str) else new[0] + " | " + new[1]
    frags = [x.strip(" ·:,.“”") for x in _re.split(r"\{[^}]*\}| / | · | \| |→|“|”", txt)]
    frags = sorted({x for x in frags if len(x) >= 5}, key=len, reverse=True)
    old = _oldline(id)
    for fr in frags:
        f = _finds(path, fr)
        if f: return f"{path}:{min(f, key=lambda l: abs(l - old))}"
    unresolved.append(f"not found: {id} {path} {frags[:3]}")
    return f"{path}:?"

def r(id, surface, screen, path, snippet, cur, prop, reason, notes="", nth=None, lcd=None):
    assert reason in ("repeat", "jargon", "too long", "inconsistent", "style", "fine", "new"), reason
    was = cur
    new = cur if prop == "=" else prop
    if id in OVERRIDE: new = OVERRIDE[id]
    loc = locate(id, path, snippet, nth, was, new)
    c, cn = cell(new)
    wv, wn = cell(was) if was != "NEW" else ("(new)", 0)
    if new is None: cn = 0
    fits = ""
    if lcd:
        font, room, worst, buf = lcd
        if id in LCD_WORST: worst = LCD_WORST[id]
        if new is None:
            fits = "n/a (removed)"
        else:
            s = worst or (new[1] if isinstance(new, tuple) else new)
            w, miss = width(font, s)
            ok = w <= room and miss == 0 and (buf is None or len(s.encode()) < buf)
            fits = f"{'yes' if ok else 'NO'} ({w} of {room} px{', ' + str(miss) + ' glyphs missing' if miss else ''}"
            fits += f", {len(s.encode())} of {buf - 1} bytes" if buf else ""
            fits += f"; worst case “{s}”)" if worst else ")"
    notes = notes.replace("[new word] ", "").replace(" [new word]", "").replace("[new word]", "").replace("[logic][new word]", "[logic]")
    notes = _re.sub(r"\s*Tests?: test_[^.]*?(?:\.(?=\s|$)|$)", "", notes)
    notes = _re.sub(r"\s*\(word-list proposal\)", "", notes).replace(" (word-list proposal).", ".")
    notes = _re.sub(r"\s*Left to Josh\.?|\s*left to Josh\.?", "", notes)
    notes = _re.sub(r"See finding (F\d)", r"Finding \1, decided", notes)
    notes = notes.replace("Also the “N more this hour” line: “Middle rear: 5 more bad reads this hour”.", "The “N more this hour” line: EVT-SN-05.")
    notes = _re.sub(r"\s+", " ", notes).strip()
    notes = {
      "DB-GR-09": "[layout] Decided: full words, the y axis 84 px on all three charts so they line up.",
      "EVT-HU-01": "Round 2 (F4): one decimal, so a figure never contradicts its threshold; spec 5.5's own example.",
      "ALR-09": "As ALR-07. The likely cause is an empty tank (5.6); a hint won't fit the line.",
      "DB-OV-57": "On Events as its own sentence (EVT-ION-04).",
      "DB-EV-04": "[layout] The sentence names its subject; the type's coloured dot stays (its name for screen readers, DB-EV-17). The filter keeps the names.",
      "DB-EV-06": "Round 2 (F2): shown on hover, or when the row is tapped.",
      "LCD-A-09": "Round 2 (rule 8): a zero second part is dropped (“for 31 d”, “for 3 h”); LCD-A-08 likewise.",
    }.get(id, notes)
    if id in OVERRIDE_NOTE: notes = (OVERRIDE_NOTE[id] + " " + notes).strip()
    rows.append([id, surface, screen, loc, c, wv, reason, cn, wn, fits, notes])



S, M = "S", "M"   # LCD fonts: Inter 18 px, Inter 24 px

# =====================================================================================================
# LCD — walk-past screen
SC, LU, AL = "lib/lcdgfx/src/lcdgfx/screens.cpp", "lib/core/src/wineador/lcd_ui.cpp", "lib/core/src/wineador/alerts.cpp"
W = "LCD"
r("LCD-W-01", W, "Walk-past · all", SC, 'paintReading(c, r, "Cabinet"', "Cabinet", "Temperature", "inconsistent",
  "Pairs with “Humidity” beside it; both readings are the cabinet's, so “Cabinet” names the place, not the reading. Same word as the dashboard card (DB-OV-01).",
  lcd=(S, 272, None, None))
r("LCD-W-02", W, "Walk-past · all", SC, 'paintReading(c, r, "Humidity"', "Humidity", "=", "fine", "", lcd=(S, 176, None, None))
r("LCD-W-03", W, "Walk-past · readings", LU, 'snprintf(v.tempText', ("{mean:.1f}", "20.3"), "=", "fine", "Big number; °C drawn beside it. “--” when too few sensors or stale (spec 5.5).")
r("LCD-W-04", W, "Walk-past · readings", LU, 'snprintf(v.rhText', ("{mean:.0f}", "62"), "=", "fine", "")
r("LCD-W-05", W, "Walk-past · all", LU, '"Set %.1f %sC"', ("Set {t:.1f} °C", "Set 20.0 °C"), ("Target {t:.1f}°C", "Target 20.0°C"), "inconsistent",
  "Word list: Target, not Set. Rule 8: no space before °.", lcd=(S, 272, "Target 22.0°C", 16))
r("LCD-W-06", W, "Walk-past · all", LU, '"Set %.0f%%"', ("Set {rh}%", "Set 62%"), ("Target {rh}%", "Target 62%"), "inconsistent",
  "Word list: Target.", lcd=(S, 176, "Target 70%", 16))
for i, (m, w) in enumerate([("Cooling", "Cooling"), ("Heating", "Heating"), ("Humidifying", "Humidifying"), ("Drying", "Drying"),
                            ("Standby", "Standby"), ("Fault", "Fault")]):
    r(f"LCD-W-{7 + i:02d}", W, f"Walk-past · {m}", LU, f'addMode(ModeIcon::{"Fault" if m == "Fault" else "Standby"}, "{m}")' if m in ("Standby", "Fault") else f'case ModeIcon::{m}: return "{m}";',
      m, "=", "fine", "Mode word (word list). Also web/app.js MODES (DB-HDR-09), the same words." + (" Two at once: “Heating” + “Humidifying” = 16+56+86+28+56+133 = 375 of 480 px." if m == "Humidifying" else ""),
      lcd=(M, 400, None, 20))
r("LCD-W-13", W, "Walk-past · Starting", LU, '"Starting %s"', ("Starting {m:ss}", "Starting 4:38"), "=", "fine", "The countdown lives here and only here (rule 1; DB-OV-30 removed).", lcd=(M, 408, "Starting 4:59", 20))
r("LCD-W-14", W, "Walk-past · Updating", LU, '"Updating %u%%"', ("Updating {pct}%", "Updating 63%"), "=", "fine", "", lcd=(M, 408, "Updating 100%", 20))
r("LCD-W-15", W, "Walk-past · Allow update open", LU, '"Update allowed - %s left"', ("Update allowed - {m:ss} left", "Update allowed - 4:12 left"),
  ("Update allowed · {m:ss}", "Update allowed · 4:12"), "style", "A hyphen used as a separator; “left” is implied by a countdown. Same text on the dashboard pill (DB-OV-32). Test: test_lcd_ui:410 checks the old text.",
  lcd=(S, 464, "Update allowed · 4:59", 40))
r("LCD-W-16", W, "Walk-past · alert line", LU, '"+%u more"', ("+{n} more", "+2 more"), "=", "fine", "At most 14 (15 alert slots).", lcd=(S, 100, "+14 more", 16))
r("LCD-W-17", W, "Walk-past · Fault", LU, 'snprintf(v.alertText, sizeof(v.alertText), "%s", alerts_.at(0).title);',
  ("{alert title}", "Fault: chamber sensor"), ("{alert title without “Fault: ”}", "Chamber sensor"), "repeat",
  "[logic] While Fault shows, the mode row already says “Fault” just above: the line drops the prefix. Room with “+14 more”: 191 px; every fault title without its prefix fits (widest “Sensors recovering” 162 px). Other alerts unchanged (ALR rows).",
  lcd=(S, 191, "Sensors recovering", 44))
r("LCD-W-18", W, "Walk-past · all", SC, 'Canvas::measure(kFontSmall, "Settings")', "Settings", "=", "fine", "Button. Greyed while Updating.", lcd=(S, 80, None, None))
r("LCD-W-19", W, "Walk-past · build tag", SC, 't == BuildTag::Bench ? "BENCH"', "BENCH · TEST · BENCH TEST", "=", "fine",
  "Spec 5.5 names the tag BENCH or TEST in capitals. (The word list's “Bench build · Test build” is for the dashboard's Running row, DB-UP-12.)", lcd=(S, 180, "BENCH TEST", None))
r("LCD-W-20", W, "Walk-past · clock not trusted", "lib/core/src/wineador/lcd_ui.h", 'char timeText[8] = "--:--";', "--:--", "=", "fine", "Spec 5.5.")
r("LCD-W-21", W, "Walk-past · readings unknown", "lib/core/src/wineador/lcd_ui.h", 'char tempText[8] = "--";', "--", "=", "fine", "Spec 5.5.")

# LCD — settings
St = "LCD"
r("LCD-S-01", St, "Settings", SC, 'c.text(kFontMedium, 16, midBaseline(kFontMedium, r), "Settings", pal::kText);', "Settings", "=", "fine", "Same word as the button that opens it.", lcd=(M, 300, None, None))
r("LCD-S-02", St, "Settings · edited", SC, '"Not saved yet"', "Not saved yet", "Not saved", "too long", "", lcd=(S, 200, None, None))
r("LCD-S-03", St, "Settings", SC, 'temp ? "Temperature" : "Humidity"', "Temperature · Humidity", "=", "fine",
  "“Target temperature” (217 px) doesn't fit the 184 px label room; the screen is only the two targets, so the bare words are clear. Proposal for the word list: allow “Temperature”/“Humidity” as the label where “Target” is the screen's whole subject (see word-list notes).",
  lcd=(M, 184, "Temperature", None))
r("LCD-S-04", St, "Settings", SC, 'snprintf(buf, sizeof(buf), "15%s22 %sC", kDash, kDeg);', "15–22 °C", "15–22°C", "style", "Rule 8: no space before °. [logic] Also: the bounds are typed here as literals; take them from settings.h (kTargetTempMinC…) so they can't drift.",
  lcd=(S, 184, None, None))
r("LCD-S-05", St, "Settings", SC, 'else snprintf(buf, sizeof(buf), "55%s70%%", kDash);', "55–70%", "=", "fine", "Same literal-bounds note as LCD-S-04.", lcd=(S, 184, None, None))
r("LCD-S-06", St, "Settings", SC, 'temp ? "steps of 0.5" : "steps of 1"', "steps of 0.5 · steps of 1", None, "repeat",
  "The − and + buttons already move one step; the number shows it. [layout] The range line moves down to sit centred.")
r("LCD-S-07", St, "Settings · window closed", "lib/core/src/wineador/lcd_ui.h", 'char allowText[24] = "Allow update";', "Allow update", "=", "fine", "Spec 5.5's name.", lcd=(M, 160, None, 24))
r("LCD-S-08", St, "Settings · window open", LU, 'snprintf(v.allowText, sizeof(v.allowText), "Update allowed");', "Update allowed", "=", "fine",
  "Same words as the walk-past line (LCD-W-15).", lcd=(S, 160, None, 24))
r("LCD-S-09", St, "Settings · window open", LU, 'snprintf(v.allowSub, sizeof(v.allowSub), "%s", c);', ("{m:ss}", "4:59"), "=", "fine", "", lcd=(M, 160, "4:59", 12))
r("LCD-S-10", St, "Settings", SC, 'button(c, L::kCancel, pal::kSurface, "Cancel"', "Cancel", "=", "fine", "", lcd=(M, 110, None, None))
r("LCD-S-11", St, "Settings", SC, 'pal::kSurface, "Save", v.draftChanged', "Save", "=", "fine", "", lcd=(M, 110, None, None))
r("LCD-S-12", St, "Settings", SC, 'textCentred(c, kFontSmall, val, (int16_t)(r.y + 76), buf, pal::kMuted);', "°C · % (under the value)", "=", "fine", "")

# LCD — alerts
r("LCD-A-01", W, "Alerts", SC, '"Alerts", pal::kText', "Alerts", "=", "fine", "", lcd=(M, 200, None, None))
r("LCD-A-02", W, "Alerts · more than 5", SC, '"%u active %s page %u of %u"', ("{n} active · page {p} of {k}", "15 active · page 1 of 3"), "=", "fine", "", lcd=(S, 260, "15 active · page 3 of 3", 64))
r("LCD-A-03", W, "Alerts · 1–5", SC, 'snprintf(buf, sizeof(buf), "%u active", (unsigned)v.alertCount);', ("{n} active", "3 active"), "=", "fine", "", lcd=(S, 260, None, 64))
r("LCD-A-04", W, "Alerts · none", SC, '"No active alerts"', "No active alerts", "=", "fine", "Spec 5.5's own words.", lcd=(M, 448, None, None))
r("LCD-A-05", W, "Alerts", SC, 'c.text(kFontMedium, (int16_t)(x + 30), midBaseline(kFontMedium, b), "Back"', "Back", "=", "fine", "", lcd=(M, 86, None, None))
r("LCD-A-06", "LCD + dashboard", "Alerts · clock not trusted", AL, 'snprintf(out, n, "for <1 min");', "for <1 min", "=", "fine", "Spec 5.5 “for 12 min”.", lcd=(S, 140, None, 20))
r("LCD-A-07", "LCD + dashboard", "Alerts · clock not trusted", AL, 'snprintf(out, n, "for %u min"', ("for {m} min", "for 12 min"), "=", "fine", "", lcd=(S, 140, "for 59 min", 20))
r("LCD-A-08", "LCD + dashboard", "Alerts · clock not trusted", AL, '"for %u h %u min"', ("for {h} h {m} min", "for 3 h 5 min"), "=", "fine",
  "[logic] Drop “0 min” (“for 3 h”), as rule 8's “3 h 5 min” implies.", lcd=(S, 140, "for 23 h 59 min", 20))
r("LCD-A-09", "LCD + dashboard", "Alerts · clock not trusted", AL, '"for %u d %u h"', ("for {d} d {h} h", "for 2 d 4 h"), "=", "fine", "Drop “0 h”. See finding on rule 8 (days).", lcd=(S, 140, "for 99 d 23 h", 20))
r("LCD-A-10", "LCD + dashboard", "Alerts", AL, 'snprintf(out, n, "%02d:%02d", at.hour, at.minute);', ("{hh:mm} today / {d} {Mon} {hh:mm}", "13 Nov 14:32"), "=", "fine", "Spec 5.5.", lcd=(S, 140, "30 Sep 23:59", 20))

# LCD — boot splash, BP2 test 29
r("LCD-B-01", W, "Boot splash", LU, 'snprintf(v.version, sizeof(v.version), "%s", version ? version : "");', ("{version}", "2.0.0"), "=", "fine",
  "Bench and test builds show “2.0.0-BENCH-TEST” (the version string's own suffix, makeVersionString): left as it is, it is the build identity.", lcd=(S, 480, "2.0.0-BENCH-TEST", 28))
r("LCD-T-01", "LCD (BP2 test 29 only)", "Landscape check", SC, '"UP", pal::kText', "UP", "=", "fine", "Bench test screen, not the product; listed for completeness.")
r("LCD-T-02", "LCD (BP2 test 29 only)", "Landscape check", SC, '"%s: touch each corner"', ("{way}: touch each corner", "way A: touch each corner"), "=", "fine", "Bench test screen.")
r("LCD-T-03", "LCD (BP2 test 29 only)", "Landscape check", SC, '{"top left", "top right", "bottom left", "bottom right"}', "top left · top right · bottom left · bottom right", "=", "fine", "Bench test screen.")

# =====================================================================================================
# Alert titles: firmware, shown on the LCD (walk-past line, alerts screen) and the dashboard (header via
# status.fault, Overview's alert list). LCD alerts-screen room: 416 − “when” width; worst “for 23 h 59 min” → 284 px (Inter 24).
A = "LCD + dashboard"
r("ALR-01", A, "Alerts · Fault (chamber)", AL, '"Fault: chamber sensor"', "Fault: chamber sensor", "=", "fine",
  "Walk-past line shows “Chamber sensor” (LCD-W-17). Test: test_lcd_ui:123.", lcd=(M, 284, None, 44))
r("ALR-02", A, "Alerts · Fault (cabinet)", AL, '"Fault: cabinet sensors"', "Fault: cabinet sensors", "Fault: too few sensors", "jargon",
  "Says what is wrong (fewer than 3 healthy), not just where. Each excluded sensor also has its own row. Test: test_lcd_ui:126.", lcd=(M, 284, None, 44))
r("ALR-03", A, "Alerts · Fault (recovery hold)", AL, '"Fault: sensor recovery"', "Fault: sensor recovery", "Fault: sensors recovering", "style",
  "The 5-minute recovery hold (4.3): a state, said as one. Test: test_lcd_ui:130.", lcd=(M, 284, None, 44))
r("ALR-04", A, "Alerts · Fault (fan)", AL, '"Fault: plenum fan stall"', "Fault: plenum fan stall", "Fault: plenum fan stalled", "style", "", lcd=(M, 284, None, 44))
r("ALR-05", A, "Alerts · Fault (settings)", AL, '"Fault: settings check"', "Fault: settings check", "Fault: bad settings", "jargon",
  "The stored settings failed their check at boot; “settings check” names the test, not the problem.", lcd=(M, 284, None, 44))
r("ALR-06", A, "Alerts · 12V dip", AL, '"12V supply dip"', "12V supply dip", "=", "fine", "", lcd=(M, 284, None, 44))
r("ALR-07", A, "Alerts · heater", AL, '"Heater ineffective"', "Heater ineffective", "Heater not working", "jargon",
  "[new word] “Not working” for “ineffective” (word-list proposal).", lcd=(M, 284, None, 44))
r("ALR-08", A, "Alerts · cooling", AL, '"Cooling ineffective"', "Cooling ineffective", "Cooling not working", "jargon", "[new word] as ALR-07.", lcd=(M, 284, None, 44))
r("ALR-09", A, "Alerts · humidifier", AL, '"Humidifier ineffective"', "Humidifier ineffective", "Humidifier not working", "jargon",
  "[new word] as ALR-07. The likely cause is an empty tank (5.6); a hint (“check the water”) would make it actionable (rule 5) but won't fit one line: left to Josh.",
  lcd=(M, 284, None, 44))
r("ALR-10", A, "Alerts · SD", AL, '"SD card failure"', "SD card failure", "SD card failed", "style", "A state, like the others.", lcd=(M, 284, None, 44))
r("ALR-11", A, "Alerts · clock", AL, '"Clock lost or not set"', "Clock lost or not set", "Clock not set", "too long",
  "Either way the time isn't trusted and shows “--:--”. Test: test_lcd_ui:472.", lcd=(M, 284, None, 44))
r("ALR-12", A, "Alerts · sensor excluded", AL, '"Excluded: %s%s"', ("Excluded: {position}{ (manual)}", "Excluded: mid_rear"), ("Excluded: {Position}", "Excluded: bottom front"), "jargon",
  "[logic][new word] Code name → plain position (word list: Top front, Middle rear …; positions table needed, see word list). “(manual)” dropped: “Excluded by hand: bottom front” (346 px) doesn't fit, and today's “Excluded: bottom_front (manual)” (360 px) already doesn't — it is cut with “...”. By-hand shows on Sensors (DB-SE-12). Tests: test_lcd_ui:93, :114. Dashboard alert-list room is wide; walk-past line: 193 of 203 px with “+3 more”, 2 px over with “+10 more” or more.",
  lcd=(M, 284, "Excluded: bottom front", 44))

# =====================================================================================================
# Dashboard — header and every page
IX, JS, IC = "web/index.html", "web/app.js", "web/image_check.js"
H = "dashboard · every page"
for i, (p, nth) in enumerate([("Overview", 1), ("Graphs", 1), ("Sensors", 1), ("Events", 1), ("Settings", 1), ("Logs", 1), ("Update", 1)]):
    r(f"DB-HDR-{i + 1:02d}", H, "Tabs", IX, f'<span>{p}</span>', p, "=", "fine", "Spec 5.5's page names.")
r("DB-HDR-08", H, "Header", IX, 'aria-label="openHumidor: Overview"', "openHumidor: Overview (logo, screen reader)", "=", "fine", "")
r("DB-HDR-08b", H, "Tabs and range (screen reader)", IX, 'aria-label="Pages"', "Pages · Range", "=", "fine", "")
r("DB-HDR-09", H, "Header · modes", JS, 'cooling: { icon: "ac_unit"', "Cooling · Heating · Humidifying · Drying · Standby · Starting · Updating · Fault", "=", "fine", "Same as the LCD (LCD-W-07…12).")
r("DB-HDR-10", H, "Header · Starting / Updating", JS, '` ${s.update.pct} %`', ("Starting {m:ss} · Updating {pct} %", "Updating 43 %"), ("Starting {m:ss} · Updating {pct}%", "Updating 43%"), "style",
  "Rule 8: no space before %. Matches LCD-W-14.")
r("DB-HDR-11", H, "Header · build tag", JS, '"BENCH TEST" : "BENCH"', "BENCH · TEST · BENCH TEST", "=", "fine", "As LCD-W-19.")
r("DB-HDR-12", H, "Header · time", IX, '<time id="hdr-time">--:--</time>', "--:--", "=", "fine", "")
r("DB-HDR-13", H, "Header · first load", IX, 'id="hdr-fresh">connecting<', "connecting", "Connecting", "style", "Rule 7: sentence case.")
r("DB-HDR-14", H, "Header · fresh", JS, '"updated just now"', "updated just now", "Updated just now", "style", "")
r("DB-HDR-15", H, "Header · fresh", JS, '`updated ${ago(s)} ago`', ("updated {ago} ago", "updated 6 s ago"), ("Updated {ago} ago", "Updated 6 s ago"), "style", "")
r("DB-HDR-16", H, "Note · busy", JS, '"The board is busy with a download or an update; trying again shortly."', "The board is busy with a download or an update; trying again shortly.",
  "Board busy. Trying again.", "too long", "Spec 5.5 says the page shows “board busy”.")
r("DB-HDR-17", H, "Note · no answer", JS, "`The board isn't answering. Showing what it said at ${status?.time?.uk",
  ("The board isn't answering. Showing what it said at {hh:mm}; trying again.", "The board isn't answering. Showing what it said at 14:32; trying again."),
  ("Board not answering. Figures from {hh:mm}; trying again.", "Board not answering. Figures from 14:32; trying again."), "too long", "")
r("DB-HDR-18", H, "Note · no answer yet", JS, '"The board isn\'t answering yet; trying again."', "The board isn't answering yet; trying again.", "Board not answering yet. Trying again.", "style", "")
r("DB-HDR-19", H, "Note · API version", JS, "`The board speaks API version ${body.api}",
  ("The board speaks API version {n}; this page knows version {m}. Reload the page.", "The board speaks API version 5; this page knows version 4. Reload the page."),
  "This page is out of date. Reload it.", "jargon", "Rule 5: what happened, what to do. The version numbers are for tools.")
r("DB-HDR-20", H, "Browser tab title", JS, 'document.title = p === "overview"', ("openHumidor / {Page} · openHumidor", "Events · openHumidor"), "=", "fine", "")

# Dashboard — Overview
O = "dashboard · Overview"
r("DB-OV-01", O, "Temperature card", IX, '<h2>Cabinet temperature</h2>', "Cabinet temperature", "Temperature", "repeat",
  "Every reading on Overview is the cabinet's. Same word as the LCD (LCD-W-01). DESIGN.md's label list names “Cabinet temperature”: update it in round 2.")
r("DB-OV-02", O, "Humidity card", IX, '<h2>Cabinet humidity</h2>', "Cabinet humidity", "Humidity", "repeat", "As DB-OV-01.")
r("DB-OV-03", O, "Temperature · Fault", JS, '`<b class="c-fault">Fault</b> — ', ("Fault — {cause}. The loads are held until it clears.", "Fault — chamber sensor. The loads are held until it clears."), None, "repeat",
  "Said by the header's Fault and the alert list (ALR-01…05). See finding F1 (DESIGN.md's Number and Reason Rule).")
r("DB-OV-04", O, "Temperature · Updating", JS, '`<b class="c-acc">Updating</b> — every load', "Updating — every load is off until the update has finished.", None, "repeat", "Header says Updating 43% (DB-HDR-10).")
r("DB-OV-05", O, "Temperature · Starting", JS, '`<b class="c-acc">Starting</b> — the loads wait', ("Starting — the loads wait {m:ss} after a boot.", "Starting — the loads wait 4:38 after a boot."), None, "repeat",
  "Header says Starting 4:38. Today Starting is said four times on Overview (header, pill DB-OV-30, this line, DB-OV-12).")
r("DB-OV-06", O, "Temperature · no reading", JS, 'No reading: too few healthy cabinet sensors, or none read recently.', "No reading: too few healthy cabinet sensors, or none read recently.", None, "repeat",
  "The number shows “--” and the line under the bar says why (DB-OV-15).")
r("DB-OV-07", O, "Temperature · Drying", JS, 'the compressor runs to take water out (an RH strip).', "Drying — the compressor runs to take water out (an RH strip).", None, "repeat",
  "Spec rule 1's own example: header and humidity bar say it.")
r("DB-OV-08", O, "Temperature · Cooling", JS, '`<b class="c-cool">Cooling</b> — started above',
  ("Cooling — started above {set+h} °C (set {set} + {h}); runs until {set} °C.", "Cooling — started above 20.5 °C (set 20.0 + 0.5); runs until 20.0 °C."), None, "repeat",
  "Header says Cooling; the bar's ticks show 20.5 and the target. Rule 2: no explaining how the firmware works.")
r("DB-OV-09", O, "Temperature · Heating", JS, '`<b class="c-heat">Heating</b> — started below',
  ("Heating — started below {set−d} °C (set {set} − {d}); runs until {set−s} °C.", "Heating — started below 18.5 °C (set 20.0 − 1.5); runs until 19.0 °C."), None, "repeat",
  "As DB-OV-08. Note: “runs until 19.0” is the one figure not on the bar; it is the tuning row DB-ST-T03.")
r("DB-OV-10", O, "Temperature · cooling wanted", JS, 'Cooling wanted</b> — ${esc(heldText(g))}', ("Cooling wanted — held: {reason} {m:ss}.", "Cooling wanted — held: min-off 3:12."), None, "repeat",
  "The compressor row says it (DB-OV-41: “Waiting · rest 3:12”).")
r("DB-OV-11", O, "Temperature · above, starting", JS, '— cooling starts on the next pass.', ("Above {x} °C — cooling starts on the next pass.", "Above 20.5 °C — cooling starts on the next pass."), None, "repeat",
  "Lasts one pass; the marker in the Cooling zone says it.")
r("DB-OV-12", O, "Temperature · heater off in Settings", JS, '— the heater is switched off in Settings.', ("Below {x} °C — the heater is switched off in Settings.", "Below 18.5 °C — the heater is switched off in Settings."), None, "repeat",
  "[logic] Moves to the heater row: “Off in Settings” (DB-OV-45), shown whenever the switch is off, not only when cold.")
r("DB-OV-13", O, "Temperature · heating wanted", JS, 'Heating wanted</b> — ${esc(heldText(g))}', ("Heating wanted — held: {reason} {m:ss}.", "Heating wanted — held: crossover 9:33."), None, "repeat", "Heater row: “Waiting · changeover 9:33” (DB-OV-44).")
r("DB-OV-14", O, "Temperature · below, starting", JS, '— heating starts on the next pass.', ("Below {x} °C — heating starts on the next pass.", "Below 18.5 °C — heating starts on the next pass."), None, "repeat", "As DB-OV-11.")
r("DB-OV-15", O, "Temperature · no reading (line under bar)", JS, '`${c.healthy} of 4 cabinet sensors healthy`', ("{n} of 4 cabinet sensors healthy", "2 of 4 cabinet sensors healthy"),
  ("{n} of 4 sensors healthy / No fresh reading", "2 of 4 sensors healthy"), "repeat",
  "[logic] When 3+ are healthy but the reading is stale it says “4 of 4 healthy” under “--”, which explains nothing: say “No fresh reading” then. “cabinet” dropped (the card is the cabinet).")
r("DB-OV-16", O, "Temperature · line under bar", JS, '°C across the cabinet sensors · spread ${fmt(c.spread_t)}',
  ("{min}–{max} °C across the cabinet sensors · spread {s} · {n} of 4 healthy · read {ago} ago", "18.1–18.4 °C across the cabinet sensors · spread 0.3 · 4 of 4 healthy · read 1 s ago"),
  ("Spread {s}°C ({min}–{max}) · {n} of 4 sensors · read {ago} ago", "Spread 0.3°C (18.1–18.4) · 4 of 4 sensors · read 1 s ago"), "repeat",
  "Min–max and spread are one fact said twice; spread leads (it drives the blowers, DB-OV-48). Spread was unitless.")
r("DB-OV-17", O, "Temperature bar · zone", JS, 'label: `Heat below ${fmt(st - db)}`', ("Heat below {x}", "Heat below 18.5"), "Heating", "repeat",
  "The tick under the edge shows 18.5; the zone needs only its mode word (word list). DESIGN.md's band description to follow in round 2.")
r("DB-OV-18", O, "Temperature bar · zone (phone)", JS, 'short: `Heat < ${fmt(st - db)}`', ("Heat < {x}", "Heat < 18.5"), "Heating", "repeat", "Phone and desktop the same word.")
r("DB-OV-19", O, "Temperature bar · zone", JS, '"Dead zone · nothing starts"', "Dead zone · nothing starts", "Holding", "jargon", "Word list.")
r("DB-OV-20", O, "Temperature bar · zone (phone)", JS, 'short: "Dead zone"', "Dead zone", "Holding", "jargon", "Word list.")
r("DB-OV-21", O, "Temperature bar · zone", JS, 'label: `Cool above ${fmt(st + hyst)}`', ("Cool above {x}", "Cool above 20.5"), "Cooling", "repeat", "As DB-OV-17.")
r("DB-OV-22", O, "Temperature bar · zone (phone)", JS, 'short: `Cool > ${fmt(st + hyst)}`', ("Cool > {x}", "Cool > 20.5"), "Cooling", "repeat", "")
r("DB-OV-23", O, "Both bars · target tick", JS, 'text: `${fmt(st)} set`', ("{set} set", "20.0 set"), ("{target} target", "20.0 target"), "inconsistent", "Word list: Target. Same for the humidity bar (DB-OV-29).")
r("DB-OV-24", O, "Bars · screen reader", JS, 'el.setAttribute("aria-label"', ("{Temperature band}: {value}{unit}, {zones}", "Temperature band: 18.3°C, Heat below 18.5, Dead zone · nothing starts, Cool above 20.5 (also Humidity band)"),
  ("{Temperature}: {value}{unit}, {zone it is in}", "Temperature: 18.3°C, heating"), "too long", "Name the zone the reading is in, not every zone.")
r("DB-OV-25", O, "Bars · screen reader", JS, '"no reading"', "no reading", "=", "fine", "")
r("DB-OV-26", O, "Humidity bar · zone", JS, 'label: `Humidify below ${fmt(sr - b, 0)}`', ("Humidify below {x}", "Humidify below 60"), "Humidifying", "repeat", "")
r("DB-OV-27", O, "Humidity bar · zone (phone)", JS, 'short: `< ${fmt(sr - b, 0)}`', ("< {x}", "< 60"), "Humidifying", "inconsistent", "Phone said only “< 60”.")
r("DB-OV-28", O, "Humidity bar · zone", JS, 'label: `Strip above ${fmt(sr + off, 0)}`', ("Strip above {x}", "Strip above 67"), "Drying", "jargon", "Word list: Drying, not strip. Spec rule 1: the bar is one of Drying's two places.")
r("DB-OV-29", O, "Humidity bar · zone (phone)", JS, 'short: `Strip > ${fmt(sr + off, 0)}`', ("Strip > {x}", "Strip > 67"), "Drying", "jargon", "")
r("DB-OV-29b", O, "Humidity bar · zone", JS, 'label: "Holding", short: "Holding"', "Holding", "=", "fine", "Word list.")
r("DB-OV-30", O, "Pill · Starting", JS, '<span>Starting: the loads wait ${mss(s.boot_hold_s)}</span>', ("Starting: the loads wait {m:ss}", "Starting: the loads wait 4:38"), None, "repeat", "Header (DB-HDR-10).")
r("DB-OV-31", O, "Pill · Updating", JS, '<span>Updating ${s.update.pct} %</span>', ("Updating {pct} %", "Updating 43 %"), None, "repeat", "Header.")
r("DB-OV-32", O, "Pill · window open", JS, '<span>Update allowed: ${mss(s.update.window_s)} left · <a href="#update">Update</a></span>',
  ("Update allowed: {m:ss} left · Update", "Update allowed: 4:12 left · Update"), ("Update allowed · {m:ss}", "Update allowed · 4:12"), "style",
  "[layout] The whole pill links to the Update page. Same words as LCD-W-15.")
r("DB-OV-33", O, "Humidity · waiting", JS, 'Waiting with the other loads.', "Waiting with the other loads.", None, "repeat", "Header says Fault / Updating / Starting.")
r("DB-OV-34", O, "Humidity · no reading", JS, 'No reading.</span>`;\n  if (L.humidifier.on)', "No reading.", None, "repeat", "The number shows “--”.")
r("DB-OV-35", O, "Humidity · Humidifying", JS, '`<b class="c-humid">Humidifying</b> — started below',
  ("Humidifying — started below {x} % (set {set} − {b}); runs until {set} %.", "Humidifying — started below 60 % (set 62 − 2); runs until 62 %."), None, "repeat", "Header and bar.")
r("DB-OV-36", O, "Humidity · Drying", JS, 'the strip ends when the water is back to target.',
  ("Drying — above {x} % (set {set} + {o}); the strip ends when the water is back to target.", "Drying — above 67 % (set 62 + 5); the strip ends when the water is back to target."), None, "repeat", "Spec rule 1's example.")
r("DB-OV-37", O, "Humidity · humidifier off in Settings", JS, '— the humidifier is switched off in Settings.', ("Below {x} % — the humidifier is switched off in Settings.", "Below 60 % — the humidifier is switched off in Settings."), None, "repeat",
  "[logic] Humidifier row: “Off in Settings” (DB-OV-46).")
r("DB-OV-38", O, "Humidity · humidifying wanted / waiting", JS, 'Humidifying wanted</b> — ${esc(heldText(g))}', ("Humidifying wanted — held: {reason} / Below {x} % — humidifying starts after {n} min below.", "Below 60 % — humidifying starts after 10 min below."), None, "repeat",
  "Humidifier row says a wait. The 10-minute persistence is a tuning value (DB-ST-T17).")
r("DB-OV-39", O, "Humidity · above, waiting", JS, '— an RH strip starts if it stays above for', ("Above {x} % — an RH strip starts if it stays above for {n} min.", "Above 67 % — an RH strip starts if it stays above for 30 min."), None, "repeat",
  "The marker sits in the Drying zone; persistence is DB-ST-T14. See finding F1.")
r("DB-OV-40", O, "Humidity · holding", JS, 'Holding, between ${fmt(set - bandPts, 0)} and', ("Holding, between {lo} and {hi} %.", "Holding, between 60 and 67 %."), None, "repeat", "The bar's Holding zone.")
r("DB-OV-40b", O, "Alerts", IX, '<h2 id="h-alerts">Alerts</h2>', "Alerts", "=", "fine", "")
r("DB-OV-40c", O, "Alerts · none", JS, '`<li class="none">No active alerts.</li>`', "No active alerts.", "No active alerts", "style", "Rule 7: no full stop on a phrase. Same as LCD-A-04.")
r("DB-OV-40d", O, "Loads", IX, '<h2 id="h-loads">Loads</h2>', "Loads", "=", "fine", "")
r("DB-OV-41", O, "Loads · compressor", JS, '`on ${ago(L.compressor.for_s)}${L.compressor.stripping ? " · RH strip" : ""}`',
  ("on {ago}{ · RH strip}", "on 7 min · RH strip"), ("On for {ago}", "On for 7 min"), "repeat",
  "Spec rule 1: the compressor row says only how long it has run (the row's icon and colour already turn to Drying).")
r("DB-OV-42", O, "Loads · compressor off", JS, '`off · stopped ${ago(L.compressor.stopped_ago_s)} ago`', ("off · stopped {ago} ago", "off · stopped 30 min ago"), ("Off for {ago}", "Off for 30 min"), "repeat",
  "“off” and “stopped” say the same. Spec Display: time since the last compressor stop.")
r("DB-OV-43", O, "Loads · off", JS, '"off", !L.compressor.on && gc', "off (compressor, heater, humidifier)", "Off", "style", "Rule 7.")
r("DB-OV-44", O, "Loads · waiting", JS, 'const heldText = (g) => `held: ${g.reason}', ("held: {reason code} {m:ss}", "held: crossover 9:33"), ("Waiting · {reason} {m:ss}", "Waiting · changeover 9:33"), "jargon",
  "[logic][new word] Word list: Waiting, not held; spec rule 4's own example. The reason codes become words (GRD rows) — a map in app.js, the API keeps its codes.")
r("DB-OV-45", O, "Loads · heater", JS, '`on · ${L.heater.duty_pct} %`', ("on · {duty} %", "on · 60 %"), ("On · {duty}%", "On · 60%"), "style",
  "Rule 8. [logic] Plus “Off in Settings” when heater_enable is off (replaces DB-OV-12; from the settings already fetched).")
r("DB-OV-46", O, "Loads · humidifier", JS, 'L.humidifier.on ? "on"', "on", "On", "style", "[logic] Plus “Off in Settings” when humidifier_enable is off (replaces DB-OV-37).")
r("DB-OV-47", O, "Loads · plenum fan", JS, '`${L.fan.pwm} % · ${num(L.fan.rpm)} rpm`', ("{pwm} % · {rpm} rpm", "20 % · 1,060 rpm"), ("{pwm}% · {rpm} rpm", "20% · 1,060 rpm"), "style", "Rule 8.")
r("DB-OV-47b", O, "Loads · labels", JS, 'row("Plenum fan"', "Compressor · Heater · Humidifier · Plenum fan · Interlock · Blowers · Ion generator", "Compressor · Heater · Humidifier · Plenum fan · Power to · Blowers · Ion generator", "jargon", "Spec word list: Power to, not Interlock.")
r("DB-OV-48", O, "Loads · interlock", JS, '`${L.interlock} side`', ("{cooling|heating} side", "cooling side"), ("{Cooling|Heating} side", "Cooling side"), "style", "")
r("DB-OV-49", O, "Loads · blowers idle (unset)", JS, '`idle (${B.pwm} % until BP2 test 22)`', ("idle ({pwm} % until BP2 test 22)", "idle (100 % until BP2 test 22)"), ("Idle · {pwm}%", "Idle · 100%"), "jargon",
  "Rule 3: no test numbers. Josh knows why it is 100% until test 22.")
r("DB-OV-50", O, "Loads · blowers idle", JS, '`${B.pwm} % · idle`', ("{pwm} % · idle", "45 % · idle"), ("Idle · {pwm}%", "Idle · 45%"), "inconsistent", "State first (rule 4).")
r("DB-OV-51", O, "Loads · blowers while updating", JS, '`${B.pwm} % · idle while updating`', ("{pwm} % · idle while updating", "45 % · idle while updating"), ("Idle · {pwm}%", "Idle · 45%"), "repeat", "Header says Updating.")
r("DB-OV-52", O, "Loads · blowers full speed", JS, '`full speed · ${B.why === "fault"', ("full speed · {fault|no fresh reading|no reading yet}", "full speed · no fresh reading"),
  ("Full speed{ · no fresh reading|no reading yet}", "Full speed · no fresh reading"), "repeat", "No reason for a fault (the header and alerts say Fault).")
r("DB-OV-53", O, "Loads · blowers boost", JS, '`boost · spread ${fmt(B.spread_t, 2)} °C',
  ("boost · spread {t} °C, {rh} pts · idle in {m:ss}", "boost · spread 0.35 °C, 0.7 pts · idle in 9:38"), ("Boost{ · idle in {m:ss}}", "Boost · idle in 9:38"), "too long",
  "CLAUDE's “dense rows” example. The temperature spread is said once, under the temperature bar (DB-OV-16). See finding F7 (humidity spread).")
r("DB-OV-54", O, "Loads · ion running", JS, '`on · ${mss(I.left_s)} left`', ("on · {m:ss} left", "on · 1:23 left"), ("On · {m:ss} left", "On · 1:23 left"), "style", "")
r("DB-OV-55", O, "Loads · ion waiting", JS, '`off · next run ${esc(I.next)}`', ("off · next run {hh:mm}", "off · next run 12:00"), ("Next run {hh:mm}", "Next run 12:00"), "repeat", "“off” is implied by a next run.")
r("DB-OV-56", O, "Loads · ion waiting, clock not trusted", JS, '(from boot: the time isn\'t trusted)', ("off · next run in {ago} (from boot: the time isn't trusted)", "off · next run in 3 h 5 min (from boot: the time isn't trusted)"),
  ("Next run in {ago}", "Next run in 3 h 5 min"), "too long", "The Clock not set alert says why.")
r("DB-OV-57", O, "Loads · ion, last skipped", JS, '` · last run skipped${I.last_skipped.at', ("· last run skipped ({hh:mm}): {why}", " · last run skipped (00:00): boot hold"), None, "too long",
  "On Events as its own sentence (EVT-ION-04). [api] If Josh wants to keep it, “Skipped 00:00 · starting”.")
r("DB-OV-58", O, "Loads · ion waiting (held)", JS, '`held: ${esc(I.why)}${I.next ?', ("held: {why} · next {hh:mm}", "held: cap record can't be saved · next 12:00"), ("Waiting · {why}", "Waiting · can't save its record"), "jargon",
  "Word list: Waiting. Why words: ION rows.")
r("DB-OV-59", O, "Loads · ion off", JS, '"switched off on Settings"', "switched off on Settings", "Off in Settings", "style", "Word list: Off.")
r("DB-OV-60", O, "Loads · ion no runs", JS, '"no runs set on Settings"', "no runs set on Settings", "No runs set", "too long", "")
r("DB-OV-61", O, "Loads · ion at boot", JS, '"off · starting"', "off · starting", "Off", "repeat", "Header says Starting.")
r("DB-OV-62", O, "Last 7 days", IX, '<h2 id="h-week">Last 7 days</h2>', "Last 7 days", "=", "fine", "")
r("DB-OV-63", O, "Last 7 days", JS, 'wk("Crossovers", w.crossovers)', "Crossovers", "Changeovers", "inconsistent", "Word list.")
r("DB-OV-64", O, "Last 7 days", JS, 'wk("RH strips", w.strips)', "RH strips", "Drying runs", "jargon", "[new word] “Drying runs” (word list has Drying; the count needs a noun).")
r("DB-OV-65", O, "Last 7 days", JS, 'wk("Humidifier runs", w.humid_runs)', "Humidifier runs", "=", "fine", "")
r("DB-OV-66", O, "Last 7 days", JS, '<div><dt>Disturbances</dt>', "Disturbances", "=", "fine", "")
r("DB-OV-67", O, "Last 7 days · detection off", JS, '"detection not switched on"', "detection not switched on", "Off", "too long", "Word list's own example.")
r("DB-OV-68", O, "Last 7 days · counting", JS, 'wk = (label, v) => `<div><dt>${label}</dt><dd>${v === null ? "counting…"', "counting…", "Counting…", "style", "")
r("DB-OV-69", O, "Board", IX, '<h2 id="h-board">Board</h2>', "Board", "=", "fine", "")
r("DB-OV-70", O, "Board", JS, '["Firmware", esc(brd.version)]', ("Firmware · {version}", "2.0.0"), "=", "fine", "")
r("DB-OV-71", O, "Board", JS, '["Running for", ago(brd.uptime_s)]', ("Running for · {ago}", "31 d 0 h"), ("Running for · {ago}", "31 d"), "style",
  "[logic] ago(): drop a zero second part (“31 d”, “3 h”), rule 8. Shared helper: also DB-HDR-15, DB-SE-09, DB-OV-42, DB-OV-56.")
r("DB-OV-72", O, "Board", JS, '["Last restart", esc(brd.restart_reason || "--")]', ("Last restart · {code}", "POWERON"), ("Last restart · {words}", "Power on or reset"), "jargon",
  "Word list. [logic] A map in app.js from the API's code (RST rows).")
r("DB-OV-73", O, "Board", JS, '["12 V supply"', ("12 V supply · {v} V · lowest today {v} V", "12.12 V · lowest today 11.94 V"), ("12V supply · {v}V · lowest today {v}V", "12.12V · lowest today 11.94V"), "inconsistent",
  "The alert says “12V”; the page said “12 V”. See finding F5 (volts in rule 8).")
r("DB-OV-74", O, "Board", JS, '["WiFi signal"', ("WiFi signal · {dBm} dBm · {good|fair|weak}", "-58 dBm · good"), ("WiFi signal · {Good|Fair|Weak} · {dBm} dBm", "Good · −58 dBm"), "style", "State first (rule 4); a real minus sign.")
r("DB-OV-75", O, "Board", JS, '["Free memory"', ("Free memory · {n} KB", "175 KB"), "=", "fine", "")
r("DB-OV-76", O, "Board", JS, '["SD card", brd.card_free_bytes === null ? "not available"', ("SD card · {free} free of {total} / not available", "59.2 GB free of 59.7 GB"), ("SD card · {free} free of {total} / Not available", "59.2 GB free of 59.7 GB"), "style", "Sentence case for the “Not available” case.")

# Guard reasons (API codes → words in app.js; the same words in the event sentences, EVT-GRD)
G = "lib/core/src/wineador/guards.cpp"
GO = "dashboard · Overview (Loads) + Events"
for i, (code, word, why, note) in enumerate([
    ("settings-problem", "settings", "jargon", "A Fault shows too."),
    ("updating", "updating", "fine", ""),
    ("boot-hold", "starting", "jargon", "Word list: Starting, not boot hold."),
    ("relay-exercise", "relay check", "jargon", "[new word] “Relay check” for relay exercise (word-list proposal)."),
    ("disabled", "(row says “Off in Settings”)", "jargon", "Word list: Off."),
    ("sensors", "too few sensors", "jargon", ""),
    ("fan", "fan stalled", "jargon", ""),
    ("cabinet-ceiling", "cabinet too warm", "jargon", ""),
    ("chamber-ceiling", "chamber too warm", "jargon", ""),
    ("rh-ceiling", "humidity limit", "jargon", ""),
    ("heater-ineffective", "retry", "jargon", "The heater is waiting to try again after not working; the countdown follows (“Waiting · retry 28:00”)."),
    ("humid-ineffective", "retry", "jargon", "As heater-ineffective."),
    ("stripping", "drying", "jargon", "Word list."),
    ("compressor-run", "compressor on", "jargon", "Humidifier waits while the compressor runs (humidify during cooling off)."),
    ("min-off", "rest", "jargon", "[new word] “Rest” for minimum off time (word-list proposal). “Waiting · rest 3:12”."),
    ("crossover", "changeover", "jargon", "Word list."),
    ("other-load", "other load on", "jargon", "Heater and compressor never run together."),
]):
    snip = f'return "{code}";' if code != "max-run" else None
    r(f"GRD-{i + 1:02d}", GO, "Waiting · reason", G, snip, ("held: {reason}", f"held: {code}"), ("Waiting · {reason}", "Waiting · " + word if not word.startswith("(") else "Off in Settings"), why, note)
r("GRD-18", GO, "Waiting · reason", "lib/core/src/wineador/event_text.h", '"max-run"', ("held: {reason}", "held: max-run"), ("Waiting · {reason}", "Waiting · run limit"), "jargon",
  "After its longest allowed run the load rests; “Waiting · run limit 4:10”.")

# Restart reasons (src/drivers/reset_reason.h codes → words in app.js)
RR = "src/drivers/reset_reason.h"
for i, (code, word, note) in enumerate([
    ("POWERON", "Power on or reset", "Word list; the S3 can't tell them apart (3A.11)."),
    ("SOFTWARE", "Restarted by the firmware", "After an update, or a restart command. [new word]"),
    ("PANIC", "Crash", "[new word]"),
    ("INT_WATCHDOG", "Watchdog", "[new word] The three watchdog codes, one word."),
    ("TASK_WATCHDOG", "Watchdog", "[new word]"),
    ("WATCHDOG", "Watchdog", "[new word]"),
    ("DEEPSLEEP", "Woke from sleep", "Not used by this firmware. [new word]"),
    ("BROWNOUT", "Power dip", "[new word]"),
    ("SDIO", "Other", ""),
    ("EXTERNAL", "Reset", "[new word]"),
    ("OTHER", "Other", ""),
]):
    r(f"RST-{i + 1:02d}", "dashboard · Overview (Board) + Events (boot)", "Last restart", RR, f'return "{code}";', code, word, "jargon", note)

# Dashboard — Graphs
GP = "dashboard · Graphs"
r("DB-GR-01", GP, "Page", IX, '<h1 id="h-graphs">Graphs</h1>', "Graphs", "=", "fine", "")
r("DB-GR-02", GP, "Range", IX, 'data-range="hour">Hour<', "Hour · Day · Week · Month", "=", "fine", "")
r("DB-GR-03", GP, "Summary", JS, '["Compressor on", pct(d.duty.compressor)]', ("Compressor on · {n} %", "23 %"), ("Compressor on · {n}%", "23%"), "style", "Rule 8. Also Heater on, Humidifier on.")
r("DB-GR-04", GP, "Summary · week, month", JS, '["Crossovers, 7 days"', "Crossovers, 7 days · RH strips, 7 days · Humidifier runs, 7 days", "Changeovers, 7 days · Drying runs, 7 days · Humidifier runs, 7 days", "inconsistent", "As DB-OV-63/64.")
r("DB-GR-05", GP, "Empty", JS, '"Nothing logged in this range yet. After a boot', "Nothing logged in this range yet. After a boot the graphs refill from the card in the background.", "Nothing logged in this range yet.", "too long", "Rule 2.")
r("DB-GR-06", GP, "Temperature chart", IX, '<figcaption>Temperature, °C', "Temperature, °C — key: cabinet mean · coldest to warmest sensor · setpoint", "Temperature, °C — key: Mean · Sensor range · Target", "inconsistent",
  "Word list: Target. “Cabinet” implied, as on Overview.")
r("DB-GR-07", GP, "Humidity chart", IX, '<figcaption>Humidity, %', "Humidity, % — key: cabinet mean · setpoint", "Humidity, % — key: Mean · Target", "inconsistent", "")
r("DB-GR-08", GP, "Loads chart", IX, '<figcaption>What ran', "What ran — key: compressor · heater · humidifier", "What ran — key: Compressor · Heater · Humidifier", "style",
  "Sentence case. The axis also labels the rows (DB-GR-09): see there.")
r("DB-GR-09", GP, "Loads chart · axis", JS, 'values: () => ["Humid", "Heat", "Comp"]', "Humid · Heat · Comp", "Humidifier · Heater · Compressor", "jargon",
  "[layout] Abbreviations; the key says it already. Full words need the axis 58 → ~80 px on all three charts (so they still line up). Or drop the axis labels and keep the key — but then the rows are told apart by colour only (rule 9). Left to Josh.")
r("DB-GR-10", GP, "Each sensor", IX, '<legend>Each sensor</legend>', "Each sensor", "Sensors", "style", "")
r("DB-GR-11", GP, "Each sensor · chips", JS, '["top_front", "bottom_front", "mid_front", "mid_rear", "chamber", "ambient"]', ("ch{n} {code}", "ch2 mid_front"), ("{Position}", "Middle front"), "jargon",
  "[new word] Word list positions; “Room” for ambient (word-list proposal). Channel numbers only on Sensors.")

# Dashboard — Sensors
SE = "dashboard · Sensors"
r("DB-SE-01", SE, "Page", IX, '<h1 id="h-sensors">Sensors</h1>', "Sensors", "=", "fine", "")
r("DB-SE-02", SE, "Subtitle", JS, '`${healthyCab} of 4 cabinet sensors healthy · at least 3 are needed for control`', ("{n} of 4 cabinet sensors healthy · at least 3 are needed for control", "4 of 4 cabinet sensors healthy · at least 3 are needed for control"),
  ("{n} of 4 cabinet sensors healthy · 3 needed", "4 of 4 cabinet sensors healthy · 3 needed"), "too long", "")
r("DB-SE-03", SE, "Table head", IX, '<th>Channel</th><th>Where</th><th>State</th>', "Channel · Where · State", "Channel · Position · State", "style", "")
r("DB-SE-04", SE, "Table head", IX, '<th class="n">Temp</th><th class="n">RH</th><th class="n">Raw RH</th>', "Temp · RH · Raw RH", "Temperature · Humidity · Raw humidity", "jargon", "Word list: Humidity, not RH, in labels. “Temp” abbreviation.")
r("DB-SE-05", SE, "Table head", IX, '<th class="n">Read</th>', "Read", "Last read", "style", "")
r("DB-SE-06", SE, "Table head", IX, '<th class="n">CRC / timeouts this hour</th>', "CRC / timeouts this hour", "Bad reads / timeouts, this hour", "jargon", "[new word] “Bad read” for CRC failure (word-list proposal).")
r("DB-SE-06b", SE, "Phone layout", "web/app.css", 'content: "CRC / timeouts this hour: "', "CRC / timeouts this hour:", "Bad reads / timeouts, this hour:", "jargon", "Same as DB-SE-06 (the phone's cell label, in CSS).")
r("DB-SE-06c", SE, "Phone layout", "web/app.css", 'content: "raw "', "raw", "Raw", "style", "")
r("DB-SE-07", SE, "Row · channel", JS, '<td class="ch">ch${x.ch}</td>', ("ch{n}", "ch3"), ("{n}", "3"), "repeat", "The column is headed Channel.")
r("DB-SE-08", SE, "Row · position", JS, '<td>${esc(x.name)}<div class="pos">probe ${esc(x.probe)} · ${x.role}</div></td>', ("{code} / probe {label} · {role}", "mid_rear / probe A1C5 · cabinet"), ("{Position} / Probe {label}", "Middle rear / Probe A1C5"), "jargon",
  "[logic] Position from a table (word list); the role repeats the position (“Chamber / chamber”). Probe labels belong here (word list).")
r("DB-SE-09", SE, "Row · state", JS, 'x.state === "healthy" ? "Healthy" : x.state === "manual" ? "Excluded by hand" : x.state === "not_read" ? "Not read yet" : `Excluded: ${x.reason}`',
  ("Healthy · Excluded by hand · Not read yet · Excluded: {reason code}", "Excluded: bus-fault"), ("Healthy · Excluded by hand · Not read yet · Excluded: {reason}", "Excluded: sensor bus fault"), "jargon", "Reason words: EXC rows.")
r("DB-SE-10", SE, "Row · values", JS, 'fmt(x.t, 2) + " °C"', ("{t:.2f} °C · {rh:.1f} % · {raw:.1f} %", "20.53 °C · 62.0 % · 63.4 %"), ("{t:.2f}°C · {rh:.1f}% · {raw:.1f}%", "20.53°C · 62.0% · 63.4%"), "style",
  "No space before ° or %. Decimals kept: see finding F4 (rule 8 vs a diagnostic page).")
r("DB-SE-11", SE, "Row · read age", JS, '<td class="n dim">${ago(x.age_s)} ago</td>', ("{ago} ago", "7 s ago"), "=", "fine", "")
r("DB-SE-12", SE, "Row · button", JS, '${manual ? "Bring back" : "Exclude"}', "Exclude · Bring back", "=", "fine", "")
r("DB-SE-13", SE, "Exclude · confirm", JS, '`Exclude ch${ch} ${x.name} by hand? It stays excluded until you bring it back.`',
  ("Exclude ch{n} {code} by hand? It stays excluded until you bring it back.", "Exclude ch3 mid_rear by hand? It stays excluded until you bring it back."),
  ("Exclude {Position}? It stays out until you bring it back.", "Exclude Middle rear? It stays out until you bring it back."), "jargon", "")
r("DB-SE-14", SE, "Exclude · confirm, would fault", JS, 'the cabinet goes into Fault and the loads are held.',
  ("That leaves {n} of 4 cabinet sensors: the cabinet goes into Fault and the loads are held.", "That leaves 2 of 4 cabinet sensors: the cabinet goes into Fault and the loads are held."),
  ("Only {n} cabinet sensors left: this causes a Fault and stops every load.", "Only 2 cabinet sensors left: this causes a Fault and stops every load."), "style",
  "Chamber case: “No chamber sensor left: this causes a Fault and stops every load.” [logic] Clarify: a confirm() dialog's buttons are the browser's (OK / Cancel) and can't name the action.")
r("DB-SE-15", SE, "After exclude", JS, '"excluded by hand" : "brought back; it rejoins after its good reads"',
  ("ch{n} {code} (probe {p}): excluded by hand. / …: brought back; it rejoins after its good reads.", "ch3 mid_rear (probe A1C5): brought back; it rejoins after its good reads."),
  ("{Position} excluded. / {Position} back: in use after 10 good reads.", "Middle rear back: in use after 10 good reads."), "too long", "“10 good reads”: the figure the event log uses (main.cpp SENSOR RECOVERED).")
r("DB-SE-16", SE, "Exclude · no answer", JS, '$("#s-note").textContent = err.body?.message || "The board didn\'t answer; nothing changed.";', "The board didn't answer; nothing changed.", "No answer from the board. Nothing changed; try again.", "style",
  "Rule 5. Also DB-ST-13 (same text).")

# Sensor exclusion reasons (sensor_health.cpp codes → words in app.js)
SH = "lib/core/src/wineador/sensor_health.cpp"
for i, (code, word, note) in enumerate([
    ("crc", "bad reads", "[new word] as DB-SE-06."), ("timeout", "no answer", ""), ("absent", "not found", ""),
    ("implausible", "impossible reading", ""), ("divergence", "disagrees with the others", "4.3's divergence check."),
    ("bus-fault", "sensor bus fault", ""), ("wrong-serial", "wrong sensor", "A probe with another serial in that place."),
]):
    r(f"EXC-{i + 1:02d}", "dashboard · Sensors + Events", "Excluded · reason", SH, f'return "{code}";', ("Excluded: {reason}", code), ("Excluded: {reason}", word), "jargon", note)

# Dashboard — Events
EV = "dashboard · Events"
r("DB-EV-01", EV, "Page", IX, '<h1 id="h-events">Events</h1>', "Events", "=", "fine", "")
r("DB-EV-02", EV, "Filter", IX, '<label class="field-inline">Show <select id="e-type"><option value="">all types</option>', "Show · all types", "Show · All", "too long", "Also app.js:505 (the same option, rebuilt).")
r("DB-EV-03", EV, "Filter · options; row type", JS, '`<option${x === t ? " selected" : ""}>${esc(x)}</option>`', ("{type code}", "hwdog"), ("{type name}", "Watchdog"), "jargon",
  "[logic] Event type codes are internal (“i2c”, “hwdog”, “psram”, “12v”, “guard”, “strip”, “ntp”). A name per type: TYP rows. In each row the type word repeats the sentence's first word (“humidify · Humidifier called…”): see DB-EV-04.")
r("DB-EV-04", EV, "Row · type", JS, '<span class="type t-${TYPE_COLOUR[e.type] || ""}">${esc(e.type)}</span>', ("{type code} (beside the sentence)", "humidify"), None, "repeat",
  "[layout] The sentence names its subject; keep the coloured dot (DESIGN: event dot) and drop the word. The filter keeps the names.")
r("DB-EV-05", EV, "Row · time tooltip", JS, '(clock not trusted)', ("{utc}{ (clock not trusted)}", "2026-11-13T14:32:05Z (clock not trusted)"), ("{utc}{ (clock not set)}", "2026-11-13T14:32:05Z (clock not set)"), "inconsistent",
  "As ALR-11. The UTC stamp on hover is for matching the card's files: kept.")
r("DB-EV-06", EV, "Row · logged text", JS, '<small class="logged">${esc(e.detail)}</small>', ("{logged detail}", "HUMIDIFY CALL END: mean RH 62.0% reached the 62% setpoint"), "=", "fine",
  "Spec 5.5 keeps the logged text in small print. See finding F2: it repeats every sentence in engineering words.")
r("DB-EV-07", EV, "Day heading", JS, '`<li class="day">${esc(d)}</li>`', ("{d} {Mon} {yyyy}", "13 Nov 2026"), "=", "fine", "")
r("DB-EV-08", EV, "Empty", JS, '`<li class="empty">No events${t ? ` of type ${esc(t)} in what is loaded` : ""}.</li>`', ("No events{ of type {code} in what is loaded}.", "No events of type hwdog in what is loaded."),
  ("No events / No {type name} events loaded", "No Watchdog events loaded"), "jargon", "")
r("DB-EV-09", EV, "SD card out", JS, '"SD card unavailable: these are the recent events kept in the board\'s memory only."', "SD card unavailable: these are the recent events kept in the board's memory only.",
  "SD card not available: recent events only.", "too long", "")
r("DB-EV-10", EV, "Older", IX, '>Older events</button>', "Older events", "=", "fine", "")
r("DB-EV-11", EV, "Door test", IX, '<h2>Door test</h2>', "Door test", "=", "fine", "")
r("DB-EV-12", EV, "Door test", IX, '<p>Logs a "door test" event now, for the disturbance-detection tests.</p>', "Logs a \"door test\" event now, for the disturbance-detection tests.", "Marks a door opening in the events.", "too long", "")
r("DB-EV-13", EV, "Door test · note", IX, 'placeholder="Note (optional), e.g. door open 30 s"', "Note (optional), e.g. door open 30 s", "Note (optional)", "style",
  "Clarify: a placeholder isn't a label. [layout] Make “Note (optional)” a visible label (today it's screen-reader only) and leave the box empty, or keep the example as the placeholder.")
r("DB-EV-14", EV, "Door test · button", IX, '>Log door test</button>', "Log door test", "=", "fine", "")
r("DB-EV-15", EV, "Door test · done", JS, '"Door test logged."', "Door test logged.", "Door test logged", "style", "A phrase: no full stop.")
r("DB-EV-16", EV, "Door test · no answer", JS, '"The board didn\'t answer; nothing was logged."', "The board didn't answer; nothing was logged.", "No answer from the board. Nothing logged; try again.", "style", "")

ET = "lib/core/src/wineador/event_text.h"
for i, (code, name) in enumerate([
    ("Compressor", "Compressor"), ("Heater", "Heater"), ("Humidifier", "Humidifier"), ("Interlock", "Interlock"), ("Harvest", "Fan run-on"),
    ("Guard", "Waiting"), ("Crc", "Bad read"), ("Timeout", "No answer"), ("Sensor", "Sensor"), ("Fan", "Plenum fan"), ("Settings", "Settings"),
    ("Boot", "Restart"), ("Sd", "SD card"), ("I2c", "Sensor bus"), ("Rtc", "Clock"), ("Ntp", "Clock"), ("Crossover", "Changeover"),
    ("Strip", "Drying"), ("Humidify", "Humidifying"), ("Exercise", "Relay check"), ("Ineffective", "Not working"), ("Disturbance", "Disturbance"),
    ("Heap", "Memory"), ("Psram", "Memory"), ("Hwdog", "Watchdog"), ("Lcd", "Cabinet screen"), ("Supply12v", "12V supply"), ("Wifi", "WiFi"),
    ("Door", "Door test"), ("Ota", "Update"), ("Blowers", "Blowers"), ("Ion", "Ion generator"), ("Scenario", "Test"),
]):
    code_txt = {"Supply12v": "12v"}.get(code, code.lower())
    fine = name.lower() == code_txt
    r(f"TYP-{i + 1:02d}", EV, "Filter · type name", ET, f'X({code}, "{code_txt}")', code_txt, "=" if False else name, "fine" if fine else "jargon",
      "[logic] Names in app.js; the API and the card keep the codes." + (" [new word]" if name in ("Fan run-on", "Bad read", "Relay check", "Not working", "Cabinet screen") else ""))

# Dashboard — Settings
STG = "dashboard · Settings"
r("DB-ST-01", STG, "Page", IX, '<h1 id="h-settings">Settings</h1>', "Settings", "=", "fine", "")
r("DB-ST-02", STG, "Target temperature", IX, '<label>Target temperature</label>', "Target temperature", "=", "fine", "Word list.")
r("DB-ST-03", STG, "Target temperature · hint", JS, '`${fmt(d.min, dec)}–${fmt(d.max, dec)}${unit} in steps of ${fmt(d.step, dec)}`', ("{min}–{max} {unit} in steps of {step}", "15.0–22.0 °C in steps of 0.5"), ("{min}–{max}{unit}", "15–22°C"), "repeat",
  "The − and + move one step. Same as the LCD (LCD-S-04).")
r("DB-ST-04", STG, "Target humidity", IX, '<label>RH setpoint</label>', "RH setpoint", "Target humidity", "jargon", "Word list (both words).")
r("DB-ST-05", STG, "Target humidity · hint", JS, '`${fmt(d.min, dec)}–${fmt(d.max, dec)}${unit} in steps of', ("55–70 % in steps of 1", "55–70 % in steps of 1"), "55–70%", "repeat", "Unit from the API “%”; no space.")
r("DB-ST-06", STG, "Heater", IX, '<small>The heater may run when the cabinet is cold</small>', "Heater — The heater may run when the cabinet is cold", "Heater", "repeat", "The switch says it. Rule 2.")
r("DB-ST-07", STG, "Humidifier", IX, '<small>The humidifier may run when the cabinet is dry</small>', "Humidifier — The humidifier may run when the cabinet is dry", "Humidifier", "repeat", "")
r("DB-ST-08", STG, "Ion generator", IX, '<small>Off means no runs at all; its run settings are kept for when it is switched back on.</small>', "Ion generator — Off means no runs at all; its run settings are kept for when it is switched back on.", "Ion generator", "too long",
  "CLAUDE's example. The settings below stay visible, which shows they are kept.")
r("DB-ST-09", STG, "Ion runs", IX, '<label>Ion generator runs per day</label>', "Ion generator runs per day", "Runs per day", "repeat", "[layout] Indented under the Ion generator switch so it reads as its setting.")
r("DB-ST-10", STG, "Ion minutes", IX, '<label>Ion generator minutes per run</label>', "Ion generator minutes per run", "Minutes per run", "repeat", "As DB-ST-09.")
r("DB-ST-10b", STG, "Ion hints", JS, 'f.startsWith("ion_") ? `${d.min}–${d.max}${unit}`', ("{min}–{max}{ unit}", "0–5 min"), "=", "fine", "")
r("DB-ST-10c", STG, "Stepper buttons (screen reader)", IX, 'aria-label="Lower"', "Lower · Higher · Fewer · More · Shorter · Longer", "=", "fine", "", nth=1)
r("DB-ST-11", STG, "Ion total · none", JS, '"No runs: the ion generator won\'t run."', "No runs: the ion generator won't run.", "No runs", "repeat", "")
r("DB-ST-12", STG, "Ion total · too much", JS, '`${draft.ion_runs_per_day} runs of ${draft.ion_minutes_per_run} min is ${perDay} min a day: at most ${maxDay} minutes a day in total.`',
  ("{r} runs of {m} min is {t} min a day: at most {max} minutes a day in total.", "3 runs of 4 min is 12 min a day: at most 10 minutes a day in total."),
  ("{t} min a day. The limit is {max} min.", "12 min a day. The limit is 10 min."), "too long", "Rule 5. Same words as the API refusal (API-34).")
r("DB-ST-12b", STG, "Ion total", JS, '`${draft.ion_runs_per_day} ${draft.ion_runs_per_day === 1 ? "run" : "runs"} of ${draft.ion_minutes_per_run} min: ${perDay} min a day,',
  ("{r} runs of {m} min: {t} min a day, every {24/r} hours from 00:00 UTC.", "2 runs of 2 min: 4 min a day, every 12 hours from 00:00 UTC."),
  ("{t} min a day, at {UK times}", "4 min a day, at 00:00 and 12:00"), "inconsistent",
  "One run today reads “at 00:00 UTC” (app.js:573): the same fix. [logic] UK local times, as every other time (rule 8), worked out in the page with Europe/London as the graphs already do. In summer they read 01:00 and 13:00: see finding F6.")
r("DB-ST-13", STG, "Save · no answer", JS, '} catch (err) { m.className = "msg bad"; m.textContent = err.body?.message || "The board didn\'t answer; nothing changed."; }\n});\n\n// ---------------------------------------------------------------- Logs', "The board didn't answer; nothing changed.", "No answer from the board. Nothing changed; try again.", "style", "As DB-SE-16.")
r("DB-ST-14", STG, "Save · done", JS, '"Saved. The board uses the new settings from its next pass."', "Saved. The board uses the new settings from its next pass.", "Saved", "too long", "Rule 2.")
r("DB-ST-15", STG, "Save · updating", JS, '"An update is running: settings can be changed when it has finished."', "An update is running: settings can be changed when it has finished.", "Updating. Save when it has finished.", "too long", "")
r("DB-ST-16", STG, "Buttons", IX, '>Cancel</button><button type="submit" class="btn primary" id="st-save" disabled>Save</button>', "Cancel · Save", "=", "fine", "")
r("DB-ST-17", STG, "Tuning", IX, '<h2 id="h-tuning">Tuning</h2>', "Tuning", "=", "fine", "")
r("DB-ST-18", STG, "Tuning", IX, "Set once from measurements of this cabinet. Shown here for reference; they can't be changed from this page.", "Set once from measurements of this cabinet. Shown here for reference; they can't be changed from this page.",
  "Set from this cabinet's tests. Read only.", "too long", "")
r("DB-ST-19", STG, "Tuning · values", JS, '(t.value ? "On" : "Off")', ("{value}{ unit} / On · Off", "0.5 °C"), ("{value}{unit} / On · Off", "0.5°C"), "style", "[logic] No space before °C or %; “points” → “%” (DB-ST-T17 note).")

WA = "lib/core/src/wineador/web_api.cpp"
for i, (code, cur, prop, why, note) in enumerate([
    ("cooling_hysteresis_c", "Cooling hysteresis", "Cooling starts at target +", "jargon", "Values read as a sentence: “Cooling starts at target + 0.5°C”."),
    ("heating_deadband_c", "Heating deadband", "Heating starts at target −", "jargon", ""),
    ("heating_stop_below_c", "Heating stops below setpoint", "Heating stops at target −", "inconsistent", ""),
    ("crossover_delay_min", "Crossover delay", "Changeover wait", "inconsistent", "Word list."),
    ("fan_idle_pct", "Fan PWM, idle", "Fan speed, idle", "jargon", "Word list: Speed. CLAUDE's example."),
    ("fan_compressor_pct", "Fan PWM, compressor run", "Fan speed, compressor on", "jargon", ""),
    ("fan_cool_harvest_start_pct", "Fan PWM, cooling harvest start", "Fan speed, run-on after cooling, start", "jargon", "[new word] “Run-on” for harvest (word-list proposal)."),
    ("fan_cool_harvest_end_pct", "Fan PWM, cooling harvest end", "Fan speed, run-on after cooling, end", "jargon", "[new word]"),
    ("fan_heating_pct", "Fan PWM, heating and its harvest", "Fan speed, heating and run-on", "jargon", "[new word]"),
    ("harvest_convergence_c", "Harvest convergence margin", "Run-on ends within", "jargon", "[new word] “Run-on ends within 1.0°C” (chamber of the cabinet)."),
    ("harvest_backstop_min", "Harvest backstop", "Run-on at most", "jargon", "[new word]"),
    ("dosed_control", "Dosed control", "=", "fine", "Spec's name for a B6 option; Off."),
    ("rh_strip_enable", "RH stripping", "Drying", "jargon", "Word list."),
    ("rh_strip_offset_pct", "RH strip offset", "Drying starts at target +", "jargon", ""),
    ("rh_strip_persist_min", "RH strip trigger persistence", "Drying starts after", "jargon", "“Drying starts after 30 min”."),
    ("rh_strip_floor_c", "RH strip floor below setpoint", "Drying stops below target −", "jargon", "A temperature: “Drying stops below target − 2.0°C”."),
    ("humidify_band_pts", "Humidify band", "Humidifying starts at target −", "jargon", "Unit “points” → “%”; see finding F4."),
    ("humidify_persist_min", "Humidify trigger persistence", "Humidifying starts after", "jargon", ""),
    ("humidify_during_cooling", "Humidify during cooling runs", "Humidify while cooling", "too long", ""),
    ("heater_pwm_cap_pct", "Heater PWM cap", "Heater power limit", "jargon", ""),
    ("heater_ineffective_rise_c", "Heater ineffective: rise needed", "Heater check: rise needed", "jargon", "Matches “Heater not working” (ALR-07)."),
    ("heater_ineffective_window_min", "Heater ineffective: window", "Heater check: within", "jargon", ""),
    ("heater_ineffective_retry_min", "Heater ineffective: retry after", "Heater check: retry after", "jargon", ""),
    ("cooling_ineffective_fall_c", "Cooling ineffective: fall needed", "Cooling check: fall needed", "jargon", ""),
    ("cooling_ineffective_window_min", "Cooling ineffective: window", "Cooling check: within", "jargon", ""),
    ("humid_ineffective_rise_pts", "Humidifier ineffective: rise needed (at setpoint)", "Humidifier check: rise needed", "jargon", "“(at setpoint)” dropped: Josh knows; the spec says it."),
    ("humid_ineffective_window_min", "Humidifier ineffective: window", "Humidifier check: within", "jargon", ""),
    ("humid_ineffective_retry_min", "Humidifier ineffective: retry after", "Humidifier check: retry after", "jargon", ""),
    ("disturbance_detection", "Disturbance detection", "=", "fine", ""),
    ("log_steady_s", "Log rate, steady", "Log every, idle", "style", ""),
    ("log_active_s", "Log rate, compressor or heater active", "Log every, running", "too long", ""),
    ("heater_pwm_period_s", "Heater PWM period", "Heater cycle", "jargon", "Word list: no PWM."),
    ("sensor_read_s", "Sensor read rate", "Sensors read every", "style", ""),
    ("blower_idle_pct", "Blower PWM, idle (full speed until BP2 test 22) / Blower PWM, idle", "Blower speed, idle", "jargon", "Word list: Speed. Rule 3: no test numbers (the value says 100%)."),
    ("blower_boost_above_c", "Blower boost: spread above", "Blowers boost above", "style", "“Blowers boost above 1.0°C”."),
    ("blower_boost_above_rh_pts", "Blower boost: or RH spread above", "or humidity spread above", "jargon", ""),
    ("blower_idle_below_c", "Blower back to idle: spread below", "Blowers idle below", "style", ""),
    ("blower_idle_below_rh_pts", "Blower back to idle: and RH spread below", "and humidity spread below", "jargon", ""),
    ("blower_idle_hold_min", "Blower back to idle: held for", "for", "too long", "Reads on from the row above: “… for 10 min”."),
]):
    r(f"DB-ST-T{i + 1:02d}", STG, "Tuning · label", WA, f'"{code}", ', cur, prop, why, note + (" [api] Label only; the name stays." if prop != "=" else ""))

# Dashboard — Logs
LG = "dashboard · Logs"
r("DB-LG-01", LG, "Page", IX, '<h1 id="h-logs">Logs</h1>', "Logs", "=", "fine", "")
r("DB-LG-02", LG, "Card", JS, '`SD card: ${bytes(r.card.free_bytes)} free of ${bytes(r.card.total_bytes)}`', ("SD card: {free} free of {total}", "SD card: 59.2 GB free of 59.7 GB"), "=", "fine", "")
r("DB-LG-03", LG, "Table head", IX, '<th>File</th><th>Kind</th><th>Month</th><th class="n">Size</th>', "File · Kind · Month · Size", "=", "fine", "")
r("DB-LG-04", LG, "Row · kind", JS, '<td>${f.kind}</td>', ("{readings|events|hourly}", "readings"), ("{Readings|Events|Hourly}", "Readings"), "style", "")
r("DB-LG-05", LG, "Row · month", JS, '<td>${f.month ?? "every hour"}</td>', ("{yyyy-mm} / every hour", "2026-11"), ("{Mon yyyy} / All", "Nov 2026"), "style",
  "“every hour” described the kind (hourly), not the month; the hourly file holds every month.")
r("DB-LG-06", LG, "Row · old file", JS, '<span class="dim">(schema 1)</span>', "(schema 1)", "(old format)", "jargon", "Rev N cards' files. [new word]")
r("DB-LG-07", LG, "Row · file name", JS, '<tr><td>${esc(f.name)}', ("{file name}", "2026-11-readings-s2.csv"), "=", "fine", "The name of the file you download; not a path. Rule 3 doesn't apply.")
r("DB-LG-08", LG, "Row · button", JS, 'download="${esc(f.name)}">${icon("download")}Download</a>', "Download", "=", "fine", "")
r("DB-LG-09", LG, "Empty", JS, 'No log files on the card yet.</td>', "No log files on the card yet.", "No log files yet", "style", "")
r("DB-LG-10", LG, "Note", IX, "Downloads run at the card's own pace: a month of readings takes a few minutes. One download at a time.", "Downloads run at the card's own pace: a month of readings takes a few minutes. One download at a time.",
  "A month of readings takes a few minutes. One download at a time.", "too long", "Also app.js:618 (the same text, set again).")
r("DB-LG-11", LG, "Card out", JS, "The SD card isn't available just now, so there is nothing to download.",
  "The SD card isn't available just now, so there is nothing to download. The board keeps logging into its memory and writes it all to the card when the card is back; the files appear here then.",
  "SD card not available. Logs are kept in memory until it's back.", "too long", "")

# Dashboard — Update
UP = "dashboard · Update"
r("DB-UP-01", UP, "Page", IX, '<h1 id="h-update">Update</h1>', "Update", "=", "fine", "")
r("DB-UP-02", UP, "Restarting", JS, '<b>Installed: restarting</b><span>Written and checked. Every load is off; the board restarts in a moment, then the boot hold applies.</span>',
  "Installed: restarting — Written and checked. Every load is off; the board restarts in a moment, then the boot hold applies.", "Restarting", "too long", "Header says Updating; Starting follows.")
r("DB-UP-03", UP, "Updating", JS, '<b>Updating</b><span>Writing the new firmware a piece at a time. Every load is off until it has finished.</span>',
  ("Updating — Writing the new firmware a piece at a time. Every load is off until it has finished. {pct} %", "… 43 %"), ("Updating · {pct}%", "Updating · 43%"), "too long", "Rule 2.")
r("DB-UP-04", UP, "Checking", JS, '<b>Checking the new firmware</b>',
  ("Checking the new firmware — It is marked good once it has run 60 s with the settings check passed, the watchdog toggling and every sensor read once. A reset before then rolls back to the one before. {m:ss}", "… 0:45"),
  ("Checking new firmware — A restart now goes back to the old one. {m:ss}", "Checking new firmware — A restart now goes back to the old one. 0:45"), "too long",
  "CLAUDE's example. Keeps the one fact that changes what Josh does (don't restart it).")
r("DB-UP-05", UP, "Window open", JS, '"Allow update was pressed on the cabinet screen. One update can be sent."', ("Update allowed — Allow update was pressed on the cabinet screen. One update can be sent. {m:ss}", "… 4:12"),
  ("Update allowed · {m:ss}", "Update allowed · 4:12"), "repeat", "Same as LCD-W-15.")
r("DB-UP-06", UP, "Window open, compressor on", JS, '"The compressor is running: an update never cuts a cooling run short, so it waits until it has stopped."',
  "The compressor is running: an update never cuts a cooling run short, so it waits until it has stopped.", "Waiting · compressor on", "too long",
  "Rule 4. Said twice on this page today (also DB-UP-07).")
r("DB-UP-07", UP, "Window open, compressor on", JS, '"The compressor is running: an update never cuts a cooling run short. Wait for it to stop."', "The compressor is running: an update never cuts a cooling run short. Wait for it to stop.", None, "repeat", "DB-UP-06 says it.")
r("DB-UP-08", UP, "Window closed", JS, '<b>Updates are closed</b><span>Press <b>Allow update</b> in Settings on the cabinet\'s screen: it opens a 5-minute window.</span>',
  "Updates are closed — Press Allow update in Settings on the cabinet's screen: it opens a 5-minute window.", "Updates closed — Press Allow update in Settings on the cabinet screen.", "too long", "")
r("DB-UP-09", UP, "Facts · running", JS, '["Running", `${esc(upd.build.version)} · ${upd.build.board} · ${upd.build.profile}${upd.build.test ? " · test" : ""}`]',
  ("Running · {version} · {board} · {profile}{ · test}", "2.0.0 · rev-q · real"), ("Running · {version} · {Cabinet|Bench|Test} build", "2.0.0 · Cabinet build"), "jargon",
  "Word list. Bench and test together: “Bench test build” [new word].")
r("DB-UP-10", UP, "Facts · started properly", JS, '["Started properly"', ("Started properly · yes / not yet: a reset now would roll back", "yes"), None, "repeat", "The Checking box says it (DB-UP-04).")
r("DB-UP-11", UP, "Facts · last update", JS, '["Last update"', ("Last update · {result} · {detail} · {when}", "installed · 2.0.0, replacing 1.9.0: started properly · 13 Nov 14:32"),
  ("Last update · {Result} · {version} (was {old}) · {when}", "Installed · 2.0.0 (was 1.9.0) · 13 Nov 14:32"), "repeat", "“installed” and “started properly” said twice. Details: OTA-D rows.")
r("DB-UP-12", UP, "Facts · result words", JS, 'const lastText = { none: "none yet"', "none yet · installed · refused · abandoned · rolled back · failed", "None yet · Installed · Refused · Stopped · Rolled back · Failed", "style", "“abandoned” → “Stopped” (the upload stalled or was cut off).")
r("DB-UP-13", UP, "Facts · slot", JS, '["Largest image", bytes(upd.slot_bytes)]', ("Largest image · {size}", "3.3 MB"), ("Size limit · {size}", "3.3 MB"), "jargon", "")
r("DB-UP-14", UP, "Send", IX, '<h2>Send firmware</h2>', "Send firmware", "=", "fine", "")
r("DB-UP-15", UP, "Send · help", IX, '<p>Only the <b>revq</b> cabinet build is accepted', "Only the revq cabinet build is accepted: .pio.nosync/build/revq/firmware.bin. The board checks it before anything is written.", "Cabinet build only.", "jargon",
  "Rule 3: no build names or paths. The path is in STATUS and the docs.")
r("DB-UP-16", UP, "Send · file", IX, '<span id="u-file-name">Choose firmware.bin</span>', "Choose firmware.bin", "Choose file", "jargon", "Also app.js:686.")
r("DB-UP-17", UP, "Send · file chosen", JS, '`${updFile.name} · ${bytes(updFile.size)}`', ("{name} · {size}", "firmware.bin · 1.4 MB"), "=", "fine", "")
r("DB-UP-18", UP, "Send · rollback test image", JS, '`This is the ROLLBACK TEST image (${version})',
  ("This is the ROLLBACK TEST image ({v}): it will roll back by itself. Every load stays off, and about 20 s after it starts the board resets and goes back to the firmware before it. B7 test 6 only.", "…"),
  ("Rollback test image: about 20 s after it starts, the board goes back to the firmware it has now. For B7 test 6 only.", "Rollback test image: about 20 s after it starts, the board goes back to the firmware it has now. For B7 test 6 only."), "too long",
  "A warning for a bench test; the test number stays because it says when to use it.")
r("DB-UP-19", UP, "Send · checked in the page", JS, '" The board checks it again before writing anything."', ("{verdict} The board checks it again before writing anything. / {verdict} Nothing will be sent.", "A Rev Q cabinet build, version 2.0.0. The board checks it again before writing anything."),
  ("{verdict}", "Cabinet build 2.0.0."), "too long", "The disabled Send button says “nothing will be sent”.")
r("DB-UP-20", UP, "Send · button", IX, 'id="u-send" disabled>Send update</button>', "Send update", "=", "fine", "")
r("DB-UP-21", UP, "Sending", JS, '`Sending: ${Math.round((p.loaded / p.total) * 100)} %`', ("Sending: {pct} %", "Sending: 43 %"), ("Sending {pct}%", "Sending 43%"), "style", "")
r("DB-UP-22", UP, "Installed", JS, '`Installed ${r.version}. The board is restarting; this page will show it when it answers.`', ("Installed {v}. The board is restarting; this page will show it when it answers.", "Installed 2.0.0. The board is restarting; this page will show it when it answers."),
  ("Installed {v}. Restarting…", "Installed 2.0.0. Restarting…"), "too long", "")
r("DB-UP-23", UP, "Back after restart", JS, '`Back on ${i.build.version}, being checked', ("Back on {v}, being checked: it is marked good after 60 s of running. The boot hold applies, as after every restart.", "Back on 2.0.0, being checked: …"),
  ("Running {v}", "Running 2.0.0"), "repeat", "The Checking box (DB-UP-04) and the header's Starting say the rest.")
r("DB-UP-24", UP, "Rolled back (just now)", JS, "`Running ${i.build.version}: the new firmware didn't start properly and was rolled back.`", ("Running {v}: the new firmware didn't start properly and was rolled back.", "Running 1.9.0: the new firmware didn't start properly and was rolled back."),
  ("The new firmware didn't start. Back on {v}.", "The new firmware didn't start. Back on 1.9.0."), "style", "State first.")
r("DB-UP-25", UP, "Rolled back (after checking)", JS, "`${updAwaitGood} didn't start properly and was rolled back: running ${upd.build.version}.`", ("{new} didn't start properly and was rolled back: running {v}.", "2.0.0 didn't start properly and was rolled back: running 1.9.0."),
  ("{new} didn't start. Back on {v}.", "2.0.0 didn't start. Back on 1.9.0."), "too long", "")
r("DB-UP-26", UP, "Marked good", JS, '`${upd.build.version} started properly and is marked good.`', ("{v} started properly and is marked good.", "2.0.0 started properly and is marked good."), ("Update done: {v}", "Update done: 2.0.0"), "jargon", "")
r("DB-UP-27", UP, "Refused, no message", JS, '`Refused (HTTP ${x.status}). Nothing was written.`', ("Refused (HTTP {code}). Nothing was written.", "Refused (HTTP 500). Nothing was written."), "Refused. Nothing changed.", "jargon", "Rule 3: no codes.")
r("DB-UP-28", UP, "Upload cut", JS, '"The upload stopped. Nothing was installed; the board discards a half-sent image."', "The upload stopped. Nothing was installed; the board discards a half-sent image.", "Upload stopped. Nothing changed; try again.", "too long", "")

# OTA last-update details (firmware, shown on Update's Last update row)
OU = "lib/core/src/wineador/ota_update.cpp"
for i, (snip, cur, prop, note) in enumerate([
    ('"%s: writing"', ("{v}: writing", "2.0.0: writing"), ("{v}", "2.0.0"), "The result word in front says the rest."),
    ('"%s, replacing %s: restarting"', ("{v}, replacing {old}: restarting", "2.0.0, replacing 1.9.0: restarting"), ("{v} (was {old})", "2.0.0 (was 1.9.0)"), ""),
    ('"%s: %s; nothing changed"', ("{v}: {outcome}; nothing changed", "2.0.0: no data for 60 s; nothing changed"), ("{v}: {outcome}", "2.0.0: upload stalled"), "“Nothing changed” is implied by Stopped / Failed."),
    ('"%s, replacing %s: started properly"', ("{v}, replacing {old}: started properly", "2.0.0, replacing 1.9.0: started properly"), ("{v} (was {old})", "2.0.0 (was 1.9.0)"), ""),
    ('"%s: waiting to start properly"', ("{v}: waiting to start properly", "2.0.0: waiting to start properly"), ("{v}: being checked", "2.0.0: being checked"), ""),
    ('"%s: interrupted during the write (a reset or power cut); still %s"', ("{v}: interrupted during the write (a reset or power cut); still {cur}", "2.0.0: interrupted during the write (a reset or power cut); still 1.9.0"),
     ("{v}: cut off by a restart; still {cur}", "2.0.0: cut off by a restart; still 1.9.0"), ""),
    ('"from %s to %s: %s"', ("from {v} to {cur}: {it didn't start properly | not started properly within 4 min 30 s}", "from 2.0.0 to 1.9.0: it didn't start properly"),
     ("{v} didn't start; back on {cur}", "2.0.0 didn't start; back on 1.9.0"), "Also the rolled-back event's tail (EVT-OTA-09)."),
]):
    r(f"OTA-D-{i + 1:02d}", UP, "Last update · detail", OU, snip, cur, prop, "too long", "[api] Text in GET /api/update last.detail; no field change. " + note)
for i, (code, cur, prop) in enumerate([("Stalled", "no data for 60 s", "upload stalled"), ("CutOff", "the upload stopped part way", "upload cut off"),
                                       ("Corrupt", "the image's own check failed (corrupt)", "file damaged"), ("FlashFailed", "a flash write failed", "couldn't write it"),
                                       ("Cancelled", "the writing stopped moving for 70 s", "writing stalled")]):
    r(f"OTA-O-{i + 1:02d}", UP + " + Events", "Update outcome", OU, f'case OtaOutcome::{code}:', cur, prop, "jargon", "Inside the Last update detail and API-33.", nth=1)

# Image checks: web/image_check.js and lib/core image_identity.cpp say the same (test_image_check proves the codes match)
II = "lib/core/src/wineador/image_identity.cpp"
for i, (code, js_snip, cpp_snip, cur, prop, note) in enumerate([
    ("too_short", '"too_short", "', 'ImageVerdict::TooShort:', "The file is too short to be a firmware image.", "Not a firmware file.", ""),
    ("not_firmware", '"not_firmware", "', 'ImageVerdict::NotFirmware:', "That file isn't a firmware image (use firmware.bin from the revq build).", "Not a firmware file. Choose firmware.bin.", "Rule 3: no build names."),
    ("rev_n_image", '"rev_n_image", "', 'ImageVerdict::RevNImage:', "That is a Rev N image (ESP32): it can't run on this board.", "Built for another board.", "Rule 3. The code still says rev_n for tools."),
    ("wrong_chip", '"wrong_chip", "', 'ImageVerdict::WrongChip:', "That image is for another kind of chip.", "Built for another board.", ""),
    ("no_app_description", '"no_app_description", "', 'ImageVerdict::NoAppDesc:', "That image isn't firmware built for this project.", "Not openHumidor firmware.", ""),
    ("no_identity", '"no_identity", "', 'ImageVerdict::NoIdentity:', "That image carries no openHumidor identity: a BP2 test sketch, or firmware from before stage Q5.", "Not a cabinet build: a test sketch or old firmware.", "Rule 3: no stage names."),
    ("unknown_identity_layout", '"unknown_identity_layout", "', 'ImageVerdict::UnknownLayout:', "That image's identity is in a newer layout this page can't read. (firmware: “…this firmware can't read.”)", "Too new for this page. Reload it, then try again. (firmware: “Too new for this firmware.”)", "The two copies differ on purpose (page / firmware)."),
    ("wrong_board", '"wrong_board", "', 'ImageVerdict::WrongBoard:', "That image is built for another board.", "Built for another board.", ""),
    ("bench_build", '"bench_build", "', 'ImageVerdict::BenchBuild:', "That is a bench build (short timers): never for the cabinet.", "Bench build: not for the cabinet.", ""),
    ("test_build", '"test_build", "', 'ImageVerdict::TestBuild:', "That is a test build (scripted readings): never for the cabinet.", "Test build: not for the cabinet.", ""),
]):
    r(f"IMG-{i + 1:02d}a", UP, f"Send · check ({code})", IC, js_snip, cur, prop, "too long" if len(cur) > 45 else "jargon", note + " Page copy; IMG-%02db is the firmware's." % (i + 1))
    r(f"IMG-{i + 1:02d}b", UP, f"Send · refused ({code})", II, cpp_snip, cur, prop, "too long" if len(cur) > 45 else "jargon", "Firmware copy (wrong_image message, API-22).", nth=2)
r("IMG-11a", UP, "Send · check (accept)", IC, 'say("accept", `A Rev Q cabinet build, version ${version}.`)', ("A Rev Q cabinet build, version {v}.", "A Rev Q cabinet build, version 2.0.0."), ("Cabinet build {v}.", "Cabinet build 2.0.0."), "jargon", "Word list.")
r("IMG-11b", UP, "Send · accept (firmware)", II, '"A Rev Q cabinet build."', "A Rev Q cabinet build.", "Cabinet build.", "jargon", "")

# =====================================================================================================
# API messages (lib/core web_api.cpp etc.): the page shows `message` when a write is refused.
for i, (code, cur, prop, why, note) in enumerate([
    ("not_found", "There is no such page or file.", "Not found.", "too long", "Tools only, or a stale link."),
    ("method_not_allowed", "That request isn't allowed here.", "=", "fine", "Tools only."),
    ("not_json", "Changes must be sent as JSON.", "=", "fine", "Tools only."),
    ("bad_json", "The request isn't one flat JSON object.", "=", "fine", "Tools only."),
    ("empty", "Nothing to change was given.", "Nothing to change.", "style", ""),
    ("unknown_field", "That isn't something this request can change.", "That can't be changed here.", "style", ""),
    ("read_only", "Tuning settings are shown here but can only be changed on the B6 tuning dashboard.", "Tuning settings are read only.", "jargon", "Rule 3."),
    ("wrong_type", "That value is the wrong kind (a number, on/off or text).", "=", "fine", "Tools only."),
    ("out_of_bounds", "That value is outside the setting's limits.", "Out of range.", "style", "Generic; the settings give their own (API-35…)."),
    ("off_step", "That value isn't one of the setting's steps.", "=", "fine", "Generic."),
    ("unknown_channel", "There is no such sensor.", "No such sensor.", "style", ""),
    ("too_long", "That text is too long.", "=", "fine", ""),
    ("bad_query", "A value in the address is missing or wrong.", "=", "fine", "Tools only."),
    ("bad_path", "That isn't a log file on the card.", "Not a log file.", "style", ""),
    ("updating", "An update is running: changes wait until it has finished.", "Updating. Try again when it has finished.", "style", "Rule 5."),
    ("compressor_running", "The compressor is running: an update never cuts a cooling run short. Try again when it has stopped.", "Compressor on. Try again when it stops.", "too long", "Rule 5. A third copy of DB-UP-06."),
    ("already_updating", "An update is already running.", "Already updating.", "style", ""),
    ("window_closed", "Updates are only accepted after Allow update is pressed on the cabinet screen.", "Updates closed. Press Allow update on the cabinet screen.", "style", "Rule 5."),
    ("busy", "Another download or update is running. Try again when it has finished.", "Board busy. Try again when the download or update has finished.", "style", ""),
    ("low_memory", "The board is short of memory just now. Try again in a minute.", "Board short of memory. Try again in a minute.", "style", ""),
    ("too_big", "That file is too big to be a firmware image for this board.", "File too big for this board.", "style", ""),
    ("wrong_image", "That image isn't a Rev Q cabinet build.", "Not a cabinet build.", "jargon", "Default; the image check's own text replaces it (IMG rows)."),
    ("stalled", "The upload stopped: nothing arrived for 60 seconds. Nothing was installed.", "Upload stalled. Nothing changed; try again.", "too long", ""),
    ("card_unavailable", "The SD card isn't available just now.", "SD card not available.", "style", ""),
    ("no_answer", "The controller didn't answer in time. Try again.", "No answer from the board. Try again.", "inconsistent", "“Board”, the word the pages use."),
    ("being_checked", "The last update is still being checked. Try again shortly.", "Last update still being checked. Try again in {n} s.", "style", "The firmware's own (API-31) says the seconds; this default is never sent."),
    ("corrupt_image", "The image arrived but failed its own check (checksum or SHA-256): it is corrupt. Nothing changed.", "File damaged. Nothing changed; send it again.", "jargon", ""),
    ("flash_failed", "Writing the image to flash failed. Nothing changed; the board carries on as it was.", "Couldn't write the update. Nothing changed.", "jargon", ""),
    ("too_soon", "Changes from the dashboard are saved at most once every 10 s. Try again shortly.", "Too soon. Try again shortly.", "too long", "Default; the real one is API-32."),
    ("ion_too_much", "The ion generator may run at most 10 minutes a day in total (runs per day times minutes per run).", "The ion generator can run at most 10 min a day.", "too long", "Default; the real one is API-34."),
]):
    r(f"API-{i + 1:02d}", "dashboard (API message)", f"Refused · {code}", WA, f'"{code}",', cur, prop, why, note + " Code unchanged.")
WM = "lib/core/src/wineador/web_mail.cpp"
r("API-31", "dashboard (API message)", "Refused · being_checked", OU, '"The last update is still being checked: try again in %lu s."', ("The last update is still being checked: try again in {n} s.", "The last update is still being checked: try again in 40 s."),
  ("Last update still being checked. Try again in {n} s.", "Last update still being checked. Try again in 40 s."), "style", "Rule 5's own form.")
r("API-32", "dashboard (API message)", "Refused · too_soon", WM, '"Changes from the dashboard are saved at most once every 10 s. Try again in %lu s."', ("Changes from the dashboard are saved at most once every 10 s. Try again in {n} s.", "Changes from the dashboard are saved at most once every 10 s. Try again in 7 s."),
  ("Too soon. Try again in {n} s.", "Too soon. Try again in 7 s."), "too long", "Spec rule 5's own example.")
r("API-33", "dashboard (API message)", "Refused · stalled (abandoned)", OU, '"The update was abandoned: %s. The half-written image is discarded; nothing changed."', ("The update was abandoned: {outcome}. The half-written image is discarded; nothing changed.", "The update was abandoned: no data for 60 s. The half-written image is discarded; nothing changed."),
  ("Update stopped: {outcome}. Nothing changed; try again.", "Update stopped: upload stalled. Nothing changed; try again."), "too long", "Outcome words: OTA-O rows.")
r("API-34", "dashboard (API message)", "Refused · ion_too_much", WA, '"Ion generator: %u runs of %u min is %u min a day; %s."', ("Ion generator: {r} runs of {m} min is {t} min a day; at most 10 minutes a day in total.", "Ion generator: 3 runs of 4 min is 12 min a day; at most 10 minutes a day in total."),
  ("{t} min a day. The limit is 10 min.", "12 min a day. The limit is 10 min."), "too long", "Same as the page's own (DB-ST-12). kIonTooMuchText is also the spec's quoted words (4.6c “at most 10 minutes a day in total”).")
r("API-35", "dashboard (API message)", "Refused · out_of_bounds (temperature)", WA, '"Target temperature must be %.0f to %.0f %sC."', ("Target temperature must be {lo} to {hi} °C.", "Target temperature must be 15 to 22 °C."), ("Target temperature: {lo}–{hi}°C.", "Target temperature: 15–22°C."), "style", "Page can't send it (the stepper is clamped).")
r("API-36", "dashboard (API message)", "Refused · out_of_bounds (humidity)", WA, '"RH setpoint must be %.0f to %.0f%%."', ("RH setpoint must be {lo} to {hi}%.", "RH setpoint must be 55 to 70%."), ("Target humidity: {lo}–{hi}%.", "Target humidity: 55–70%."), "jargon", "Word list.")
r("API-37", "dashboard (API message)", "Refused · off_step (temperature)", WA, '"Target temperature goes in steps of %.1f %sC."', ("Target temperature goes in steps of {s} °C.", "Target temperature goes in steps of 0.5 °C."), ("Target temperature goes in steps of {s}°C.", "Target temperature goes in steps of 0.5°C."), "style",
  "docs/api.md quotes it; test_web_api:341 checks the whole reply.")
r("API-38", "dashboard (API message)", "Refused · off_step (humidity)", WA, '"RH setpoint goes in steps of %.0f point."', ("RH setpoint goes in steps of {s} point.", "RH setpoint goes in steps of 1 point."), "Target humidity is a whole number.", "jargon", "")
r("API-39", "dashboard (API message)", "Refused · ion bounds", WA, '"Ion generator runs per day must be 0 to %.0f."', ("Ion generator runs per day / minutes per run must be 0 to {max}.", "Ion generator runs per day must be 0 to 4."), ("Runs per day: 0–{max}. / Minutes per run: 0–{max}.", "Runs per day: 0–4."), "style", "")
r("API-40", "dashboard (API message)", "Refused · ion step", WA, '"Ion generator runs per day is a whole number."', "Ion generator runs per day / minutes per run is a whole number.", "=", "fine", "")
r("API-41", "dashboard (API message)", "Refused · wrong type", WA, '"That setting takes a number."', "That setting takes a number. · That setting takes true or false.", "=", "fine", "Tools only.", nth=1)
r("API-42", "dashboard (API message)", "Refused · door note", WA, '"The note can be at most 120 characters."', "The note can be at most 120 characters.", "=", "fine", "The page's box stops at 120.")
r("API-43", "dashboard (API message)", "Refused · door note", WA, '"The note is one line of plain text."', "The note is one line of plain text.", "=", "fine", "")
r("API-44", "dashboard (API message)", "Refused · tools", WA, '"Give both channel and excluded."', "Give both channel and excluded. · channel is a number 0-5… · excluded takes true or false. · note is text. · The request body is empty. · The request isn't one flat JSON object: {why}.", "=", "fine",
  "Tools only (the page never sends these): field names are right there.")

# =====================================================================================================
# Event sentences (lib/core event_sentence.cpp, from the logged detail; the detail itself never changes).
ES = "lib/core/src/wineador/event_sentence.cpp"
E = "dashboard · Events"
def ev(id, snip, cur, prop, why, note="", path=ES, nth=None):
    r(id, E, "Event sentence", path, snip, cur, prop, why, note, nth=nth)

ev("EVT-LD-01", 'if (starts(d, onText)) return snprintf(out, n, "%s started%s%s"', ("{Load} started: {logged reason}", "Compressor started: temperature call, control 20.62 °C"),
   ("Compressor started: cooling, {t}°C / drying, {rh}% · Heater started: {t}°C · Humidifier started", "Compressor started: cooling, 20.6°C"), "jargon",
   "[logic] “temperature call”, “humidify call”, “RH strip, mean RH” → mode words; one decimal. Humidifier and drying starts carry no numbers (the Humidifying / Drying event beside them does). “scenario asks directly” / “demanded directly (test)” → “: test”. Test: test_event_text:205.")
ev("EVT-LD-02", 'if (starts(d, offText)) return snprintf(out, n, "%s stopped%s%s"', ("{Load} stopped: {demand ended | guard code}", "Humidifier stopped: compressor-run"),
   ("{Load} stopped{: guard words}", "Humidifier stopped: compressor on"), "jargon", "“demand ended” → no reason. Guard words: GRD rows. Test: test_event_text:207 (“max-run” → “run limit”).")
ev("EVT-IL-01", '"Interlock moved to the heating side%s"', ("Interlock moved to the heating side{ (relay exercise)}", "Interlock moved to the heating side (relay exercise)"), ("Interlock to the heating side{ (relay check)}", "Interlock to the heating side (relay check)"), "jargon", "Also the cooling side. Test: test_event_text:208.")
ev("EVT-HV-01", '"Harvest after %s started"', ("Harvest after {kind} started: chamber {x} °C from the cabinet mean; ends within {y} °C", "Harvest after cooling started: chamber 6.01 °C from the cabinet mean; ends within 1.0 °C"),
   ("Fan run-on after {kind} started", "Fan run-on after cooling started"), "jargon", "[new word] Run-on. The figures stay in the small print.")
ev("EVT-HV-02", '"Harvest after %s ended"', ("Harvest after {kind} ended: {chamber converged | BACKSTOP (no convergence) | a load started} after {s} s", "Harvest after cooling ended: chamber converged after 55.0 s"),
   ("Fan run-on ended after {s} s{: time limit | : a load started}", "Fan run-on ended after 55 s"), "jargon", "[new word]")
ev("EVT-GD-01", 'return snprintf(o, n, "%s held back:%s", b, x + 9);', ("{Load} held back: {guard code}", "Humidifier held back: compressor-run"), ("{Load} waiting: {guard words}", "Humidifier waiting: compressor on"), "jargon", "Word list: Waiting.")
ev("EVT-GD-02", '"%s no longer held back%s%s"', ("{Load} no longer held back: {why}; was {code}", "Humidifier no longer held back: no longer asked for; was compressor-run"), ("{Load} no longer waiting", "Humidifier no longer waiting"), "too long", "")
ev("EVT-SN-01", '"Sensor read failed its checksum"', ("Sensor read failed its checksum: ch{n} {code}: {crc} ({k} since boot)", "Sensor read failed its checksum: ch3 mid_rear: crc (12 since boot)"), ("Bad read: {Position}", "Bad read: Middle rear"), "jargon",
   "[new word] Also the “N more this hour” line: “Middle rear: 5 more bad reads this hour”.")
ev("EVT-SN-02", '"Sensor read timed out"', ("Sensor read timed out: ch{n} {code}: timeout ({k} since boot)", "Sensor read timed out: ch3 mid_rear: timeout (4 since boot)"), ("No answer: {Position}", "No answer: Middle rear"), "jargon", "")
ev("EVT-SN-03", '"Sensor ch%d (%s) left out: %s"', ("Sensor ch{n} ({code}) left out: {why}", "Sensor ch3 (mid_rear) left out: absent"), ("{Position} excluded: {reason}", "Middle rear excluded: not found"), "inconsistent",
   "“Left out” vs “Excluded” everywhere else. Reason words: EXC rows. Test: test_event_text:222.")
ev("EVT-SN-04", '"Sensor ch%d (%s) back in use"', ("Sensor ch{n} ({code}) back in use", "Sensor ch3 (mid_rear) back in use"), ("{Position} back in use", "Middle rear back in use"), "jargon", "")
ev("EVT-FN-01", 'int fan(char* o, size_t n, const char* d) { return labelled(o, n, "Plenum fan", d); }', ("Plenum fan: {logged text}", "Plenum fan: FAN STALL: tach reads zero with PWM asserted. Compressor and heater held off."),
   ("Plenum fan stalled: compressor and heater off / Plenum fan restart try {n} / Plenum fan running again", "Plenum fan stalled: compressor and heater off"), "jargon", "[logic] Passes the logged text through today; a sentence per detail (main.cpp FAN STALL / kickstart / FAN RECOVERED).")
ev("EVT-ST-01", '"Settings changed%s%s"', ("Settings changed from {where}: {code} {a} → {b}", "Settings changed from dashboard: target_temp_c 20.0 → 20.5"),
   ("{Setting} {a} → {b} ({where})", "Target temperature 20.0 → 20.5°C (dashboard)"), "jargon",
   "[logic] Codes → labels: Target temperature, Target humidity, Heater, Humidifier, Ion generator, Ion runs per day, Ion minutes per run, “{Position} excluded by hand”. Where: dashboard, cabinet screen, serial console. The same example in docs/api.md. Test: test_event_text:219.")
ev("EVT-ST-02", 'return labelled(o, n, "Settings", d);', ("Settings: {logged text}", "Settings: LCD: settings refused (20.5 C, 62%)"), ("Settings from the cabinet screen not saved / Targets rounded to their steps", "Settings from the cabinet screen not saved"), "jargon", "[logic] The two other settings details (main.cpp:823, :1395).")
ev("EVT-BT-01", '"Board started (%s)%s%s"', ("Board started ({code}), firmware {v}", "Board started (POWERON), firmware 2.0.0"), ("Started: {restart words}, firmware {v}", "Started: power on or reset, firmware 2.0.0"), "jargon", "RST rows. Test: test_event_text:226.")
ev("EVT-SD-01", '"SD card failed: the logs are kept in memory until it is back"', "SD card failed: the logs are kept in memory until it is back", "SD card failed: logs kept in memory until it's back", "style", "Test: test_event_text:224.")
ev("EVT-SD-02", '"SD card back: what was kept is being written to it"', "SD card back: what was kept is being written to it", "SD card back: writing the kept logs", "style", "")
ev("EVT-SD-03", '"Old readings deleted to free card space"', ("Old readings deleted to free card space: {logged}", "Old readings deleted to free card space: 2025-01"), ("Card nearly full: oldest readings deleted ({months})", "Card nearly full: oldest readings deleted (Jan 2025)"), "style", "")
ev("EVT-SD-04", '"A card write failed once and worked on the retry"', "A card write failed once and worked on the retry", "=", "fine", "")
ev("EVT-SD-05", '"SD card switched off and on again to recover it"', "SD card switched off and on again to recover it", "=", "fine", "")
ev("EVT-SD-06", '"A card write attempt failed"', "A card write attempt failed", "Card write failed", "style", "")
ev("EVT-SD-07", '"SD stress test"', ("SD stress test: {logged}", "SD stress test: 1000 kHz, retry on: …"), "=", "fine", "Test builds only.")
ev("EVT-I2-01", '"Sensor bus fault"', ("Sensor bus fault: I2C BUS STUCK with ch{n} selected: …", "Sensor bus fault: I2C BUS STUCK with ch2 selected: ch2 excluded, mux reset pulsed, I2C restarted (1 since boot)."), ("Sensor bus stuck: {Position} excluded", "Sensor bus stuck: Middle front excluded"), "jargon", "")
ev("EVT-RT-01", '"The clock lost its time while unpowered; internet time will correct it"', "The clock lost its time while unpowered; internet time will correct it", "Clock lost its time. Internet time will set it.", "style", "")
ev("EVT-RT-02", 'return labelled(o, n, "Clock", d);', ("Clock: {logged}", "Clock: DS3231 EOSC was SET at boot (…): cleared"), ("Clock battery setting fixed / Clock check skipped at start", "Clock battery setting fixed"), "jargon", "[logic] main.cpp:1248, :1251; the TEST line stays as it is.")
ev("EVT-NT-01", '"Clock corrected from internet time (it was %+lld s out)"', ("Clock corrected from internet time (it was {±n} s out)", "Clock corrected from internet time (it was +5 s out)"), ("Clock corrected by {n} s", "Clock corrected by 5 s"), "too long", "")
ev("EVT-NT-02", '"Clock checked against internet time"', "Clock checked against internet time", "=", "fine", "")
ev("EVT-NT-03", 'return labelled(o, n, "Internet time", d);', ("Internet time: {logged}", "Internet time: NTP: DS3231 within 2 s (+1 s); not written"), ("Clock right to 2 s / Clock not corrected: write failed", "Clock right to 2 s"), "jargon", "[logic] Rev N's “WiFi on for the NTP sync” lines (old cards): left as they are.")
ev("EVT-CO-01", '"Crossover: cooling started after heating"', "Crossover: cooling started after heating", "Changeover: heating to cooling", "inconsistent", "Word list. Test: test_event_text:203.")
ev("EVT-CO-02", '"Crossover: heating started after cooling"', "Crossover: heating started after cooling", "Changeover: cooling to heating", "inconsistent", "")
ev("EVT-DR-01", '"RH strip started: RH %.1f%%, above %.1f%%"', ("RH strip started: RH {rh:.1f}%, above {x:.1f}%", "RH strip started: RH 68.1%, above 67.0%"), ("Drying started: humidity {rh}%, above {x}%", "Drying started: humidity 68%, above 67%"), "jargon", "Word list; rule 8 whole %. Test: test_event_text:199.")
ev("EVT-DR-02", '"RH strip ended%s%s"', ("RH strip ended: {why}", "RH strip ended: vapour pressure reached target"), ("Drying ended: {back to target | too cold | heating needed | too few sensors | switched off | test}", "Drying ended: back to target"), "jargon",
   "Reasons from temp_control.cpp stripStopName.")
ev("EVT-HU-01", '"Humidifier called: RH %.1f%%, below %.1f%%"', ("Humidifier called: RH {rh:.1f}%, below {x:.1f}%", "Humidifier called: RH 59.4%, below 60.0%"), ("Humidifying started: humidity {rh}%, below {x}%", "Humidifying started: humidity 59%, below 60%"), "inconsistent",
   "Mode word. Note: 59.6% rounds to “60%, below 60%”: keep one decimal on this one, or say “at 60%”? See finding F4. Test: test_event_text:190; spec 5.5 quotes the old sentence as its example.")
ev("EVT-HU-02", '"Humidifier call ended: RH %.1f%% reached the setpoint"', ("Humidifier call ended: RH {rh:.1f}% reached the setpoint", "Humidifier call ended: RH 62.0% reached the setpoint"), ("Humidifying ended: humidity {rh}%, at target", "Humidifying ended: humidity 62%, at target"), "inconsistent", "Test: test_event_text:194.")
ev("EVT-EX-01", '"Relay exercise"', ("Relay exercise: {logged}", "Relay exercise: RELAY EXERCISE: started. Compressor and heater may not start; cycling once both are off."), ("Relay check started / cycle {n} of {k} / done", "Relay check started"), "jargon", "[new word]")
ev("EVT-IN-01", 'return labelled(o, n, head, c + 2);', ("{Heater|Cooling|Humidifier} ineffective: {logged}", "Heater ineffective: chamber changed +0.1 C in 15 min"), ("{Heater|Cooling|Humidifier} not working: {figures}", "Heater not working: chamber rose 0.1°C in 15 min"), "jargon", "[new word] as ALR-07.")
ev("EVT-DI-01", '"Disturbance detected"', ("Disturbance detected: {logged}", "Disturbance detected: …"), "=", "fine", "Detection is off until B6; its wording waits for that.")
ev("EVT-HP-01", '"Free memory went below its 30 KB floor"', "Free memory went below its 30 KB floor", "Memory low: below 30 KB", "style", "")
ev("EVT-PS-01", '"Extra memory (PSRAM)"', ("Extra memory (PSRAM): {logged}", "Extra memory (PSRAM): NO PSRAM FOUND at boot: …"), "Extra memory not found: screens off, smaller log buffers", "jargon", "")
ev("EVT-HW-01", '"Hardware watchdog"', ("Hardware watchdog: {logged}", "Hardware watchdog: HARDWARE WATCHDOG: 950 ms between toggles (the limit is 900 ms; U5 resets at 1.12 s minimum)"), ("Watchdog nearly missed: {ms} ms", "Watchdog nearly missed: 950 ms"), "jargon", "The TEST pause/resume lines stay.")
ev("EVT-LC-01", '"Cabinet screen"', ("Cabinet screen: {logged}", "Cabinet screen: LCD: Allow update pressed - an update may be sent in the next 5 minutes (5.5)"),
   ("Allow update pressed / Cabinet screen slow: {ms} ms / Touch screen setup failed / Cabinet screen off: no extra memory", "Allow update pressed"), "jargon", "[logic] One sentence per detail (main.cpp:827-834, :1400).")
ev("EVT-12-01", '"12%sV supply dipped below %.1f%sV"', ("12 V supply dipped below {v} V", "12 V supply dipped below 11.0 V"), ("12V supply dipped below {v}V", "12V supply dipped below 11.0V"), "inconsistent", "As DB-OV-73.")
ev("EVT-12-02", '"12%sV supply back after %.1f%ss; lowest %.2f%sV"', ("12 V supply back after {s} s; lowest {v} V", "12 V supply back after 5.3 s; lowest 10.52 V"), ("12V supply back after {s} s, lowest {v}V", "12V supply back after 5.3 s, lowest 10.52V"), "inconsistent", "")
ev("EVT-12-03", 'return labelled(o, n, "12 V supply", d);', ("12 V supply: {logged}", "12 V supply: 5 more 12V dips in the last hour (only the first 10 logged)"), ("{n} more 12V dips this hour", "5 more 12V dips this hour"), "too long", "")
ev("EVT-WF-01", 'return snprintf(o, n, "WiFi lost");', "WiFi lost", "=", "fine", "")
ev("EVT-WF-02", '"WiFi back after %lu%ss"', ("WiFi back after {s} s", "WiFi back after 61 s"), "=", "fine", "Test: test_event_text:212.")
ev("EVT-WF-03", 'return labelled(o, n, "WiFi", d);', ("WiFi: {logged}", "WiFi: 5 more WiFi outages in the last hour (only the first 10 logged)"), ("{n} more WiFi drops this hour", "5 more WiFi drops this hour"), "too long", "")
ev("EVT-DT-01", '"Door test: %s"', ("Door test{: note}", "Door test: door open 30 s"), "=", "fine", "Test: test_event_text:228.")
for i, (snip, cur, prop, why, note) in enumerate([
    ('{kEvOtaStarted, "Update started"}', ("Update started: {v} ({bytes} bytes), replacing {old}; loads off", "Update started: 2.0.0 (1413978 bytes), replacing 1.9.0; loads off"), ("Update started: {v} (was {old})", "Update started: 2.0.0 (was 1.9.0)"), "too long", ""),
    ('{kEvOtaInstalled, "Update installed"}', ("Update installed: {v}, checked; restarting", "Update installed: 2.0.0, checked; restarting"), ("Update installed: {v}. Restarting", "Update installed: 2.0.0. Restarting"), "style", ""),
    ('{kEvOtaAbandoned, "Update abandoned"}', ("Update abandoned: {v}, {outcome}; half-written image discarded, normal control", "Update abandoned: 2.0.0, no data for 60 s; half-written image discarded, normal control"), ("Update stopped: {outcome}. Nothing changed", "Update stopped: upload stalled. Nothing changed"), "too long", ""),
    ('{kEvOtaFailed, "Update failed"}', ("Update failed: {v}, {outcome}; nothing changed, normal control", "Update failed: 2.0.0, the image's own check failed (corrupt); nothing changed, normal control"), ("Update failed: {outcome}. Nothing changed", "Update failed: file damaged. Nothing changed"), "too long", ""),
    ('{kEvOtaInterrupted, "Update interrupted"}', ("Update interrupted: {v} (a reset or power cut during the write); still {cur}, nothing changed", "…"), ("Update cut off by a restart: still {cur}", "Update cut off by a restart: still 1.9.0"), "too long", ""),
    ('{kEvOtaRefused, "Update refused"}', ("Update refused: {code}{ image code} ({…})", "Update refused: compressor_running (…)"), ("Update refused: {message words}", "Update refused: compressor on"), "jargon", "[logic] Code → the API message's words."),
    ('{kEvOtaWaiting, "New firmware running, being checked"}', ("New firmware running, being checked: {v}, waiting to start properly (5.2: …)", "…"), ("Checking new firmware {v}", "Checking new firmware 2.0.0"), "jargon", "Rule 3: no spec numbers."),
    ('{kEvOtaGood, "New firmware started properly"}', ("New firmware started properly: {v} marked good (60 s of the loop, …)", "…"), ("Update done: {v}", "Update done: 2.0.0"), "too long", "Matches DB-UP-26."),
    ('{kEvOtaRolledBack, "Update rolled back"}', ("Update rolled back: from {v} to {cur}: …", "Update rolled back: from 2.0.0 to 1.9.0: it didn't start properly"), ("Update rolled back: {v} didn't start; back on {cur}", "Update rolled back: 2.0.0 didn't start; back on 1.9.0"), "style", ""),
    ('{kEvOtaSelfRollback, "New firmware didn\'t start properly in 4 min 30 s"}', ("New firmware didn't start properly in 4 min 30 s: {v} rolls itself back…", "…"), ("New firmware didn't start: going back", "New firmware didn't start: going back"), "too long", ""),
    ('{kEvOtaRollbackTest, "Rollback test image"}', ("Rollback test image: {logged}", "…"), "=", "fine", "B7 test 6 only."),
    ('return labelled(o, n, "Updates refused", d);', ("Updates refused: {n} more OTA uploads refused in the last hour (only the first 10 logged)", "…"), ("{n} more refused updates this hour", "5 more refused updates this hour"), "too long", ""),
]):
    ev(f"EVT-OTA-{i + 1:02d}", snip, cur, prop, why, note)
for i, (snip, cur, prop, why, note) in enumerate([
    ('{kEvBlowersBoost, "Blowers to boost (full speed)"}', ("Blowers to boost (full speed): spread {t} °C, {rh} points (above {x} °C or {y} points) / : after boot; idle after 10 min of low spread", "Blowers to boost (full speed): spread 1.20 °C, 2.0 points (above 1.0 °C or 3 points)"),
     ("Blowers to boost: spread {t}°C, {rh}% / Blowers to boost after {start|the update|full speed}", "Blowers to boost: spread 1.2°C, 2%"), "too long", "Test: test_blower_rule:311."),
    ('{kEvBlowersIdle, "Blowers back to idle"}', ("Blowers back to idle: spread below {x} °C and {y} points for {n} min; {pct}%", "Blowers back to idle: spread below 0.5 °C and 2 points for 10 min; 100%"), "Blowers back to idle", "too long", "Thresholds are tuning rows. Test: test_blower_rule:317."),
    ('{kEvBlowersFull, "Blowers at full speed"}', ("Blowers at full speed: {fault | no fresh reading | no reading yet since boot}", "Blowers at full speed: fault"), ("Blowers at full speed: {fault | no fresh reading | no reading yet}", "Blowers at full speed: fault"), "fine", "Test: test_blower_rule:313."),
    ('{kEvBlowersFullEnd, "Blowers\' full speed ended"}', ("Blowers' full speed ended ({why})", "Blowers' full speed ended (fault)"), "Blowers' full speed ended", "repeat", "Test: test_blower_rule:315."),
]):
    ev(f"EVT-BL-{i + 1:02d}", snip, cur, prop, why, note)
for i, (snip, cur, prop, why, note) in enumerate([
    ('{kEvIonStart, "Ion generator run started"}', ("Ion generator run started: {n} min (slot {hh:mm} UTC){; shortened: at most 10 minutes in 24 hours}", "Ion generator run started: 2 min (slot 12:00 UTC)"), ("Ion generator on: {n} min{ (10-minute limit)}", "Ion generator on: 2 min"), "inconsistent", "UTC slot dropped (the event's own time is UK). Test: test_ion_schedule:669."),
    ('{kEvIonEnd, "Ion generator run ended"}', ("Ion generator run ended: on {m} min {s} s", "Ion generator run ended: on 2 min 0 s"), ("Ion generator off after {duration}", "Ion generator off after 2 min"), "style", "Rule 8: “2 min”, not “2 min 0 s”."),
    ('{kEvIonCutShort, "Ion generator run cut short"}', ("Ion generator run cut short: {why}; on {m} min {s} s", "Ion generator run cut short: fault; on 0 min 45 s"), ("Ion generator stopped early: {why}, after {duration}", "Ion generator stopped early: fault, after 45 s"), "style", "Test: test_ion_schedule:686."),
    ('{kEvIonSkipped, "Ion generator run skipped"}', ("Ion generator run skipped: {why} (slot {hh:mm} UTC | from boot, time not trusted)", "Ion generator run skipped: boot hold (slot 00:00 UTC)"), ("Ion run skipped: {why}", "Ion run skipped: starting"), "inconsistent", "Why words: ION rows. Test: test_ion_schedule:698."),
    ('{kEvIonStopped, "Ion generator run stopped"}', ("Ion generator run stopped: switched off; on {m} min {s} s", "Ion generator run stopped: switched off; on 1 min 3 s"), ("Ion generator switched off after {duration}", "Ion generator switched off after 1 min 3 s"), "style", ""),
    ('{kEvIonRecordFailed, "Ion generator\'s cap record not saved"}', ("Ion generator's cap record not saved: no new run starts until it can be saved (tried again every minute)", "…"), ("Ion generator paused: can't save its record", "Ion generator paused: can't save its record"), "too long", "Test: test_ion_schedule:585."),
    ('{kEvIonRecordSaved, "Ion generator\'s cap record saved again"}', ("Ion generator's cap record saved again: runs resume", "Ion generator's cap record saved again: runs resume"), ("Ion generator record saved: runs resume", "Ion generator record saved: runs resume"), "style", ""),
]):
    ev(f"EVT-ION-{i + 1:02d}", snip, cur, prop, why, note)
ev("EVT-SC-01", '"Test scenario"', ("Test scenario: {logged}", "Test scenario: …"), "=", "fine", "Test builds only.")
ev("EVT-ZZ-01", 'labelled(out, n, type && *type ? type : "Event", d)', ("{type}: {logged} (an event this page doesn't know)", "future: a new kind of event"), "=", "fine", "Fallback for newer firmware's events.")

# Ion and blower reason words (firmware → API → page)
IO = "lib/core/src/wineador/ion_schedule.cpp"
for i, (code, cur, prop, why) in enumerate([("BootHold", "boot hold", "starting", "jargon"), ("Updating", "update", "updating", "style"), ("Fault", "fault", "=", "fine"),
                                             ("Cap", "at most 10 minutes in 24 hours", "10-minute limit", "too long"), ("SwitchedOff", "switched off", "=", "fine"),
                                             ("RecordUnsaved", "cap record can't be saved", "can't save its record", "jargon")]):
    r(f"ION-{i + 1:02d}", "dashboard · Overview + Events", "Ion · why", IO, f'case IonWhy::{code}: return', cur, prop, why, "[api] A text field (loads.ion.why) read by the page: a page-side map keeps the API as it is." if prop != "=" else "")
BR = "lib/core/src/wineador/blower_rule.cpp"
r("BLW-01", "dashboard · Overview + Events", "Blowers · why", BR, 'case BlowerWhy::Boot: return "no reading yet since boot";', "no reading yet since boot", "no reading yet", "too long", "[api] as ION rows.")
r("BLW-02", "dashboard · Overview + Events", "Blowers · why", BR, 'case BlowerWhy::Stale: return "no fresh reading";', "no fresh reading", "=", "fine", "")
r("BLW-03", "dashboard · Overview + Events", "Blowers · why", BR, 'case BlowerWhy::Fault: return "fault";', "fault", "=", "fine", "")

# ---- round 2: strings that are new
JS2, IX2, ES2 = "web/app.js", "web/index.html", "lib/core/src/wineador/event_sentence.cpp"
r("DB-OV-80", O, "Humidity · line under bar", JS2, 'Spread ${Math.max(...rhs) - Math.min(...rhs)}%', "NEW",
  ("Spread {s}% ({min}–{max}) · read {ago} ago", "Spread 2% (61–63) · read 1 s ago"), "new",
  "F7: matches the temperature line; the spread from the healthy cabinet sensors' humidity in the status (no API change). Empty while there is no reading.")
r("DB-OV-81", O, "Temperature · no fresh reading", JS2, '"No fresh reading"', "NEW", "No fresh reading", "new", "With 3 or more healthy sensors but a stale reading (DB-OV-15).")
r("DB-OV-82", O, "Loads · heater, humidifier off", JS2, 'offInSettings("heater_enable") ? "Off in Settings"', "NEW", "Off in Settings", "new",
  "Replaces DB-OV-12 and DB-OV-37: shown whenever the switch is off. Also a load held by the `disabled` guard.")
r("DB-EV-17", E, "Row · type, screen reader", JS2, '<span class="sr">${esc(word("event_types", e.type))}</span>', "NEW", ("{type name}", "Watchdog"), "new",
  "The row shows the type's dot only (DB-EV-04); its name for screen readers (rule 9).")
r("DB-EV-18", E, "Door test · placeholder", IX2, 'placeholder="e.g. door open 30 s"', "NEW", "e.g. door open 30 s", "new", "Under the visible label DB-EV-13.")
r("DB-UP-30", "dashboard · Update", "Facts · bench test", JS2, '"Bench test build"', "NEW", "Bench test build · Bench build · Test build · Cabinet build", "new", "Word list.")
for i, (pos, name) in enumerate([("top_front", "Top front"), ("bottom_front", "Bottom front"), ("mid_front", "Middle front"),
                                 ("mid_rear", "Middle rear"), ("chamber", "Chamber"), ("ambient", "Room")]):
    r(f"POS-{i + 1:02d}", "LCD + dashboard", "Sensor position", "lib/core/src/wineador/sensor_table.cpp", f'"{pos}", ', "NEW", name, "new",
      f"The one source (spec 5.5): kSensors[].name; web/words.js is made from it (tools/make_web_words.py, test_web_words). The code “{pos}” stays in the logs and the API.")
r("EVT-SN-05", E, "Event sentence", ES2, '"%s: %lu more bad reads or no answers this hour"', "NEW",
  ("{Position}: {n} more bad reads or no answers this hour", "Middle rear: 5 more bad reads or no answers this hour"), "new", "The logged line counts both together.")
r("EVT-IN-02", E, "Event sentence", ES2, '"%s working again"', "NEW", ("{Heater|Cooling|Humidifier} working again", "Heater working again"), "new", "")
r("EVT-ST-03", E, "Event sentence", ES2, '"%s excluded by hand"', "NEW", ("{Position} excluded by hand / {Position} back in use", "Middle rear excluded by hand (dashboard)"), "new", "Inside EVT-ST-01.")

if errors or unresolved:
    print("\n".join(errors + unresolved), file=sys.stderr)
    sys.exit(1)
ids = [x[0] for x in rows]
dup = {i for i in ids if ids.count(i) > 1}
assert not dup, dup
with open(os.path.join(ROOT, "docs/ui-strings.csv"), "w", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(["id", "surface", "screen_or_state", "file:line", "current", "was", "reason", "chars_current", "chars_was", "fits", "notes"])
    w.writerows(rows)
from collections import Counter
print(len(rows), "rows;", dict(Counter(x[6] for x in rows)))
print("LCD not fitting:", [x[0] for x in rows if x[9].startswith("NO")])
