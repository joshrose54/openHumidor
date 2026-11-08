#!/usr/bin/env python3
"""Make the SD card nearly full for B3 test 24's retention check (spec 5.4).

The firmware's boot check: below 10 % free, delete the oldest month of readings
until above 20 % free. This script, run on the Mac with the card in the reader:
  1. writes two dummy OLD months of readings, /logs/2025/2025-09-readings.csv and
     2025-10-readings.csv, big enough that deleting both takes free space from
     ~8 % to just over 20 % (deleting only the first must NOT be enough), and
  2. writes a filler file /FILLER.bin so free space ends at ~8 %.
It works the sizes out from the card's real capacity and free space.

    python3 tools/fill_card.py /Volumes/WINEADOR            # show the plan only
    python3 tools/fill_card.py /Volumes/WINEADOR --write    # write the files
    python3 tools/fill_card.py /Volumes/WINEADOR --remove   # delete FILLER.bin (and any dummy months left)

Writing tens of GB takes a while (~10-40 min, depending on the reader); progress
is printed. Nothing else on the card is touched.
"""
import argparse
import os
import shutil
import sys

GB = 1024 ** 3
CHUNK = 64 * 1024 * 1024
HEADER = (b"# wineador readings\n# schema 1\n# firmware DUMMY (tools/fill_card.py, for B3 test 24)\n"
          b"# profile bench\ntimestamp,time_trusted\n")
DUMMIES = ["logs/2025/2025-09-readings.csv", "logs/2025/2025-10-readings.csv"]


def plan(card):
    du = shutil.disk_usage(card)
    total, free = du.total, du.free
    target_free = 0.08 * total                     # end at ~8 % free: below the 10 % trigger
    # both months together must lift free from 8 % to above 20 %; one alone must not
    need = 0.20 * total - target_free              # 12 % of the card
    month = int(need * 0.55)                       # 2 x 55 % of it = 110 %: both needed, and enough
    filler = int(free - target_free - 2 * month)
    return total, free, month, filler


def write_file(path, size):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    zeros = bytes(CHUNK)
    done = 0
    with open(path, "wb") as f:
        f.write(HEADER if path.endswith(".csv") else b"")
        done = f.tell()
        while done < size:
            n = min(CHUNK, size - done)
            f.write(zeros[:n])
            done += n
            if (done // CHUNK) % 16 == 0:
                print(f"    {os.path.basename(path)}: {done / GB:5.1f} / {size / GB:.1f} GB", flush=True)
    print(f"    {os.path.basename(path)}: done, {size / GB:.2f} GB")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("card")
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--remove", action="store_true")
    a = ap.parse_args()
    if not os.path.ismount(a.card):
        print(f"{a.card} is not a mounted volume. Is the card in the reader?")
        return 1
    if a.remove:
        for rel in ["FILLER.bin"] + DUMMIES:
            p = os.path.join(a.card, rel)
            if os.path.exists(p):
                os.remove(p)
                print(f"  removed {rel}")
        du = shutil.disk_usage(a.card)
        print(f"  free now {du.free / GB:.1f} GB of {du.total / GB:.1f} GB ({100 * du.free / du.total:.1f}%)")
        return 0
    total, free, month, filler = plan(a.card)
    print(f"\n  card {total / GB:.1f} GB, free {free / GB:.1f} GB ({100 * free / total:.1f}%)")
    if filler < 0:
        print("  Not enough free space for the plan (the card is already fuller than expected). Nothing written.")
        return 1
    after = free - 2 * month - filler
    print(f"  dummy old months: 2 x {month / GB:.2f} GB  ({DUMMIES[0]}, {DUMMIES[1]})")
    print(f"  filler:           {filler / GB:.2f} GB  (FILLER.bin)")
    print(f"  free afterwards:  {after / GB:.1f} GB ({100 * after / total:.1f}%) - below the 10% trigger")
    print(f"  the board should delete 2025-09 (free -> {100 * (after + month) / total:.1f}%, still under 20%),")
    print(f"  then 2025-10 (free -> {100 * (after + 2 * month) / total:.1f}%, above 20%), and stop.")
    if not a.write:
        print("\n  Plan only. Add --write to write the files.\n")
        return 0
    for rel in DUMMIES:
        write_file(os.path.join(a.card, rel), month)
    write_file(os.path.join(a.card, "FILLER.bin"), filler)
    du = shutil.disk_usage(a.card)
    print(f"\n  done: free {du.free / GB:.1f} GB ({100 * du.free / du.total:.1f}%). Eject the card before removing it.\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
