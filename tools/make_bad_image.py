#!/usr/bin/env python3
"""B7 test 6 (spec): a corrupt firmware image, for the OTA "image check failed" case.

Takes a real cabinet build and changes ONE byte inside its program data, well after the identity
block, so the image still passes the identity check (it is a Rev Q cabinet build, sent, written)
but fails its own check at the end (the checksum and the SHA-256 the image carries): the board
refuses it with corrupt_image, the new slot is never set to boot, nothing changes.

    python3 tools/make_bad_image.py                       # .pio.nosync/images/revq.bin -> .pio.nosync/images/revq-corrupt.bin
    python3 tools/make_bad_image.py IN.bin OUT.bin

It refuses to write over its input, and checks the result: identity intact, checksum wrong.
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


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, ".pio.nosync", "images", "revq.bin")
    dst = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(src), "revq-corrupt.bin")
    if os.path.abspath(src) == os.path.abspath(dst):
        sys.exit("make_bad_image: the output must not be the input")
    b = bytearray(open(src, "rb").read())
    if b[ID_OFFSET:ID_OFFSET + len(MAGIC)] != MAGIC or b[ID_OFFSET + 16:ID_OFFSET + 21] != b"rev-q":
        sys.exit("make_bad_image: %s isn't a Rev Q build with an identity block (build revq first)" % src)
    if not checksum_ok(b):
        sys.exit("make_bad_image: %s already fails its checksum" % src)
    segs, _ = segments(b)
    # the middle of the largest segment: program data, far from the header and the identity block
    off, n = max(segs, key=lambda s: s[1])
    at = off + n // 2
    if at < ID_OFFSET + 64:
        sys.exit("make_bad_image: unexpected layout")
    b[at] ^= 0x01
    assert b[ID_OFFSET:ID_OFFSET + len(MAGIC)] == MAGIC and not checksum_ok(b)
    open(dst, "wb").write(b)
    print("wrote %s: byte 0x%X changed (identity block intact, checksum and SHA-256 now wrong)" % (dst, at))


if __name__ == "__main__":
    main()
