#!/usr/bin/env python3
"""Check the wineador SD card logs on the Mac (spec 5.4; B3 test 24; B4 step 1).

Usage (card in the Mac's reader, mounted as WINEADOR):
    python3 tools/check_logs.py /Volumes/WINEADOR --profile bench --test24
    python3 tools/check_logs.py /Volumes/WINEADOR --profile real  --soak

Only reads the card. Prints each check as PASS / FAIL / NOTE and a summary.

What it checks
  every run   folders and file names by UTC month; headers (schema, firmware,
              profile, each sensor's serial with rh_gain / rh_offset as in
              lib/core/src/wineador/sensor_table.cpp); every row has the header's
              columns; each RH column = gain x raw + offset (clamped 0-100), for
              the logic's and (test builds) the real readings; each row's month
              matches its file; time_trusted is 0/1; file dates are real (not
              1980); loop_max_ms present (its maximum is reported).
  --test24    every 5.4 event type present (disturbance exempt until B6), the RTC
              lost-time and NTP-correction events, the retention deletion.
  --soak      no reset (one boot); the guard audit again from the events file
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

EVENT_TYPES = ["compressor", "heater", "humidifier", "interlock", "harvest", "guard", "crc", "timeout", "sensor",
               "fan", "settings", "boot", "sd", "i2c", "rtc", "ntp", "crossover", "strip", "exercise", "ineffective"]

results = []


def report(ok, what, detail=""):
    tag = "PASS" if ok is True else "FAIL" if ok is False else "NOTE"
    results.append(tag)
    print(f"  {tag:4}  {what}" + (f" - {detail}" if detail else ""))


def sensor_table():
    """(channel, serial, gain, offset) from the firmware's own table."""
    src = open(os.path.join(ROOT, "lib/core/src/wineador/sensor_table.cpp")).read()
    rows = re.findall(r"\{(\d+),\s*0x([0-9A-Fa-f]+),\s*\"\w+\",\s*\"(\w+)\",\s*([-\d.]+)f,\s*([-\d.]+)f\}", src)
    return [(int(c), int(s, 16), pos, float(g), float(o)) for c, s, pos, g, o in rows]


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
    want = [f"# wineador {kind}", "# schema 1"]
    for w in want:
        if w not in comments:
            report(False, f"{os.path.basename(path)} header", f"missing '{w}'")
            return
    prof = [c for c in comments if c.startswith("# profile ")]
    if not prof or not prof[0].startswith(f"# profile {profile}"):
        report(False, f"{os.path.basename(path)} header profile", f"{prof[0] if prof else 'none'}; expected {profile}")
        return
    if not any(c.startswith("# firmware ") for c in comments):
        report(False, f"{os.path.basename(path)} header", "no firmware line")
        return
    if kind == "readings":
        for ch, serial, pos, g, o in table:
            line = f"# ch{ch} serial 0x{serial:08X} {pos} rh_gain {g:.4f} rh_offset {o:.3f}"
            if line not in comments:
                report(False, f"{os.path.basename(path)} header", f"missing or wrong: '{line}'")
                return
    report(True, f"{os.path.basename(path)} header", prof[0][2:])


def check_readings(path, table):
    comments, header, rows = read_csv(path)
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
    return header, rows


def file_date_ok(path):
    """The file's date comes from the board's system clock. Real (2026 on), or - for a
    file written while the clock had lost time - the same year as its own name (5.4:
    rows go to the month the clock says). 1980 means the clock was never set."""
    y = dt.datetime.fromtimestamp(os.stat(path).st_mtime).year
    name = os.path.basename(path)
    named = int(name[:4]) if name[:4].isdigit() else None
    return y >= 2026 or (named is not None and y == named and y != 1980), y


def load_events(paths):
    events = []
    for p in sorted(paths):
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
    ap.add_argument("--profile", choices=["bench", "real"], required=True)
    ap.add_argument("--test24", action="store_true")
    ap.add_argument("--soak", action="store_true")
    a = ap.parse_args()

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
    report(bool(readings) and bool(events_files), "files as /logs/YYYY/YYYY-MM-readings.csv and -events.csv",
           f"{len(readings)} readings, {len(events_files)} events files")
    hourly = os.path.join(logs, "hourly.csv")   # (a part file hourly-p2.csv would only follow a 3.9 GB hourly.csv)
    report(os.path.isfile(hourly), "/logs/hourly.csv exists")

    for p in readings + events_files + ([hourly] if os.path.isfile(hourly) else []):
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

    all_rows, header = [], []
    for p in readings:
        comments, h, rows = read_csv(p)
        check_header(p, comments, "readings", a.profile, table)
        header, rows = check_readings(p, table)
        all_rows += rows
    for p in events_files:
        comments, h, rows = read_csv(p)
        check_header(p, comments, "events", a.profile, table)
        report(h == ["timestamp", "time_trusted", "uptime_ms", "type", "detail"], f"{os.path.basename(p)} columns")
    if os.path.isfile(hourly):
        comments, h, rows = read_csv(hourly)
        check_header(hourly, comments, "hourly", a.profile, table)
        bad = sum(1 for r in rows if len(r) != len(h))
        report(bad == 0, f"hourly.csv: {len(rows)} rows with {len(h)} columns", f"{bad} wrong" if bad else "")

    events = load_events(events_files)
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

    if a.soak:
        boots = [e for e in events if e["type"] == "boot"]
        report(len(boots) == 1, "no resets: one boot in the events file", f"{len(boots)} boots")
        breaches, starts, maxstops = audit(events, TIMERS[a.profile])
        report(not breaches, "no guard ever breached (min off, min on, max run, crossover, interlock side)",
               "; ".join(breaches[:5]) + (f" (+{len(breaches) - 5} more)" if len(breaches) > 5 else ""))
        report(None, "starts: " + ", ".join(f"{k} {v}" for k, v in starts.items()) +
               " | max-run stops: " + ", ".join(f"{k} {v}" for k, v in maxstops.items()))
        report(None, f"strips {sum(1 for e in events if e['type'] == 'strip' and 'START' in e['detail'])}, "
                     f"crossovers {sum(1 for e in events if e['type'] == 'crossover')}, "
                     f"humidify calls {sum(1 for e in events if e['type'] == 'humidify' and 'START' in e['detail'])}")
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
            for ch, *_ in table:
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
