#!/usr/bin/env python3
"""Makes the LCD's fonts and icons (spec 5.5) as 4-bit anti-aliased tables for lib/lcdgfx:

    lib/lcdgfx/src/lcdgfx/assets.cpp    (generated: never edit by hand)

Sources, both openly licensed (5.5: "Icons and fonts ... openly licensed sets, with the licence
noted in the source"); the licence texts are in lib/lcdgfx/LICENSES/:
  - Inter (variable), SIL Open Font Licence 1.1 - https://github.com/rsms/inter, taken from
    https://github.com/google/fonts/raw/main/ofl/inter/Inter%5Bopsz,wght%5D.ttf
  - Material Symbols Rounded (variable), Apache 2.0 - https://github.com/google/material-design-icons,
    variablefont/MaterialSymbolsRounded%5BFILL,GRAD,opsz,wght%5D.ttf

The two .ttf files are not kept in the repo (the icon font is 15 MB). Download them anywhere and
run (Pillow 10 or later):

    python3 tools/make_lcd_assets.py <Inter.ttf> <MaterialSymbolsRounded.ttf>

The output records each source file's SHA-256, so a rebuild can be checked against this one.
Only what the screens use is made; the firmware build itself needs neither Python nor the fonts.
"""
import hashlib
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "lib", "lcdgfx", "src", "lcdgfx", "assets.cpp")

ASCII = "".join(chr(c) for c in range(32, 127))
EXTRA = "°·–"          # degree, middle dot, en dash
DIGITS = "0123456789"

# name, weight, optical size, pixel size, characters
FONTS = [
    ("kFontHuge", 600, 32, 100, "0123456789.-"),
    ("kFontLarge", 600, 32, 40, "0123456789.-:% C°"),
    ("kFontMedium", 500, 24, 24, ASCII + EXTRA),
    ("kFontSmall", 400, 18, 18, ASCII + EXTRA),
]

# name, Material Symbols name, codepoint, box size
ICONS = [
    ("kIconCooling", "ac_unit", 0xEB3B, 48),
    ("kIconHeating", "local_fire_department", 0xEF55, 48),
    ("kIconHumidifying", "water_drop", 0xE798, 48),
    ("kIconDrying", "humidity_low", 0xF164, 48),
    ("kIconStandby", "mode_standby", 0xF037, 48),
    ("kIconStarting", "hourglass_top", 0xEA5B, 48),
    ("kIconUpdating", "system_update_alt", 0xE8D7, 48),
    ("kIconFault", "warning", 0xF083, 48),
    ("kIconWifi", "wifi", 0xE63E, 28),
    ("kIconWifiOff", "wifi_off", 0xE648, 28),
    ("kIconSettings", "settings", 0xE8B8, 24),
    ("kIconAlert", "warning", 0xF083, 22),
    ("kIconAdd", "add", 0xE145, 36),
    ("kIconRemove", "remove", 0xE15B, 36),
    ("kIconPageUp", "keyboard_arrow_up", 0xE316, 32),
    ("kIconPageDown", "keyboard_arrow_down", 0xE313, 32),
    ("kIconBack", "arrow_back", 0xE5C4, 24),
    ("kIconArrowUp", "arrow_upward", 0xE5D8, 150),     # BP2 test 29's landscape check only
]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def pack4(img):
    """8-bit greyscale -> 4 bits a pixel, two to a byte (high nibble first), each row padded."""
    w, h = img.size
    px = img.load()
    out = bytearray()
    for y in range(h):
        row = [min(15, (px[x, y] + 8) // 17) for x in range(w)]
        if w % 2:
            row.append(0)
        for i in range(0, len(row), 2):
            out.append((row[i] << 4) | row[i + 1])
    return out


def render_glyph(font, ch):
    """(image, x offset, y offset of the top from the baseline) - the glyph's ink only."""
    x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
    w, h = max(0, x1 - x0), max(0, y1 - y0)
    if w == 0 or h == 0:
        return None, 0, 0
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
    return img, x0, y0


def c_bytes(data):
    lines = []
    for i in range(0, len(data), 24):
        lines.append("  " + ",".join(str(b) for b in data[i:i + 24]) + ",")
    return "\n".join(lines) if lines else "  0,"


def make_font(path, name, weight, opsz, size, chars):
    font = ImageFont.truetype(path, size)
    font.set_variation_by_axes([opsz, weight])
    ascent, descent = font.getmetrics()
    # Tabular digits: every digit the width of the widest, so numbers don't jiggle.
    digit_adv = max(font.getlength(d) for d in DIGITS)
    bits = bytearray()
    glyphs = []
    for ch in sorted(set(chars), key=ord):
        img, xoff, yoff = render_glyph(font, ch)
        adv = font.getlength(ch)
        if ch in DIGITS:
            xoff += round((digit_adv - adv) / 2)
            adv = digit_adv
        off = len(bits)
        w = h = 0
        if img is not None:
            w, h = img.size
            bits += pack4(img)
        glyphs.append((ord(ch), off, w, h, xoff, yoff, round(adv)))
    src = [f"static const uint8_t {name}Bits[] = {{\n{c_bytes(bits)}\n}};",
           f"static const Glyph {name}Glyphs[] = {{"]
    for g in glyphs:
        src.append("  {%d, %d, %d, %d, %d, %d, %d}," % g)
    src.append("};")
    src.append(f"const Font {name} = {{{name}Glyphs, {name}Bits, {len(glyphs)}, {ascent + descent}, {ascent}, {descent}}};")
    return "\n".join(src), len(bits) + len(glyphs) * 12


def make_icon(path, name, cp, box):
    font = ImageFont.truetype(path, box)
    font.set_variation_by_axes([1, 0, min(48, max(20, box)), 500])     # FILL 1, GRAD 0, opsz, wght 500
    ch = chr(cp)
    img = Image.new("L", (box, box), 0)
    # Material Symbols draw in an em-sized box that sits on the baseline.
    ImageDraw.Draw(img).text((0, box), ch, font=font, fill=255, anchor="ls")
    data = pack4(img)
    return (f"static const uint8_t {name}Bits[] = {{\n{c_bytes(data)}\n}};\n"
            f"const Icon {name} = {{{box}, {box}, {name}Bits}};"), len(data)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    inter, symbols = sys.argv[1], sys.argv[2]
    parts = []
    total = 0
    for name, weight, opsz, size, chars in FONTS:
        s, n = make_font(inter, name, weight, opsz, size, chars)
        parts.append(f"// {name}: Inter, weight {weight}, {size} px, {len(set(chars))} characters, {n} bytes\n{s}")
        total += n
    for name, sym, cp, box in ICONS:
        s, n = make_icon(symbols, name, cp, box)
        parts.append(f"// {name}: Material Symbols Rounded '{sym}' (U+{cp:04X}), {box} x {box}, {n} bytes\n{s}")
        total += n
    head = f"""// GENERATED by tools/make_lcd_assets.py - do not edit. Rebuild with that script.
//
// Fonts: Inter (variable) - SIL Open Font Licence 1.1, Copyright 2020 The Inter Project Authors
//   (https://github.com/rsms/inter). Licence: lib/lcdgfx/LICENSES/OFL-Inter.txt
//   source {os.path.basename(inter)} sha256 {sha256(inter)}
// Icons: Material Symbols Rounded (variable) - Apache License 2.0, Copyright Google LLC
//   (https://github.com/google/material-design-icons). Licence: lib/lcdgfx/LICENSES/Apache-2.0-MaterialSymbols.txt
//   source {os.path.basename(symbols)} sha256 {sha256(symbols)}
//   Rendered (rasterised to 4-bit alpha), not modified in shape.
//
// About {total} bytes of tables in all. Each table is its own array, so a build links only
// those it uses (the BP2 test 29 arrow is in the sketch only).

#include "lcdgfx/assets.h"

namespace lcdgfx {{
"""
    with open(OUT, "w") as f:
        f.write(head + "\n\n".join(parts) + "\n\n}  // namespace lcdgfx\n")
    print(f"wrote {OUT}: about {total} bytes of tables")


if __name__ == "__main__":
    main()
