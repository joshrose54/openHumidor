# PlatformIO post-build step (extra_scripts, every board build): checks the image just built carries
# the right openHumidor identity block at byte 0x120 (spec 5.5; lib/core image_identity.h), and keeps
# a copy of the image in .pio.nosync/images/<env>.bin for test_image_check (PlatformIO clears the
# build folders when another set of environments runs, so the tests read the copies).
#
#   Rev Q firmware builds: the app description (magic 0xABCD5432) at 0x20 and the identity block at
#     0x120: magic, board rev-q, the build's profile and test flag, layout 1, the API version, and the
#     version string spec B3 gives (2.0.0, -BENCH, -TEST, -ROLLBACK-TEST).
#   Rev N builds and the BP2 sketches: no identity block (Rev N is refused by its chip ID; a sketch
#     must never be accepted).
# Anything else fails the build.
#
# Also runnable by hand:  python3 tools/check_identity.py <firmware.bin> <env> "<build flags>"

import os
import re
import shutil
import sys

ID_OFFSET = 0x120
APP_DESC_OFFSET = 0x20
MAGIC = b"openHumidor-ID"


def text(b):
    return b.split(b"\0", 1)[0].decode("ascii", "replace")


def project_dir(here):
    return os.path.dirname(os.path.dirname(os.path.abspath(here)))


def expected(env, flags, root):
    """What the block must say for this build, or None if it must carry none."""
    if env.startswith("bp2_") or "WINEADOR_BOARD_REVQ" not in flags:
        return None
    board_h = open(os.path.join(root, "include", "board.h"), encoding="utf-8").read()
    revq = board_h[board_h.index("namespace board_revq"):]
    base = re.search(r'kFirmwareVersion\s*=\s*"([^"]+)"', revq).group(1)
    api = int(re.search(r"kApiVersion\s*=\s*(\d+)", open(os.path.join(root, "lib/core/src/wineador/web_api.h")).read()).group(1))
    bench = "WINEADOR_BENCH_PROFILE" in flags
    test = "WINEADOR_TEST_BUILD" in flags
    rollback = "WINEADOR_ROLLBACK_TEST" in flags
    version = base + ("-BENCH" if bench else "") + ("-TEST" if test else "") + ("-ROLLBACK-TEST" if rollback else "")
    return {"board": "rev-q", "profile": "bench" if bench else "real", "test": 1 if test else 0, "layout": 1,
            "api": api, "version": version}


def check(path, env, flags, root):
    b = open(path, "rb").read()
    problems = []
    if len(b) < ID_OFFSET + 64 or b[0] != 0xE9:
        return ["not an ESP32 image"]
    desc_magic = int.from_bytes(b[APP_DESC_OFFSET:APP_DESC_OFFSET + 4], "little")
    if desc_magic != 0xABCD5432:
        problems.append("no app description at 0x20")
    blk = b[ID_OFFSET:ID_OFFSET + 64]
    has = blk[:15] == MAGIC + b"\0"
    want = expected(env, flags, root)
    if want is None:
        if MAGIC in b[:4096]:
            problems.append("carries an identity block, but this build must not")
        return problems
    if not has:
        problems.append("no identity block at 0x120 (found %r)" % blk[:16])
        return problems
    got = {"board": text(blk[16:24]), "profile": text(blk[24:32]), "test": blk[32], "layout": blk[33],
           "api": blk[34] | blk[35] << 8, "version": text(blk[36:64])}
    for k, v in want.items():
        if got[k] != v:
            problems.append("%s is %r, should be %r" % (k, got[k], v))
    return problems


def keep(path, env, root):
    d = os.path.join(root, ".pio.nosync", "images")
    os.makedirs(d, exist_ok=True)
    shutil.copyfile(path, os.path.join(d, env + ".bin"))


def report(path, env, flags, root):
    problems = check(path, env, flags, root)
    if problems:
        print("IDENTITY CHECK FAILED for %s (%s):" % (env, path))
        for p in problems:
            print("  - " + p)
        return False
    want = expected(env, flags, root)
    print("Identity check: %s %s" % (env, ("block at 0x120: " + want["version"]) if want else "no block (as it must be)"))
    keep(path, env, root)
    return True


if __name__ == "__main__" and len(sys.argv) >= 3 and not sys.argv[0].endswith("SConstruct"):
    root = project_dir(__file__)
    ok = report(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else "", root)
    sys.exit(0 if ok else 1)

try:
    Import("env")  # noqa: F821  (PlatformIO / SCons)

    def _after_bin(source, target, env):  # noqa: ARG001
        root = env.subst("$PROJECT_DIR")
        bin_path = env.subst("$BUILD_DIR/${PROGNAME}.bin")
        flags = " ".join(str(f) for f in env.get("CPPDEFINES", [])) + " " + env.subst("$BUILD_FLAGS")
        if not report(bin_path, env.subst("$PIOENV"), flags, root):
            env.Exit(1)

    # "buildprog" runs on every build, even when the image was already up to date
    env.AddPostAction("buildprog", _after_bin)  # noqa: F821
except NameError:
    pass
