#!/usr/bin/env python3
"""Check the wineador SD card logs on the Mac (spec 5.4; B3 test 24; B4 step 1).

Usage (card in the Mac's reader, mounted as WINEADOR):
    python3 tools/check_logs.py /Volumes/WINEADOR --profile bench --test24
    python3 tools/check_logs.py /Volumes/WINEADOR --profile real  --soak

Only reads the card. Prints each check as PASS / FAIL / NOTE and a summary.

What it checks
  every run   folders and file names by UTC month; headers (schema, board, firmware,
              profile, each sensor's serial with rh_gain / rh_offset as in
              lib/core/src/wineador/sensor_table.cpp); every row has the header's
              columns; each RH column = gain x raw + offset (clamped 0-100), for
              the logic's and (test builds) the real readings; each row's month
              matches its file; time_trusted is 0/1; file dates are real (not
              1980); loop_max_ms present (its maximum is reported).
              Schemas (spec 5.4): 1 = B1-B4 cards, channels ch2-ch7 (Rev N's mux numbers),
              no -sN in the file names; 2 = channels ch0-ch5, a "# board" line, -s2
              in every file name (hourly-s2.csv too), and the v12_mean, v12_min_1s and
              psram_min columns (empty on Rev N). A card may hold both.
  --test24    every 5.4 event type present (disturbance exempt until B6), the RTC
              lost-time and NTP-correction events, the retention deletion.
  --stress    B4's SD stress run (events file): every write made, none lost, retries
              counted, no file the wrong size, the radio cycled through an NTP window,
              lowest free heap since boot at or above 30 KB.
  --stress    on a Rev Q card (test 38): WiFi connected all through the run (no "WiFi lost"),
              and the stress build's retry never power-cycled the card.
  --outage    on a Rev Q card (test 38, the normal build): each card pull shows one power-cycle
              event, then SD FAIL, then the card back.
  --bulb      BP2 test 35: the bulb sketch's own log, /bp2/bulb.csv - every switch-on made, every
              reset clean (a boot with its reason), and no freeze (the log never just stops).
  --soak      no reset (one boot); the scenario's own verdict; the guard audit again from the events file
              (min off, min on, max run, crossover, interlock side) using
              uptime_ms; the load counts; heap, SD write time and loop time not
              creeping (first vs last 2 hours); CRC failures on the real sensors.
"""
import argparse
import csv
import datetime as dt
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Safety timers, ms (spec 5.5 real; B3 table bench)
TIMERS = {
    "real":  dict(c_off=300000, c_on=60000, c_max=1800000, h_off=120000, h_on=60000, h_max=3600000,
                  u_off=300000, u_max=1800000, xover=600000),
    "bench": dict(c_off=30000, c_on=10000, c_max=120000, h_off=20000, h_on=10000, h_max=120000,
                  u_off=20000, u_max=120000, xover=30000),
}
SLACK_MS = 1500
HEAP_FLOOR = 30 * 1024          # spec 5.2: free heap never below 30 KB

EVENT_TYPES = ["compressor", "heater", "humidifier", "interlock", "harvest", "guard", "crc", "timeout", "sensor",
               "fan", "settings", "boot", "sd", "i2c", "rtc", "ntp", "crossover", "strip", "exercise", "ineffective"]

results = []


def report(ok, what, detail=""):
    tag = "PASS" if ok is True else "FAIL" if ok is False else "NOTE"
    results.append(tag)
    print(f"  {tag:4}  {what}" + (f" - {detail}" if detail else ""))


def sensor_table():
    """(channel 0-5, serial, position, gain, offset) from the firmware's own table."""
    src = open(os.path.join(ROOT, "lib/core/src/wineador/sensor_table.cpp")).read()
    rows = re.findall(r"\{(\d+),\s*0x([0-9A-Fa-f]+),\s*\"\w+\",\s*\"(\w+)\",\s*([-\d.]+)f,\s*([-\d.]+)f\}", src)
    return [(int(c), int(s, 16), pos, float(g), float(o)) for c, s, pos, g, o in rows]


KNOWN_SCHEMAS = (1, 2)
BOARD_LINES = ("# board rev-n (channel n = mux n+2)", "# board rev-q (channel n = mux n)")


def file_schema(comments):
    """The schema number from a file's '# schema N' line, or None."""
    for c in comments:
        m = re.fullmatch(r"# schema (\d+)", c)
        if m:
            return int(m.group(1))
    return None


def name_schema(path):
    """The schema a file's name says: '-sN' in it, or 1 with none."""
    m = re.search(r"-s(\d+)(-p\d+)?\.csv$", os.path.basename(path))
    return int(m.group(1)) if m else 1


def table_for(table, schema):
    """The sensor table with the channel numbers that schema used: schema 1 numbered
    them by Rev N's mux (ch2-ch7), schema 2 on uses ch0-ch5 (spec 2.3, 5.4)."""
    return [(ch + 2 if schema == 1 else ch, serial, pos, g, o) for ch, serial, pos, g, o in table]


def read_csv(path):
    """(comment lines, header list, rows as dicts)."""
    comments, lines = [], []
    with open(path, newline="", errors="replace") as f:
        for line in f:
            if line.startswith("#"):
                comments.append(line.rstrip("\n"))
            else:
                lines.append(line)
    if not lines:
        return comments, [], []
    reader = csv.reader(lines)
    header = next(reader)
    rows = [r for r in reader if r]
    return comments, header, rows


def check_header(path, comments, kind, profile, table):
    """Returns the file's schema (or None if the header is unusable)."""
    name = os.path.basename(path)
    if f"# wineador {kind}" not in comments:
        report(False, f"{name} header", f"missing '# wineador {kind}'")
        return None
    schema = file_schema(comments)
    if schema not in KNOWN_SCHEMAS:
        report(False, f"{name} header", f"schema {schema}: this checker knows {KNOWN_SCHEMAS}")
        return None
    if name_schema(path) != schema:
        report(False, f"{name} name", f"says schema {name_schema(path)}, header says {schema} (5.4: one schema per file)")
        return None
    board = [c for c in comments if c.startswith("# board ")]
    if schema >= 2 and (len(board) != 1 or board[0] not in BOARD_LINES):
        report(False, f"{name} header", f"board line {board[0] if board else 'missing'}; expected one of {BOARD_LINES}")
        return None
    table = table_for(table, schema)
    prof = [c for c in comments if c.startswith("# profile ")]
    if not prof or not prof[0].startswith(f"# profile {profile}"):
        # A month's file is made by the first build that logs that month; a later build
        # appends to it. Only fail when no boot in the file is from the expected profile.
        boots = []
        if kind == "events":
            boots = [e for e in load_events([path]) if e["type"] == "boot" and f"profile {profile.upper()}" in e["detail"]]
        if boots:
            report(None, f"{os.path.basename(path)}: made by an earlier build ({prof[0][2:] if prof else 'none'}); "
                         f"{len(boots)} later boot(s) as {profile}")
        else:
            report(False, f"{os.path.basename(path)} header profile", f"{prof[0] if prof else 'none'}; expected {profile}")
            return schema
    if not any(c.startswith("# firmware ") for c in comments):
        report(False, f"{os.path.basename(path)} header", "no firmware line")
        return schema
    if kind == "readings":
        for ch, serial, pos, g, o in table:
            line = f"# ch{ch} serial 0x{serial:08X} {pos} rh_gain {g:.4f} rh_offset {o:.3f}"
            if line not in comments:
                report(False, f"{os.path.basename(path)} header", f"missing or wrong: '{line}'")
                return schema
    report(True, f"{os.path.basename(path)} header",
           f"schema {schema}" + (f", {board[0][2:]}" if board else "") + f", {prof[0][2:]}")
    return schema


def check_readings(path, table):
    comments, header, rows = read_csv(path)
    table = table_for(table, file_schema(comments) or 1)
    name = os.path.basename(path)
    month = name[:7]
    bad_cols = sum(1 for r in rows if len(r) != len(header))
    report(bad_cols == 0, f"{name}: {len(rows)} rows, every row has {len(header)} columns",
           f"{bad_cols} rows wrong" if bad_cols else "")
    idx = {h: i for i, h in enumerate(header)}
    rh_bad, month_bad, trust_bad = 0, 0, 0
    loop_max = 0.0
    for r in rows:
        if len(r) != len(header):
            continue
        if not r[0].startswith(month):
            month_bad += 1
        if r[idx["time_trusted"]] not in ("0", "1"):
            trust_bad += 1
        try:
            loop_max = max(loop_max, float(r[idx["loop_max_ms"]] or 0))
        except ValueError:
            pass
        for ch, serial, pos, g, o in table:
            for suffix in ("", "_real"):
                rh_k, raw_k = f"ch{ch}_rh{suffix}", f"ch{ch}_rh_raw{suffix}"
                if rh_k not in idx or not r[idx[rh_k]]:
                    continue
                rh, raw = float(r[idx[rh_k]]), float(r[idx[raw_k]])
                want = min(100.0, max(0.0, g * raw + o))
                if abs(rh - want) > 0.011:
                    rh_bad += 1
    report(rh_bad == 0, f"{name}: RH = gain x raw + offset (each serial, per the build log)", f"{rh_bad} values off" if rh_bad else "")
    report(month_bad == 0, f"{name}: every row's timestamp is in {month} (UTC month)", f"{month_bad} rows not" if month_bad else "")
    report(trust_bad == 0, f"{name}: time_trusted is 0 or 1")
    report("loop_max_ms" in idx, f"{name}: loop time logged (max per interval)", f"largest {loop_max:.1f} ms")
    # The heap floor (5.2): the lowest free heap since boot, in every row from the build that added it.
    if "heap_min" in idx:
        lows = [int(r[idx["heap_min"]]) for r in rows if len(r) == len(header) and r[idx["heap_min"]].isdigit()]
        if lows:
            report(min(lows) >= HEAP_FLOOR, f"{name}: lowest free heap since boot at or above 30 KB",
                   f"lowest {min(lows)} bytes")
    else:
        report(None, f"{name}: no heap_min column (written by a build before the 30 KB floor)")
    # Schema 2 (5.4): the 12V mean and lowest 1 s average in each interval, and the lowest-ever
    # free PSRAM - filled on Rev Q, empty on Rev N (no 12V sense, no PSRAM).
    if (file_schema(comments) or 1) >= 2:
        cols = ("v12_mean", "v12_min_1s", "psram_min")
        missing = [c for c in cols if c not in idx]
        report(not missing, f"{name}: 12V and PSRAM columns present", f"missing {missing}" if missing else "")
        if not missing:
            revn = "# board rev-n (channel n = mux n+2)" in comments
            good = [r for r in rows if len(r) == len(header)]
            filled = {c: [r[idx[c]] for r in good if r[idx[c]] != ""] for c in cols}
            if revn:
                report(all(not v for v in filled.values()), f"{name}: 12V and PSRAM columns empty (Rev N has neither)",
                       ", ".join(f"{c} {len(v)} filled" for c, v in filled.items() if v))
            else:
                def num(x):
                    try:
                        return float(x)
                    except ValueError:
                        return None
                bad = [x for c in cols for x in filled[c] if num(x) is None]
                volts = [num(x) for c in cols[:2] for x in filled[c] if num(x) is not None]
                odd = [v for v in volts if not 0.0 <= v <= 30.0]
                report(not bad and not odd, f"{name}: 12V and PSRAM values are numbers (12V within 0-30 V)",
                       f"{len(bad)} not numbers, {len(odd)} outside 0-30 V" if bad or odd else "")
                lows = [num(x) for x in filled["v12_min_1s"] if num(x) is not None]
                if lows:
                    lowest = min(lows)
                    report(None, f"{name}: 12V lowest 1 s average {lowest:.2f} V" + (" (below 11.0 V: see the 12v events)" if lowest < 11.0 else ""))
                ps = [num(x) for x in filled["psram_min"] if num(x) is not None]
                if ps:
                    report(None, f"{name}: lowest free PSRAM since boot {int(min(ps))} bytes")
    return header, rows


def check_bulb(card):
    """BP2 test 35: the bulb sketch's log on the card, /bp2/bulb.csv.
    Lines: uptime_ms,kind,cycle,reset_reason - kind boot / on / off / done / abort."""
    path = os.path.join(card, "bp2", "bulb.csv")
    print(f"\nwineador bulb surge check (BP2 test 35): {path}\n")
    if not os.path.isfile(path):
        report(False, "/bp2/bulb.csv exists", "no log: was the card in J22 during the test?")
        return finish()
    lines = []
    with open(path, errors="replace") as f:
        for raw in f:
            p = raw.strip().split(",")
            if len(p) >= 3 and p[0].isdigit() and p[2].lstrip("-").isdigit():
                lines.append(dict(up=int(p[0]), kind=p[1], n=int(p[2]), reason=p[3] if len(p) > 3 else ""))
    report(bool(lines), "the log has entries", f"{len(lines)} lines")
    if not lines:
        return finish()
    # The log can hold several runs: each ends with 'done'. BP2 runs it twice: the hardware
    # watchdog paused, then 'hwdog on'. Each run is checked.
    runs, cur = [], []
    for l in lines:
        cur.append(l)
        if l["kind"] == "done":
            runs.append(cur)
            cur = []
    if cur:
        runs.append(cur)
    dog_on = False                     # the watchdog's state as the log goes: every boot starts it paused
    states = []
    for k, run in enumerate(runs, 1):
        on_with_dog = False            # this run's switch-ons were made with the watchdog toggling
        for l in run:
            if l["kind"] == "boot":
                dog_on = False
            elif l["kind"] in ("hwdog_on", "hwdog_off"):
                dog_on = l["kind"] == "hwdog_on"
            elif l["kind"] == "on" and dog_on:
                on_with_dog = True
        label = f"run {k} (hardware watchdog {'ON' if on_with_dog else 'paused'})"
        states.append(on_with_dog)
        seq = [l for l in run if l["kind"] in ("boot", "on", "off", "done", "abort")]
        if not seq:
            continue
        ons = sorted({l["n"] for l in seq if l["kind"] == "on"})
        report(any(l["kind"] == "done" and l["n"] >= 50 for l in seq), f"{label}: all 50 switch-ons completed ('done')",
               f"switch-ons logged: {len(ons)}")
        resets = [l for i, l in enumerate(seq) if l["kind"] == "boot" and i > 0]
        for l in resets:
            report(None, f"{label}: reset after {l['n']} cycles, reason {l['reason'] or '?'} (the board restarted)")
        report(all(l["reason"] for l in seq if l["kind"] == "boot"), f"{label}: every boot logged with its reset reason")
        hung = [l for i, l in enumerate(seq) if l["kind"] == "on" and
                (i + 1 >= len(seq) or seq[i + 1]["kind"] not in ("off", "boot", "abort"))]
        last = seq[-1]
        report(last["kind"] in ("done", "abort", "boot") and not hung,
               f"{label}: no freeze (ends with 'done'; every switch-on followed by its switch-off or a reset)",
               f"the log stops at '{last['kind']},{last['n']}' - a freeze, or the card pulled"
               if last["kind"] not in ("done", "abort", "boot") else (f"{len(hung)} switch-ons with nothing after" if hung else ""))
    done = [st for st, run in zip(states, runs) if any(l["kind"] == "done" for l in run)]
    report((False in done) and (True in done),
           "both runs done: one with the hardware watchdog paused, one with 'hwdog on' (BP2 test 35)",
           f"{len(done)} complete run(s): " + ", ".join("ON" if d else "paused" for d in done))
    return finish()


def file_date_ok(path):
    """The file's date comes from the board's system clock. Real (2026 on), or - for a
    file written while the clock had lost time - the same year as its own name (5.4:
    rows go to the month the clock says). 1980 means the clock was never set."""
    y = dt.datetime.fromtimestamp(os.stat(path).st_mtime).year
    name = os.path.basename(path)
    named = int(name[:4]) if name[:4].isdigit() else None
    return y >= 2026 or (named is not None and y == named and y != 1980), y


def load_events(paths):
    """Every event, in the order of the files given (the caller lists them in time order)."""
    events = []
    for p in paths:
        comments, header, rows = read_csv(p)
        for r in rows:
            if len(r) >= 5:
                events.append(dict(ts=r[0], trusted=r[1], up=int(r[2]), type=r[3], detail=r[4]))
    return events


def audit(events, t):
    """Guard audit from load ON/OFF events, per boot (uptime restarts at each boot)."""
    breaches, starts, maxstops = [], {"compressor": 0, "heater": 0, "humidifier": 0}, {"compressor": 0, "heater": 0, "humidifier": 0}
    min_off = {"compressor": t["c_off"], "heater": t["h_off"], "humidifier": t["u_off"]}
    min_on = {"compressor": t["c_on"], "heater": t["h_on"], "humidifier": 0}
    max_run = {"compressor": t["c_max"], "heater": t["h_max"], "humidifier": t["u_max"]}
    on_at, off_at, k1 = {}, {}, "cool"
    for e in events:
        if e["type"] == "boot":
            on_at, off_at, k1 = {}, {}, "cool"
            continue
        if e["type"] == "interlock":
            k1 = "heat" if "heating side" in e["detail"] else "cool"
            continue
        L = e["type"]
        if L not in starts:
            continue
        up = e["up"]
        words = e["detail"].split(" ")
        if len(words) > 1 and words[1] == "ON":
            starts[L] += 1
            if L in off_at and up - off_at[L] + SLACK_MS < min_off[L]:
                breaches.append(f"{e['ts']} {L} started {up - off_at[L]} ms after stopping (min off {min_off[L]})")
            other = {"compressor": "heater", "heater": "compressor"}.get(L)
            if other and other in off_at and up - off_at[other] + SLACK_MS < t["xover"]:
                breaches.append(f"{e['ts']} {L} started {up - off_at[other]} ms after the {other} stopped (crossover {t['xover']})")
            if L == "compressor" and k1 != "cool":
                breaches.append(f"{e['ts']} compressor ON with K1 on the heating side")
            if L == "heater" and k1 != "heat":
                breaches.append(f"{e['ts']} heater ON with K1 on the cooling side")
            on_at[L] = up
        else:
            if L in on_at:
                ran = up - on_at.pop(L)
                if ran + SLACK_MS < min_on[L] and "max-run" not in e["detail"]:
                    breaches.append(f"{e['ts']} {L} stopped after {ran} ms (min on {min_on[L]})")
                if ran > max_run[L] + SLACK_MS:
                    breaches.append(f"{e['ts']} {L} ran {ran} ms (max run {max_run[L]})")
                if "(max-run)" in e["detail"]:
                    maxstops[L] += 1
            off_at[L] = up
    return breaches, starts, maxstops


def mean(xs):
    return sum(xs) / len(xs) if xs else 0.0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("card", help="the card's mount point, e.g. /Volumes/WINEADOR")
    ap.add_argument("--profile", choices=["bench", "real"])
    ap.add_argument("--test24", action="store_true")
    ap.add_argument("--soak", action="store_true")
    ap.add_argument("--stress", action="store_true",
                    help="B4's SD stress run: no lost writes, retries counted, verify and size checks, heap floor")
    ap.add_argument("--outage", action="store_true",
                    help="the SD stress test's card pull: everything kept in RAM reached the card, in order")
    ap.add_argument("--bulb", action="store_true", help="BP2 test 35: the bulb sketch's log, /bp2/bulb.csv")
    a = ap.parse_args()

    if a.bulb:
        return check_bulb(a.card)
    if not a.profile:
        ap.error("--profile is required (except with --bulb)")
    logs = os.path.join(a.card, "logs")
    print(f"\nwineador log check: {logs}  (profile {a.profile})\n")
    if not os.path.isdir(logs):
        report(False, "/logs exists")
        return finish()
    table = sensor_table()
    report(len(table) == 6, "sensor table read from the firmware source", f"{len(table)} sensors")

    readings, events_files = [], []
    for year in sorted(os.listdir(logs)):
        yd = os.path.join(logs, year)
        if not os.path.isdir(yd):
            continue
        for f in sorted(os.listdir(yd)):
            if f.startswith("._"):
                continue
            p = os.path.join(yd, f)
            if re.fullmatch(rf"{year}-\d\d-readings(-s\d+)?(-p\d+)?\.csv", f):
                readings.append(p)
            elif re.fullmatch(rf"{year}-\d\d-events(-s\d+)?(-p\d+)?\.csv", f):
                events_files.append(p)
            elif not f.startswith("."):
                report(None, f"unexpected file {year}/{f}")
    # In time order: by month, then schema (a month can change schema part way), then part file.
    # (By name alone "...-readings-s2.csv" would sort before "...-readings.csv".)
    def in_time_order(p):
        m = re.search(r"-p(\d+)\.csv$", p)
        return (os.path.basename(p)[:7], name_schema(p), int(m.group(1)) if m else 1)
    readings.sort(key=in_time_order)
    events_files.sort(key=in_time_order)
    report(bool(readings) and bool(events_files), "files as /logs/YYYY/YYYY-MM-readings.csv and -events.csv",
           f"{len(readings)} readings, {len(events_files)} events files")
    # hourly.csv (schema 1) and hourly-sN.csv (schema N); a part file -pN would only follow a 3.9 GB one.
    hourlies = sorted((os.path.join(logs, f) for f in os.listdir(logs)
                       if re.fullmatch(r"hourly(-s\d+)?(-p\d+)?\.csv", f)), key=lambda p: (name_schema(p), p))
    report(bool(hourlies), "an hourly file exists (/logs/hourly.csv, or hourly-sN.csv from schema 2)",
           ", ".join(os.path.basename(p) for p in hourlies))

    for p in readings + events_files + hourlies:
        ok, y = file_date_ok(p)
        if not ok:
            report(False, f"{os.path.basename(p)} file date", f"year {y}: the system clock was not set from the RTC")
    if all(file_date_ok(p)[0] for p in readings + events_files):
        report(True, "file dates from the system clock (none dated 1980)")
    for p in readings + events_files:
        name = os.path.basename(p)
        if name[:4].isdigit() and int(name[:4]) < 2026:
            comments, h, rows = read_csv(p)
            trusted = [r[1] for r in rows if len(r) > 1]
            report(None, f"{name}: written while the clock read {name[:7]} (lost time); time_trusted values {sorted(set(trusted))}")

    all_rows, header, schema = [], [], 1
    for p in readings:
        comments, h, rows = read_csv(p)
        schema = check_header(p, comments, "readings", a.profile, table) or 1
        header, rows = check_readings(p, table)
        all_rows += rows
    for p in events_files:
        comments, h, rows = read_csv(p)
        check_header(p, comments, "events", a.profile, table)
        report(h == ["timestamp", "time_trusted", "uptime_ms", "type", "detail"], f"{os.path.basename(p)} columns")
    for hourly in hourlies:
        hn = os.path.basename(hourly)
        comments, h, rows = read_csv(hourly)
        check_header(hourly, comments, "hourly", a.profile, table)
        bad = sum(1 for r in rows if len(r) != len(h))
        report(bad == 0, f"{hn}: {len(rows)} rows with {len(h)} columns", f"{bad} wrong" if bad else "")
        if (file_schema(comments) or 1) >= 2:            # 5.4: on Rev Q the hour's 12V and the lowest PSRAM
            cols = ("v12_mean", "v12_min_1s", "psram_min")
            missing = [c for c in cols if c not in h]
            report(not missing, f"{hn}: 12V and PSRAM columns present", f"missing {missing}" if missing else "")
            if not missing:
                good = [r for r in rows if len(r) == len(h)]
                filled = sum(1 for r in good for c in cols if r[h.index(c)] != "")
                if "# board rev-n (channel n = mux n+2)" in comments:
                    report(filled == 0, f"{hn}: 12V and PSRAM columns empty (Rev N has neither)", f"{filled} filled" if filled else "")
                else:
                    def num(x):
                        try:
                            return float(x)
                        except ValueError:
                            return None
                    vals = [r[h.index(c)] for r in good for c in cols if r[h.index(c)] != ""]
                    bad = [x for x in vals if num(x) is None]
                    volts = [num(r[h.index(c)]) for r in good for c in cols[:2] if r[h.index(c)] != "" and num(r[h.index(c)]) is not None]
                    odd = [v for v in volts if not 0.0 <= v <= 30.0]
                    report(not bad and not odd, f"{hn}: 12V and PSRAM values are numbers (12V within 0-30 V)",
                           f"{len(bad)} not numbers, {len(odd)} outside 0-30 V" if bad or odd else "")
        if "heap_min" in h:
            k = h.index("heap_min")
            lows = [int(r[k]) for r in rows if len(r) == len(h) and r[k].isdigit()]
            if lows:
                report(min(lows) >= HEAP_FLOOR, f"{hn}: lowest free heap since boot at or above 30 KB",
                       f"lowest {min(lows)} bytes")

    events = load_events(events_files)
    # Which board wrote the card (schema 2's "# board" line; schema 1 cards are all Rev N).
    revq = any(BOARD_LINES[1] in read_csv(p)[0] for p in events_files)
    types = {e["type"] for e in events}
    report(None, f"{len(events)} events; types seen: {', '.join(sorted(types))}")

    if a.test24:
        missing = [t for t in EVENT_TYPES if t not in types]
        report(not missing, "every 5.4 event type present (disturbance exempt until B6)",
               f"missing: {', '.join(missing)}" if missing else "")
        report(any(e["type"] == "rtc" and "LOST TIME" in e["detail"] for e in events), "RTC lost time at boot logged")
        report(any(e["type"] == "ntp" and "correction" in e["detail"] for e in events), "NTP correction logged")
        report(any(e["type"] == "sd" and "deleted" in e["detail"] for e in events), "retention: oldest month deleted at boot")
        report(any(e["type"] == "sd" and "FAILURE" in e["detail"] for e in events), "SD flush failure logged")
        report(any(e["type"] == "settings" and "->" in e["detail"] for e in events), "settings change logged with old and new")
        if revq:
            # B3 test 24 on Rev Q (BP2, Rev Q exceptions): both Rev Q events are required.
            # 12V dip: the PSU at 10.5V for ~5 s, by hand -> a dip, then its recovery, lowest near 10.5V,
            # lasting 2-20 s (wide: the PSU is turned by hand).
            dips = [i for i, e in enumerate(events) if e["type"] == "12v" and e["detail"].startswith("12V DIP")]
            recs = [(i, e) for i, e in enumerate(events) if e["type"] == "12v" and e["detail"].startswith("12V recovered")]
            pair = next(((d, r) for d in dips for r in recs if r[0] > d), None)
            report(pair is not None, "12V dip logged, then its recovery (test 24, Rev Q)",
                   "" if pair else f"{len(dips)} dip and {len(recs)} recovered events")
            if pair:
                m = re.search(r"after (\d+) ms; lowest ([\d.]+) V", pair[1][1]["detail"])
                if m:
                    ms, low = int(m.group(1)), float(m.group(2))
                    report(10.0 <= low <= 10.9 and 2000 <= ms <= 20000,
                           "the recovery's lowest reading near 10.5V and duration near 5 s (10.0-10.9V, 2-20 s: the PSU is turned by hand)",
                           f"lowest {low:.2f} V, {ms / 1000:.1f} s")
            # WiFi: the router off for ~1 minute -> "WiFi lost", then "WiFi back after N s", N about 60.
            lost = [i for i, e in enumerate(events) if e["type"] == "wifi" and e["detail"].startswith("WiFi lost")]
            backs = [(i, e) for i, e in enumerate(events) if e["type"] == "wifi" and e["detail"].startswith("WiFi back after")]
            wp = next(((l, b) for l in lost for b in backs if b[0] > l), None)
            report(wp is not None, "WiFi lost logged, then back (test 24, Rev Q)",
                   "" if wp else f"{len(lost)} lost and {len(backs)} back events")
            if wp:
                m = re.search(r"back after (\d+) s", wp[1][1]["detail"])
                if m:
                    sec = int(m.group(1))
                    report(30 <= sec <= 180, "WiFi back after about the time the router was off (30-180 s for ~1 minute)",
                           f"back after {sec} s")

    if a.stress:
        done = [e for e in events if e["type"] == "sd" and e["detail"].startswith("SD STRESS DONE:")]
        checks = [e for e in events if e["type"] == "sd" and e["detail"].startswith("SD STRESS DONE checks:")]
        if not done:
            report(False, "the SD stress run's result is in the events file", "no 'SD STRESS DONE' line")
        else:
            d = done[-1]["detail"]
            m = re.search(r"(\d+) of (\d+) writes, (\d+) failures, (\d+) retries", d)
            if m:
                w, target, fails, retries = (int(x) for x in m.groups())
                report(w == target, "every stress write was made", f"{w} of {target}")
                report(fails == 0, "no lost writes (every failed first try written by the retry)", f"{fails} lost")
                report(None, f"retries counted: {retries} in {w} writes" + (f" (1 in {w // retries})" if retries else ""))
            fails = [e for e in events if e["type"] == "sd" and e["detail"].startswith("SD FAIL #")]
            report(None, f"failed first tries recorded: {len(fails)}")
            if checks:
                c = checks[-1]["detail"]
                mm = re.search(r"size mismatches (\d+)", c)
                if mm:
                    report(int(mm.group(1)) == 0, "no file the wrong size (the verify step caught everything)",
                           f"{mm.group(1)} mismatches")
                mh = re.search(r"lowest since boot (\d+)", c)
                if mh:
                    report(int(mh.group(1)) >= HEAP_FLOOR, "lowest free heap since boot at or above 30 KB",
                           f"lowest {mh.group(1)} bytes")
        if revq and done:
            # Rev Q (BP2 test 38): WiFi stays connected all the time; the stress build's retry
            # never power-cycles the card, so the run is the card unaided.
            di = events.index(done[-1])
            # The run: from its "SD STRESS START" (firmware from Q3), else from the boot before it.
            run_i = max((i for i, e in enumerate(events[:di]) if e["type"] == "sd" and e["detail"].startswith("SD STRESS START")),
                        default=max((i for i, e in enumerate(events[:di]) if e["type"] == "boot"), default=0))
            run = events[run_i:di + 1]
            lost = [e for e in run if e["type"] == "wifi" and e["detail"].startswith("WiFi lost")]
            report(not lost, "WiFi connected all through the stress run (no 'WiFi lost')",
                   "; ".join(f"{e['ts']}" for e in lost[:5]))
            cyc = [e for e in run if e["type"] == "sd" and "power-cycled" in e["detail"]]
            report(not cyc, "the stress run never power-cycled the card (its retry doesn't; the card unaided)",
                   "; ".join(f"{e['ts']} {e['detail'][:60]}" for e in cyc[:3]))
        elif not revq:
            wifi = [e for e in events if e["type"] == "ntp" and e["detail"].startswith("WiFi ")]
            report(any("WiFi on" in e["detail"] for e in wifi) and any("WiFi off" in e["detail"] for e in wifi),
                   "the radio cycled through an NTP window (WiFi on, then off)",
                   "; ".join(f"{e['ts']} {e['detail'][:40]}" for e in wifi[-4:]))
        # Only this run's boot counts: the last boot before the stress run's result.
        if done:
            di = events.index(done[-1])
            boot_i = max((i for i, e in enumerate(events[:di]) if e["type"] == "boot"), default=0)
            below = [e for e in events[boot_i:] if e["type"] == "heap"]
            earlier = [e for e in events[:boot_i] if e["type"] == "heap"]
            report(not below, "no heap-floor event in the stress run's boot", below[0]["detail"][:90] if below else "")
            if earlier:
                report(None, f"heap-floor event in an earlier boot: {earlier[0]['ts']} {earlier[0]['detail'][:70]}")

    if a.outage:
        # Events in order: uptime never goes back within a boot.
        back, prev = 0, None
        for e in events:
            if e["type"] == "boot":
                prev = None
                continue
            if prev is not None and e["up"] < prev:
                back += 1
            prev = e["up"]
        report(back == 0, "events in order (uptime never goes back within a boot)", f"{back} out of order" if back else "")
        notes = [e for e in events if e["type"] == "sd" and "writing what was kept" in e["detail"]]
        for e in notes:
            report(None, f"{e['ts']} {e['detail']}")
        report(bool(notes), "the card came back and wrote what was kept")
        if revq:
            # Rev Q, the normal build (BP2 test 38, 3A.11): a card pull shows ONE power-cycle event
            # (the retry's remount failed), then SD FAIL, then the card back.
            seq = [e for e in events if e["type"] == "sd" and ("power-cycled" in e["detail"] or
                   e["detail"].startswith("SD FLUSH FAILURE") or "writing what was kept" in e["detail"])]
            fails = [i for i, e in enumerate(seq) if e["detail"].startswith("SD FLUSH FAILURE")]
            report(bool(fails), "a card pull: SD FAIL seen (normal build)", "" if fails else "no SD FLUSH FAILURE - was the card pulled?")
            for n, i in enumerate(fails):
                start = max([j + 1 for j in range(i) if "writing what was kept" in seq[j]["detail"]] + [0])
                cycles = [e for e in seq[start:i] if "power-cycled" in e["detail"]]
                back = any("writing what was kept" in e["detail"] for e in seq[i + 1:])
                report(len(cycles) == 1 and back,
                       f"card pull {n + 1} ({seq[i]['ts']}): one power-cycle event, then SD FAIL, then the card back",
                       f"{len(cycles)} power-cycle events before it; card back: {'yes' if back else 'NO'}")
        # Readings in order and without gaps: consecutive rows no more than 2.5 x the slow log interval apart.
        if all_rows and header:
            ts = [dt.datetime.strptime(r[0], "%Y-%m-%dT%H:%M:%SZ") for r in all_rows if len(r) == len(header)]
            order = sum(1 for x, y in zip(ts, ts[1:]) if y < x)
            gaps = [(x, y) for x, y in zip(ts, ts[1:]) if (y - x).total_seconds() > 150]
            report(order == 0, "readings rows in order", f"{order} out of order" if order else "")
            report(not gaps, "no gap in the readings (every row kept while the card was out reached it)",
                   "; ".join(f"{x:%H:%M:%S} -> {y:%H:%M:%S}" for x, y in gaps[:5]))
        for hourly in hourlies[-1:]:   # the newest schema's file: the one being written now
            comments, h, rows = read_csv(hourly)
            hours = [dt.datetime.strptime(r[0], "%Y-%m-%dT%H:%M:%SZ") for r in rows if r and r[0][:2] == "20" and int(r[1] or 0) == 1]
            missing = [x for x, y in zip(hours, hours[1:]) if (y - x).total_seconds() != 3600]
            report(not missing, "hourly rows consecutive (trusted hours)", f"jump after {missing[0]:%H:%M}" if missing else f"{len(hours)} hours")

    if a.soak:
        verdicts = [e for e in events if e["type"] == "scenario" and "========" in e["detail"]]
        if verdicts:
            last = verdicts[-1]["detail"]
            report("PASS:" in last, "the soak scenario's own verdict (from the events file)", last.split("========")[-1].strip())
        else:
            report(False, "the soak scenario's own verdict", "no scenario PASS/FAIL line in the events file")
        # No reset during the soak: no boot event between the soak's start and its verdict.
        # Boots before it (earlier tests on the same card) or after it are only noted.
        start = next((i for i, e in enumerate(events) if e["type"] == "scenario" and "[run 100" in e["detail"]), None)
        end = next((i for i, e in enumerate(events) if e["type"] == "scenario" and "======== run 100" in e["detail"]), None)
        if start is None or end is None:
            report(False, "no resets during the soak", "the soak's start or verdict is not in the events file")
        else:
            # Uptime only grows within one boot, so a reset shows as uptime dropping below the
            # soak's start, even if that boot lost the time and wrote to another month's file.
            # (Events kept through an SD outage are written late, so small steps back are normal.)
            win = events[start:end + 1]
            inside = [e for e in win if e["type"] == "boot"]
            back = sum(1 for e in win if e["up"] < win[0]["up"])
            hours = (win[-1]["up"] - win[0]["up"]) / 3.6e6
            total = sum(1 for e in events if e["type"] == "boot")
            report(not inside and back == 0, "no resets during the soak (uptime grows from its start to its verdict)",
                   "; ".join(f"{e['ts']} {e['detail'][:60]}" for e in inside[:3]) or
                   (f"{back} event(s) with uptime below the soak's start" if back else
                    f"{hours:.1f} h of uptime between start and verdict; {total} boot(s) on the card, none during the soak"))
        events = events[start:end + 1] if start is not None and end is not None else events
        fails = [e for e in events if e["type"] == "sd" and "FAILURE" in e["detail"]]
        retried = [e for e in events if e["type"] == "sd" and "retried after a remount: OK" in e["detail"]]
        report(not fails, "no SD failures", "; ".join(f"{e['ts']} {e['detail'][:90]}" for e in fails[:3]))
        report(None, f"SD writes recovered by a retry: {len(retried)}")
        breaches, starts, maxstops = audit(events, TIMERS[a.profile])
        report(not breaches, "no guard ever breached (min off, min on, max run, crossover, interlock side)",
               "; ".join(breaches[:5]) + (f" (+{len(breaches) - 5} more)" if len(breaches) > 5 else ""))
        # The expected counts over the 21 h run — the same minima as the soak scenario
        # (lib/core/src/wineador/scenario_stage6.cpp; the Mac run gave 15/12/15, 4/2/4, 2, 8).
        strips = sum(1 for e in events if e["type"] == "strip" and "START" in e["detail"])
        xovers = sum(1 for e in events if e["type"] == "crossover")
        mins = {"compressor": (10, 2), "heater": (6, 1), "humidifier": (4, 2)}
        for L, (s_min, m_min) in mins.items():
            report(starts[L] >= s_min, f"{L} ran the expected number of times", f"{starts[L]} starts (need {s_min})")
            report(maxstops[L] >= m_min, f"{L} ran into its max run", f"{maxstops[L]} max-run stops (need {m_min})")
        report(strips >= 2, "RH strips", f"{strips} (need 2)")
        report(xovers >= 4, "crossovers", f"{xovers} (need 4)")
        report(None, f"humidify calls {sum(1 for e in events if e['type'] == 'humidify' and 'START' in e['detail'])}")
        if all_rows and header:
            idx = {h: i for i, h in enumerate(header)}
            rows = [r for r in all_rows if len(r) == len(header)]
            n = len(rows)
            per2h = max(1, n // 12)
            def col(name, part):
                vals = []
                for r in part:
                    try:
                        vals.append(float(r[idx[name]]))
                    except (ValueError, KeyError):
                        pass
                return vals
            for name, label, limit in (("heap_free", "free heap", -2048), ("sd_worst_ms", "SD write time", 50),
                                       ("loop_max_ms", "loop time max", 5)):
                first, last = mean(col(name, rows[:per2h])), mean(col(name, rows[-per2h:]))
                if name == "heap_free":
                    report(last - first >= limit, f"{label} stable", f"first 2 h {first:.0f}, last 2 h {last:.0f} bytes")
                else:
                    report(last - first <= limit, f"{label} not creeping", f"first 2 h {first:.1f}, last 2 h {last:.1f} ms")
            crc = {}
            for ch, *_ in table_for(table, schema):
                k = f"ch{ch}_bus_real"
                if k in idx:
                    crc[ch] = sum(1 for r in rows if r[idx[k]] == "CRC FAIL")
            report(None, "CRC failures on the real sensors (logged reads): " + ", ".join(f"ch{c} {v}" for c, v in crc.items()))
    return finish()


def finish():
    fails = results.count("FAIL")
    print(f"\n  {results.count('PASS')} passed, {fails} failed, {results.count('NOTE')} notes\n")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
