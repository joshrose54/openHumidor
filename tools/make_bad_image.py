#!/usr/bin/env python3
"""Made-up firmware images from a real Rev Q cabinet build, for the OTA refusals (B7 test 6, and
the native tests test_image_check / test_ota_rules).

Corrupt (the default): ONE byte changed inside the program data, well after the identity block,
so the image still passes the identity check (a Rev Q cabinet build, sent, written) but fails its
own check at the end (the checksum and the SHA-256 the image carries): the board refuses it with
corrupt_image, the new slot is never set to boot, nothing changes.

    python3 tools/make_bad_image.py                       # .pio.nosync/images/revq.bin -> revq-corrupt.bin
    python3 tools/make_bad_image.py IN.bin OUT.bin

ESP32 chip (--esp32-chip): the header's chip ID set to the ESP32's (0), as a Rev N image carries,
everything else untouched: refused as a Rev N image (rev_n_image) by its chip ID. Rev N is retired
and can no longer be built; B7 test 6 uploads the last real one (~/wineador-logs/rev-n-last/), the
native tests use this.

    python3 tools/make_bad_image.py --esp32-chip          # .pio.nosync/images/revq.bin -> revq-esp32-chip.bin
    python3 tools/make_bad_image.py --esp32-chip IN.bin OUT.bin

It refuses to write over its input, and checks the result.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ID_OFFSET = 0x120
MAGIC = b"openHumidor-ID\0"


def segments(b):
    """(data offset, length) of each segment, and the checksum byte's offset."""
    out, at = [], 24
    for _ in range(b[1]):
        n = int.from_bytes(b[at + 4:at + 8], "little")
        out.append((at + 8, n))
        at += 8 + n
    return out, at | 15


def checksum_ok(b):
    segs, at = segments(b)
    s = 0xEF
    for off, n in segs:
        for x in b[off:off + n]:
            s ^= x
    return b[at] == s


CHIP_ID_OFFSET = 12     # the image header's chip ID (2 bytes, little-endian)
CHIP_ESP32 = 0
CHIP_ESP32S3 = 9


def make(src, dst, esp32_chip):
    if os.path.abspath(src) == os.path.abspath(dst):
        sys.exit("make_bad_image: the output must not be the input")
    b = bytearray(open(src, "rb").read())
    if b[ID_OFFSET:ID_OFFSET + len(MAGIC)] != MAGIC or b[ID_OFFSET + 16:ID_OFFSET + 21] != b"rev-q":
        sys.exit("make_bad_image: %s isn't a Rev Q build with an identity block (build revq first)" % src)
    if not checksum_ok(b):
        sys.exit("make_bad_image: %s already fails its checksum" % src)
    if esp32_chip:
        if int.from_bytes(b[CHIP_ID_OFFSET:CHIP_ID_OFFSET + 2], "little") != CHIP_ESP32S3:
            sys.exit("make_bad_image: %s isn't stamped for the ESP32-S3" % src)
        b[CHIP_ID_OFFSET:CHIP_ID_OFFSET + 2] = CHIP_ESP32.to_bytes(2, "little")
        open(dst, "wb").write(b)
        return "wrote %s: chip ID set to the ESP32's (a Rev N image's); nothing else changed" % dst
    segs, _ = segments(b)
    # the middle of the largest segment: program data, far from the header and the identity block
    off, n = max(segs, key=lambda s: s[1])
    at = off + n // 2
    if at < ID_OFFSET + 64:
        sys.exit("make_bad_image: unexpected layout")
    b[at] ^= 0x01
    assert b[ID_OFFSET:ID_OFFSET + len(MAGIC)] == MAGIC and not checksum_ok(b)
    open(dst, "wb").write(b)
    return "wrote %s: byte 0x%X changed (identity block intact, checksum and SHA-256 now wrong)" % (dst, at)


def main():
    args = sys.argv[1:]
    esp32_chip = "--esp32-chip" in args
    args = [a for a in args if a != "--esp32-chip"]
    src = args[0] if args else os.path.join(ROOT, ".pio.nosync", "images", "revq.bin")
    dst = args[1] if len(args) > 1 else os.path.join(os.path.dirname(src),
                                                     "revq-esp32-chip.bin" if esp32_chip else "revq-corrupt.bin")
    print(make(src, dst, esp32_chip))


if __name__ == "__main__":
    main()
