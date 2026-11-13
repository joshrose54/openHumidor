#!/usr/bin/env python3
"""Makes the LCD's logo bitmaps (spec 5.5: "The logo is converted to a bitmap at build time, as
the fonts and icons are") from the supplied artwork, used as it is:

    docs/brand/openhumidor-logo.svg  ->  lib/lcdgfx/src/lcdgfx/logo.cpp  (generated: never edit)

Run again after the SVG changes (Pillow 10 or later; nothing else):

    python3 tools/make_lcd_logo.py

The SVG's own geometry is rasterised - every path and circle as drawn, nothing redrawn - at
16 x 16 samples a pixel, then stored as 4-bit coverage, the same form as the fonts and icons, and
cropped to the ink. The tool understands exactly what this artwork uses (absolute M L H V C A Z,
circles, fill-rule evenodd or nonzero, one fill colour) and STOPS with an error on anything else
(a transform, a stroke, a relative command, ...), so a later edit to the SVG can never come out
wrong without anyone noticing. The output records the SVG's SHA-256.

Two sizes, as scales of the 302 x 84 artwork:
  kLogoSplash    the boot splash, x1.27 (its ink about 360 px wide: 3/4 of the 480 screen)
  kLogoWordmark  the walk-past wordmark, x0.45 (ink about 128 px wide, between the time and WiFi)
"""
import hashlib
import math
import os
import re
import sys
import xml.etree.ElementTree as ET

from PIL import Image, ImageChops, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SVG = os.path.join(ROOT, "docs", "brand", "openhumidor-logo.svg")
OUT = os.path.join(ROOT, "lib", "lcdgfx", "src", "lcdgfx", "logo.cpp")
NS = "{http://www.w3.org/2000/svg}"
SS = 16                      # samples per pixel, each way

# name, ink width in pixels (the scale follows from it)
SIZES = [
    ("kLogoSplash", 360),
    ("kLogoWordmark", 128),
]


class SvgError(Exception):
    pass


def tokens(d):
    for m in re.finditer(r"([A-Za-z])|(-?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)", d):
        yield m.group(1) or float(m.group(2))
    rest = re.sub(r"[A-Za-z]|-?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?|[\s,]", "", d)
    if rest:
        raise SvgError(f"path data not understood: {rest[:20]!r}")


def cubic(p0, p1, p2, p3, n=48):
    out = []
    for i in range(1, n + 1):
        t = i / n
        u = 1 - t
        out.append((u ** 3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t ** 3 * p3[0],
                    u ** 3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t ** 3 * p3[1]))
    return out


def arc(p0, rx, ry, phi_deg, large, sweep, p1, n=48):
    """SVG elliptical arc, endpoint form -> points (SVG 1.1 appendix F.6.5)."""
    if p0 == p1:
        return []
    rx, ry = abs(rx), abs(ry)
    if rx == 0 or ry == 0:
        return [p1]
    phi = math.radians(phi_deg)
    cp, sp = math.cos(phi), math.sin(phi)
    dx, dy = (p0[0] - p1[0]) / 2, (p0[1] - p1[1]) / 2
    x1, y1 = cp * dx + sp * dy, -sp * dx + cp * dy
    lam = (x1 / rx) ** 2 + (y1 / ry) ** 2
    if lam > 1:
        rx, ry = rx * math.sqrt(lam), ry * math.sqrt(lam)
    num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1
    den = rx * rx * y1 * y1 + ry * ry * x1 * x1
    k = math.sqrt(max(0.0, num / den))
    if large == sweep:
        k = -k
    cx1, cy1 = k * rx * y1 / ry, -k * ry * x1 / rx
    cx = cp * cx1 - sp * cy1 + (p0[0] + p1[0]) / 2
    cy = sp * cx1 + cp * cy1 + (p0[1] + p1[1]) / 2

    def ang(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a

    t1 = ang(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry)
    dt = ang((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry)
    if not sweep and dt > 0:
        dt -= 2 * math.pi
    elif sweep and dt < 0:
        dt += 2 * math.pi
    out = []
    for i in range(1, n + 1):
        t = t1 + dt * i / n
        out.append((cx + rx * math.cos(t) * cp - ry * math.sin(t) * sp,
                    cy + rx * math.cos(t) * sp + ry * math.sin(t) * cp))
    out[-1] = p1
    return out


def subpaths(d):
    """Absolute M L H V C A Z only. Returns a list of closed point lists."""
    toks = list(tokens(d))
    i = 0
    cur = None
    start = None
    sub = []
    subs = []
    cmd = None

    def num():
        nonlocal i
        if i >= len(toks) or isinstance(toks[i], str):
            raise SvgError(f"command {cmd} short of numbers")
        v = toks[i]
        i += 1
        return v

    while i < len(toks):
        if isinstance(toks[i], str):
            cmd = toks[i]
            i += 1
            if cmd not in "MLHVCAZ":
                raise SvgError(f"path command {cmd!r} not handled (only absolute M L H V C A Z)")
        elif cmd is None:
            raise SvgError("path data starts with a number")
        if cmd == "M":
            if sub:
                raise SvgError("a subpath not closed with Z")
            cur = (num(), num())
            start = cur
            sub = [cur]
            cmd = "L"             # numbers after M are line-tos
        elif cmd == "L":
            cur = (num(), num())
            sub.append(cur)
        elif cmd == "H":
            cur = (num(), cur[1])
            sub.append(cur)
        elif cmd == "V":
            cur = (cur[0], num())
            sub.append(cur)
        elif cmd == "C":
            p1, p2, p3 = (num(), num()), (num(), num()), (num(), num())
            sub += cubic(cur, p1, p2, p3)
            cur = p3
        elif cmd == "A":
            rx, ry, rot, large, sweep, x, y = num(), num(), num(), num(), num(), num(), num()
            sub += arc(cur, rx, ry, rot, int(large), int(sweep), (x, y))
            cur = (x, y)
        elif cmd == "Z":
            if len(sub) < 3:
                raise SvgError("a subpath with under 3 points")
            subs.append(sub)
            sub = []
            cur = start
            cmd = None
    if sub:
        raise SvgError("the last subpath is not closed with Z")
    return subs


def bbox(pts):
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    return min(xs), min(ys), max(xs), max(ys)


def shapes(root):
    """[(fill rule, [subpaths])] for every filled shape, in document order."""
    allowed_root = {"xmlns", "viewBox", "width", "height", "fill"}
    for k in root.attrib:
        if k not in allowed_root:
            raise SvgError(f"<svg> attribute {k!r} not handled")
    out = []
    for el in root.iter():
        tag = el.tag.replace(NS, "")
        if el is root:
            continue
        for k in el.attrib:
            if k not in ("d", "fill-rule", "cx", "cy", "r"):
                raise SvgError(f"<{tag}> attribute {k!r} not handled (no transforms, strokes or own fills)")
        if tag == "path":
            out.append((el.get("fill-rule", "nonzero"), subpaths(el.get("d"))))
        elif tag == "circle":
            cx, cy, r = float(el.get("cx")), float(el.get("cy")), float(el.get("r"))
            pts = [(cx + r * math.cos(2 * math.pi * i / 256), cy + r * math.sin(2 * math.pi * i / 256)) for i in range(256)]
            out.append(("nonzero", [pts]))
        else:
            raise SvgError(f"element <{tag}> not handled")
    return out


def check_nonzero(rule, subs):
    """A nonzero path is filled as the union of its subpaths: right only when they don't overlap
    (as here: the i's dot and stem). Stop if they might."""
    if rule != "nonzero" or len(subs) < 2:
        return
    boxes = [bbox(s) for s in subs]
    for a in range(len(boxes)):
        for b in range(a + 1, len(boxes)):
            A, B = boxes[a], boxes[b]
            if A[0] < B[2] and B[0] < A[2] and A[1] < B[3] and B[1] < A[3]:
                raise SvgError("a nonzero path whose subpaths overlap: not handled")


def render(shape_list, scale, vw, vh):
    """Coverage 0..255 at this scale, the whole view box."""
    W, H = math.ceil(vw * scale), math.ceil(vh * scale)
    big = Image.new("L", (W * SS, H * SS), 0)
    k = scale * SS
    for rule, subs in shape_list:
        check_nonzero(rule, subs)
        shape = Image.new("L", big.size, 0)
        for s in subs:
            one = Image.new("L", big.size, 0)
            ImageDraw.Draw(one).polygon([(x * k, y * k) for x, y in s], fill=255)
            # evenodd: each subpath flips what it covers (the holes of o, p, e, d); nonzero: union
            shape = ImageChops.logical_xor(shape.convert("1"), one.convert("1")).convert("L") if rule == "evenodd" \
                else ImageChops.lighter(shape, one)
        big = ImageChops.lighter(big, shape)
    return big.reduce(SS)


def pack4(img):
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


def c_bytes(data):
    return "\n".join("  " + ",".join(str(b) for b in data[i:i + 24]) + "," for i in range(0, len(data), 24))


def main():
    with open(SVG, "rb") as f:
        raw = f.read()
    root = ET.fromstring(raw)
    if root.get("fill", "").upper() not in ("#FFFFFF", "#FFF", "WHITE"):
        raise SvgError("expected one fill colour for the whole artwork")
    vb = [float(v) for v in root.get("viewBox").split()]
    if vb[0] != 0 or vb[1] != 0:
        raise SvgError("viewBox must start at 0 0")
    vw, vh = vb[2], vb[3]
    shape_list = shapes(root)
    pts = [p for _, subs in shape_list for s in subs for p in s]
    x0, y0, x1, y1 = bbox(pts)
    ink_w = x1 - x0
    parts, total, notes = [], 0, []
    for name, want_w in SIZES:
        scale = want_w / ink_w
        img = render(shape_list, scale, vw, vh)
        box = img.getbbox()
        img = img.crop(box)
        data = pack4(img)
        total += len(data)
        w, h = img.size
        notes.append(f"{name}: x{scale:.3f} of the {vw:g} x {vh:g} artwork, {w} x {h} px, {len(data)} bytes")
        parts.append(f"// {notes[-1]}\nstatic const uint8_t {name}Bits[] = {{\n{c_bytes(data)}\n}};\n"
                     f"const Bitmap {name} = {{{w}, {h}, {name}Bits}};")
    head = f"""// GENERATED by tools/make_lcd_logo.py - do not edit. Rebuild with that script.
//
// The openHumidor logo (spec 5.5), rasterised from docs/brand/openhumidor-logo.svg as supplied
// (its paths and circle as drawn, 16 x 16 samples a pixel, 4-bit coverage, cropped to the ink).
//   source openhumidor-logo.svg sha256 {hashlib.sha256(raw).hexdigest()}
//
// {total} bytes of tables in all. Each is its own array, so a build links only those it uses.

#include "lcdgfx/assets.h"

namespace lcdgfx {{
"""
    with open(OUT, "w") as f:
        f.write(head + "\n\n".join(parts) + "\n\n}  // namespace lcdgfx\n")
    for n in notes:
        print(n)
    print(f"wrote {OUT}: {total} bytes of tables")


if __name__ == "__main__":
    try:
        main()
    except SvgError as e:
        print(f"make_lcd_logo: {e}", file=sys.stderr)
        sys.exit(1)
