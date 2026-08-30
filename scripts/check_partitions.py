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
"""
Import("env")  # noqa: F821  (injected by PlatformIO)

import os
import sys

board = env.BoardConfig()


def fail(msg):
    print("\n" + "=" * 72)
    print("PARTITION TABLE CHECK FAILED")
    print("=" * 72)
    print(msg.rstrip())
    print("=" * 72 + "\n")
    env.Exit(1)


def find_csv(name):
    """Resolve a partitions setting the way PlatformIO does: a path relative to
    the project first, then the framework's stock tables."""
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


name = board.get("build.partitions", env.GetProjectOption("board_build.partitions", ""))
csv = find_csv(name)
if not csv:
    # Nothing to check against — don't block the build over it.
    print("check_partitions: no table found for %r, skipping" % name)
else:
    span, apps = parse(csv)
    flash = int(board.get("upload.maximum_size", 0))

    if flash and span > flash:
        fail(
            "%s spans %.2f MB, but %s has only %.2f MB of flash.\n\n"
            "This WOULD HAVE BUILT and then failed on the device. Pick a table\n"
            "that fits:\n"
            "    8 MB board  ->  default_8MB.csv\n"
            "    4 MB board  ->  min_spiffs.csv   (two 1.875 MB OTA slots)\n"
            % (name, span / 1048576.0, board.get("name", "this board"), flash / 1048576.0)
        )

    if len(apps) < 2 and not env.GetProjectOption("custom_allow_single_ota", ""):
        fail(
            "%s has %d app partition(s); over-the-air updates need two.\n\n"
            "With one slot there is nowhere to write an update to, so this box\n"
            "could only ever be reflashed over USB and 'Update all boxes' would\n"
            "not reach it. min_spiffs.csv gives two 1.875 MB slots on a 4 MB\n"
            "board.\n\n"
            "If you really want a single-slot build, set\n"
            "    custom_allow_single_ota = yes\n"
            "in this environment.\n" % (name, len(apps))
        )

    print(
        "check_partitions: %s ok — %.2f MB of %.2f MB flash, %d OTA slots"
        % (name, span / 1048576.0, flash / 1048576.0, len(apps))
    )
