#!/usr/bin/env python3
"""Drive check_partitions.py down its PlatformIO path with a stub environment.

`check_partitions.py --selftest` covers the decision logic. This covers the
glue around it — `Import("env")`, table resolution, `env.Exit()` — which only
a real build would otherwise exercise. Since the whole point of the guard is
to fail a build, a bug in that glue is invisible until the day it matters.

    python3 scripts/test_check_partitions.py

Needs the pinned platform and framework installed (i.e. one successful build,
or `pio pkg install`). Skips cleanly if they are not there.
"""

import contextlib
import glob
import io
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "check_partitions.py")
PROJECT = os.path.dirname(HERE)


def _first(pattern):
    hits = sorted(glob.glob(os.path.expanduser(pattern)))
    return hits[0] if hits else None


BOARDS = _first("~/.platformio/platforms/espressif32*/boards")
PKG = _first("~/.platformio/packages/framework-arduinoespressif32")


class Exited(Exception):
    pass


class _Board:
    def __init__(self, board_id, table):
        with open(os.path.join(BOARDS, board_id + ".json")) as fh:
            self._d = json.load(fh)
        self._table = table

    def get(self, key, default=None):
        return {
            "build.partitions": self._table,
            "upload.maximum_size": self._d["upload"]["maximum_size"],
            "name": self._d["name"],
        }.get(key, default)


class _Platform:
    def get_package_dir(self, _name):
        return PKG


class _Env:
    """The slice of PlatformIO's env that check_partitions.py actually uses."""

    def __init__(self, board_id, table, options=None):
        self._board = _Board(board_id, table)
        self._options = options or {}

    def BoardConfig(self):
        return self._board

    def PioPlatform(self):
        return _Platform()

    def subst(self, value):
        return PROJECT if value == "$PROJECT_DIR" else value

    def GetProjectOption(self, key, default=""):
        return self._options.get(key, default)

    def Exit(self, code):
        raise Exited(code)


def run(board_id, table, options=None):
    """-> (exit_code, stdout). 0 means the build would have been allowed."""
    env = _Env(board_id, table, options)
    globs = {"__name__": "check_partitions", "__file__": SCRIPT,
             "Import": lambda *_a: globs.update(env=env)}
    out, code = io.StringIO(), 0
    try:
        with contextlib.redirect_stdout(out):
            with open(SCRIPT) as fh:
                exec(compile(fh.read(), SCRIPT, "exec"), globs)
    except Exited as exc:
        code = exc.args[0]
    return code, out.getvalue()


# (board id, table, project options, expected exit, must contain, must not contain)
CASES = [
    ("esp32-c5-devkitc-1",     "min_spiffs.csv",  None,
     0, ["ok —"],                 ["WARNING", "FAILED"]),
    ("esp32-c5-devkitc1-n4",   "min_spiffs.csv",  None,
     0, ["ok —", "4.00 MB"],      ["WARNING", "FAILED"]),
    ("esp32-c5-devkitc-1",     "default_8MB.csv", None,
     1, ["FAILED", "8.00 MB"],    ["ok —"]),
    ("esp32-c5-devkitc-1",     "huge_app.csv",    None,
     1, ["FAILED", "app partition"], ["ok —"]),
    ("esp32-c5-devkitc-1",     "huge_app.csv",    {"custom_allow_single_ota": "yes"},
     0, ["ok —"],                 ["FAILED"]),
    ("esp32-c5-devkitc1-n8r4", "min_spiffs.csv",  None,
     0, ["WARNING", "ok —"],      ["FAILED"]),
    ("seeed_xiao_esp32c5",     "default_8MB.csv", None,
     0, ["ok —", "8.00 MB"],      ["WARNING", "FAILED"]),
    ("seeed_xiao_esp32c3",     "min_spiffs.csv",  None,
     0, ["ok —"],                 ["WARNING", "FAILED"]),
    ("esp32-c5-devkitc-1",     "nonexistent.csv", None,
     0, ["skipping"],             ["FAILED"]),
]


def main():
    if not BOARDS or not PKG:
        print("platform packages not installed — nothing to test")
        return 0

    failures = 0
    for board_id, table, options, want_code, must, must_not in CASES:
        if not os.path.isfile(os.path.join(BOARDS, board_id + ".json")):
            print("  SKIP %-24s %-18s (board id not in this platform)"
                  % (board_id, table))
            continue
        code, out = run(board_id, table, options)
        ok = (code == want_code
              and all(m in out for m in must)
              and not any(m in out for m in must_not))
        failures += not ok
        print("  %-4s %-24s %-18s%-5s exit=%s"
              % ("ok" if ok else "FAIL", board_id, table,
                 " +opt" if options else "", code))
        if not ok:
            print("       wanted exit=%s, containing %s, not containing %s"
                  % (want_code, must, must_not))
            print("       ---- actual output ----")
            print("       " + out.replace("\n", "\n       "))

    print("\nPlatformIO path: %s"
          % ("ALL PASS" if not failures else "%d FAILED" % failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
