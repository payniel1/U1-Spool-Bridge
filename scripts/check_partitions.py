"""Pre-build guard: does the partition table actually fit this board's flash?

PlatformIO will happily build an 8 MB partition table against a 4 MB board. The
compile succeeds, the image is produced, and you find out on the bench when the
device will not boot — or worse, when an OTA writes off the end of flash months
later. Nothing in the toolchain checks this, so we do.

It also insists on two OTA slots. This firmware's whole fleet-update story
depends on writing to an inactive app partition and switching over only once the
image verifies; a single-slot table (huge_app.csv, say) silently removes that and
leaves a box that can only be updated over USB. That is a real capability to lose
by accident, so losing it has to be deliberate: set

    custom_allow_single_ota = yes

in the environment if you genuinely want a no-OTA build.

The third case is a warning rather than an error: a table far *smaller* than the
board. `esp32-c5-devkitc-1` sets min_spiffs because that devkit is a 4 MB part,
but Espressif sells the same board as N8R4 and N16R4 with 8 and 16 MB. Point the
env at one of those and everything works — it just quietly halves the OTA slot
and strands most of the flash. Worth saying out loud; not worth failing over.

Runs standalone too:

    python3 scripts/check_partitions.py --selftest
"""

import os
import sys

MB = 1048576.0


def parse(path):
    """-> (span_bytes, [app partition names])"""
    span, apps = 0, []
    with open(path) as fh:
        for line in fh:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            cols = [c.strip() for c in line.split(",")]
            if len(cols) < 5:
                continue
            name, ptype, offset, size = cols[0], cols[1], cols[3], cols[4]
            try:
                off = int(offset, 0)
                sz = int(size, 0)
            except ValueError:
                continue          # a blank offset means "pack after the last one"
            span = max(span, off + sz)
            if ptype == "app":
                apps.append(name)
    return span, apps


def suggest(flash):
    """The stock table that best uses a board of this size."""
    if flash >= 16 * MB:
        return "default_16MB.csv"
    if flash >= 8 * MB:
        return "default_8MB.csv"
    return "min_spiffs.csv"


def evaluate(table, span, apps, flash, board, allow_single=False):
    """Pure decision function. -> list of (level, message).

    level is "fail" or "warn". An empty list means the table is a good fit.
    Kept free of PlatformIO so --selftest can exercise every board x table
    combination without a toolchain.
    """
    out = []

    if flash and span > flash:
        out.append(("fail",
            "%s spans %.2f MB, but %s has only %.2f MB of flash.\n\n"
            "This WOULD HAVE BUILT and then failed on the device. Pick a table\n"
            "that fits:\n"
            "    16 MB board ->  default_16MB.csv\n"
            "     8 MB board ->  default_8MB.csv\n"
            "     4 MB board ->  min_spiffs.csv   (two 1.875 MB OTA slots)\n"
            % (table, span / MB, board, flash / MB)))

    if len(apps) < 2 and not allow_single:
        out.append(("fail",
            "%s has %d app partition(s); over-the-air updates need two.\n\n"
            "With one slot there is nowhere to write an update to, so this box\n"
            "could only ever be reflashed over USB and 'Update all boxes' would\n"
            "not reach it. min_spiffs.csv gives two 1.875 MB slots on a 4 MB\n"
            "board.\n\n"
            "If you really want a single-slot build, set\n"
            "    custom_allow_single_ota = yes\n"
            "in this environment.\n" % (table, len(apps))))

    # Only worth saying when the table fits — an overrun is already fatal above.
    if flash and span <= flash and span * 2 <= flash:
        out.append(("warn",
            "%s uses %.2f MB of the %.2f MB on this board.\n"
            "  (%s)\n"
            "  Nothing breaks, but the OTA slots are smaller than they need to be\n"
            "  and the rest of the flash is unreachable. %s would use the whole\n"
            "  part -- set board_build.partitions in this environment to switch."
            % (table, span / MB, flash / MB, board, suggest(flash))))

    return out


# ---------------------------------------------------------------- PlatformIO

def _run_pio(env):
    board = env.BoardConfig()

    def find_csv(name):
        """Resolve a partitions setting the way PlatformIO does: a path relative
        to the project first, then the framework's stock tables."""
        if not name:
            return None
        if os.path.isabs(name) and os.path.isfile(name):
            return name
        local = os.path.join(env.subst("$PROJECT_DIR"), name)
        if os.path.isfile(local):
            return local
        stock = os.path.join(
            env.PioPlatform().get_package_dir("framework-arduinoespressif32") or "",
            "tools", "partitions", name,
        )
        return stock if os.path.isfile(stock) else None

    name = board.get("build.partitions",
                     env.GetProjectOption("board_build.partitions", ""))
    csv = find_csv(name)
    if not csv:
        # Nothing to check against — don't block the build over it.
        print("check_partitions: no table found for %r, skipping" % name)
        return

    span, apps = parse(csv)
    flash = int(board.get("upload.maximum_size", 0))
    findings = evaluate(
        name, span, apps, flash, board.get("name", "this board"),
        allow_single=bool(env.GetProjectOption("custom_allow_single_ota", "")),
    )

    for level, msg in findings:
        if level == "warn":
            print("\ncheck_partitions: WARNING\n  " + msg.rstrip() + "\n")

    fails = [m for lvl, m in findings if lvl == "fail"]
    if fails:
        print("\n" + "=" * 72)
        print("PARTITION TABLE CHECK FAILED")
        print("=" * 72)
        print("\n".join(m.rstrip() for m in fails))
        print("=" * 72 + "\n")
        env.Exit(1)
        return

    print("check_partitions: %s ok — %.2f MB of %.2f MB flash, %d OTA slots"
          % (name, span / MB, flash / MB, len(apps)))


# ------------------------------------------------------------------ selftest

def _selftest():
    """Exercise evaluate() over the board/table combinations that matter.

    Reads the real board JSONs and the real stock CSVs, so it fails if either
    the platform or a table changes underneath us.
    """
    import glob
    import json

    roots = sorted(glob.glob(os.path.expanduser(
        "~/.platformio/platforms/espressif32*/boards")))
    parts = sorted(glob.glob(os.path.expanduser(
        "~/.platformio/packages/framework-arduinoespressif32/tools/partitions")))
    if not roots or not parts:
        print("selftest: platform packages not installed, nothing to check")
        return 0
    boards_dir, parts_dir = roots[0], parts[0]

    def board(bid):
        with open(os.path.join(boards_dir, bid + ".json")) as fh:
            d = json.load(fh)
        return d["name"], int(d["upload"]["maximum_size"])

    def table(t):
        return parse(os.path.join(parts_dir, t))

    # (board id, table, expected levels in order)
    cases = [
        ("seeed_xiao_esp32c5",      "default_8MB.csv",  []),
        ("seeed_xiao_esp32c5",      "min_spiffs.csv",   ["warn"]),
        ("seeed_xiao_esp32c3",      "min_spiffs.csv",   []),
        ("seeed_xiao_esp32c3",      "default_8MB.csv",  ["fail"]),
        ("esp32-c5-devkitc-1",      "min_spiffs.csv",   []),
        ("esp32-c5-devkitc-1",      "default_8MB.csv",  ["fail"]),
        ("esp32-c5-devkitc-1",      "huge_app.csv",     ["fail"]),
        ("esp32-c5-devkitc1-n4",    "min_spiffs.csv",   []),
        ("esp32-c5-devkitc1-n4",    "default_8MB.csv",  ["fail"]),
        ("esp32-c5-devkitc1-n8r4",  "min_spiffs.csv",   ["warn"]),
        ("esp32-c5-devkitc1-n8r4",  "default_8MB.csv",  []),
        ("esp32-c5-devkitc1-n16r4", "min_spiffs.csv",   ["warn"]),
        ("esp32-c5-devkitc1-n16r4", "default_16MB.csv", []),
    ]

    bad = 0
    for bid, tbl, want in cases:
        try:
            bname, flash = board(bid)
        except (IOError, OSError):
            print("  SKIP %-26s %-18s (board id not in this platform)" % (bid, tbl))
            continue
        span, apps = table(tbl)
        got = [lvl for lvl, _ in evaluate(tbl, span, apps, flash, bname)]
        ok = got == want
        bad += not ok
        print("  %-4s %-26s %-18s %4.1f/%4.1f MB  apps=%d  -> %-12s want %s"
              % ("ok" if ok else "FAIL", bid, tbl, span / MB, flash / MB,
                 len(apps), got or ["clean"], want or ["clean"]))

    # huge_app.csv must pass once the escape hatch is set
    span, apps = table("huge_app.csv")
    bname, flash = board("esp32-c5-devkitc-1")
    got = [l for l, _ in evaluate("huge_app.csv", span, apps, flash, bname,
                                  allow_single=True)]
    ok = got == []
    bad += not ok
    print("  %-4s custom_allow_single_ota=yes lets huge_app.csv through -> %s"
          % ("ok" if ok else "FAIL", got or ["clean"]))

    print("\nselftest: %s" % ("ALL PASS" if not bad else "%d FAILED" % bad))
    return 1 if bad else 0


try:
    Import("env")  # noqa: F821  (injected by PlatformIO)
except NameError:
    if "--selftest" in sys.argv:
        sys.exit(_selftest())
    print(__doc__.strip().splitlines()[0])
    print("Run with --selftest to check every board x table combination.")
else:
    _run_pio(env)  # noqa: F821
