#!/usr/bin/env python3
"""Keep the Arduino IDE sketch in step with include/ and src/.

    python3 scripts/sync_arduino.py           # rewrite the sketch
    python3 scripts/sync_arduino.py --check    # fail if it is out of date

arduino/u1_spool_bridge/ is a FLATTENED COPY of include/*.h and src/*.cpp —
the Arduino IDE wants every file in the sketch folder with no subdirectories,
and the sources already include each other by bare filename, so nothing needs
rewriting on the way across. Only the .ino and the README are native to it.

It was maintained by hand, and it drifted: it sat at 1.15.3 while the project
reached 1.17.1, so anyone building through the IDE got a version with none of
the stock-firmware support, none of the OTA stall recovery and none of the
board-mismatch guard — and nothing said so. Copying by hand is the bug; this
is the fix, and --check is what stops it happening again.
"""

import filecmp
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SKETCH = os.path.join(ROOT, "arduino", "u1_spool_bridge")

# Native to the sketch, never generated, never deleted.
KEEP = {"u1_spool_bridge.ino", "ARDUINO-README.txt"}


def sources():
    out = {}
    for sub, ext in (("include", ".h"), ("src", ".cpp")):
        d = os.path.join(ROOT, sub)
        for name in sorted(os.listdir(d)):
            if name.endswith(ext):
                out[name] = os.path.join(d, name)
    return out


def main():
    check = "--check" in sys.argv
    if not os.path.isdir(SKETCH):
        sys.exit("no sketch directory at %s" % SKETCH)

    src = sources()
    have = {n for n in os.listdir(SKETCH) if n not in KEEP}

    stale   = [n for n, p in src.items()
               if n not in have or not filecmp.cmp(p, os.path.join(SKETCH, n), shallow=False)]
    orphans = sorted(have - set(src))

    if check:
        if stale or orphans:
            print("The Arduino sketch is out of date.")
            for n in sorted(stale):
                print("  differs or missing: %s" % n)
            for n in orphans:
                print("  no longer in the project: %s" % n)
            print("\nRun: python3 scripts/sync_arduino.py")
            return 1
        print("arduino sketch: in step with include/ and src/ (%d files)" % len(src))
        return 0

    for n in sorted(stale):
        shutil.copy2(src[n], os.path.join(SKETCH, n))
        print("  updated %s" % n)
    for n in orphans:
        os.remove(os.path.join(SKETCH, n))
        print("  removed %s" % n)
    if not stale and not orphans:
        print("already in step; nothing to do")
    else:
        print("\n%d updated, %d removed — sketch now matches include/ and src/"
              % (len(stale), len(orphans)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
