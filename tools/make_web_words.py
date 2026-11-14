#!/usr/bin/env python3
"""Writes web/words.js from the words in lib/core/src/wineador/words.cpp and the sensor table.

The dashboard shows a word, never a code (spec 5.5 "Words on screen"): the sensor positions
("Middle rear"), the guards ("rest"), the exclusion and restart reasons, the ion generator's and
the blowers' why, the event types. Those words are defined once, in the firmware; the page reads
this generated file rather than typing them itself, so the LCD and the dashboard can't disagree.
The native test test_web_words fails if the file is out of date; this tool rewrites it (by running
that test with WINEADOR_WRITE_WEB_WORDS=1, so the file is made by the firmware's own code).
Run tools/make_web_assets.py afterwards to pack it into the page.

    python3 tools/make_web_words.py
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIO = os.path.expanduser("~/.platformio/penv/bin/pio")

env = dict(os.environ, WINEADOR_WRITE_WEB_WORDS="1")
r = subprocess.run([PIO, "test", "-e", "native", "-f", "test_web_words"], cwd=ROOT, env=env)
if r.returncode != 0:
    sys.exit("make_web_words: the test failed; web/words.js may not be written")
print("wrote web/words.js")
