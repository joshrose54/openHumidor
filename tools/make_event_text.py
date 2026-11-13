#!/usr/bin/env python3
"""Writes tools/event_text.json from the event wording in lib/core/src/wineador/event_text.h.

The wording of the events is defined once, in that header. tools/check_logs.py reads this JSON
file rather than typing the words itself, so the two can't drift. The native test
test_event_text fails if the file is out of date; this tool rewrites it (by running that test
with WINEADOR_WRITE_EVENT_TEXT=1, so the file is made by the firmware's own code).

    python3 tools/make_event_text.py
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIO = os.path.expanduser("~/.platformio/penv/bin/pio")

env = dict(os.environ, WINEADOR_WRITE_EVENT_TEXT="1")
r = subprocess.run([PIO, "test", "-e", "native", "-f", "test_event_text"], cwd=ROOT, env=env)
if r.returncode != 0:
    sys.exit("make_event_text: the test failed; tools/event_text.json may not be written")
print("wrote tools/event_text.json")
