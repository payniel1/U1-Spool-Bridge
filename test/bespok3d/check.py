#!/usr/bin/env python3
"""Does this firmware's payload actually satisfy the Bespok3d plugin?

    python3 test/bespok3d/check.py

The two sides of that question are written in different languages and live in
different repositories, which is exactly the kind of seam that rots quietly.
This puts them together:

    u1BuildPayload()  ->  their validator  ->  their answer  ->  u1ClassifyReply()

Both ends are the SHIPPED code, compiled here against small Arduino stubs, not
a description of it. The middle is a transcription of their rules with a
version and a source link on it (see bespok3d_validator.py) — refresh that when
the plugin moves and this will tell you whether anything broke.

Needs a C++17 compiler and ArduinoJson, which it looks for under .pio/libdeps
(i.e. after one `pio run`). Skips cleanly if either is missing.
"""

import glob
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import bespok3d_validator as b3  # noqa: E402

fails = 0


def chk(name, cond, extra=""):
    global fails
    print(("  ok   " if cond else "  FAIL ") + name + ("" if cond else "  <- %s" % (extra,)))
    fails += (not cond)


def find_arduinojson():
    for pat in (".pio/libdeps/*/ArduinoJson/src", ".pio/libdeps/*/ArduinoJson"):
        for hit in sorted(glob.glob(os.path.join(ROOT, pat))):
            if os.path.isfile(os.path.join(hit, "ArduinoJson.h")):
                return hit
    env = os.environ.get("ARDUINOJSON_SRC")
    return env if env and os.path.isfile(os.path.join(env, "ArduinoJson.h")) else None


def build(out, sources, aj):
    cmd = ["g++", "-std=gnu++17", "-Wall", "-DARDUINOJSON_ENABLE_ARDUINO_STRING=1",
           "-I", os.path.join(ROOT, "include"), "-I", os.path.join(HERE, "stubs"),
           "-I", aj, "-o", out] + [os.path.join(ROOT, s) for s in sources]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[:2000])
        sys.exit("build failed: " + out)


def main():
    if not shutil.which("g++"):
        print("no g++ — skipping")
        return 0
    aj = find_arduinojson()
    if not aj:
        print("ArduinoJson not found (run `pio run` once, or set ARDUINOJSON_SRC) — skipping")
        return 0

    tmp = os.path.join(ROOT, ".pio", "b3check")
    os.makedirs(tmp, exist_ok=True)
    emit = os.path.join(tmp, "emit")
    clas = os.path.join(tmp, "classify")
    build(emit, ["test/bespok3d/emit_payload.cpp", "src/u1_client.cpp", "src/settings.cpp",
                 "src/spool_data.cpp", "src/send_gate.cpp", "src/u1_reply.cpp"], aj)
    build(clas, ["test/bespok3d/classify.cpp", "src/u1_reply.cpp"], aj)

    payloads = json.loads(subprocess.run([emit], capture_output=True, text=True).stdout)

    print("=== u1BuildPayload() vs %s ===" % b3.DESCRIBES)

    stock = b3.handle(payloads["stock"])
    chk("stock-backend payload is accepted", stock == {"state": "success"}, stock)

    ext = b3.handle(payloads["extended"])
    chk("extended-backend payload is refused", ext.get("state") == "error", ext)
    chk("...refusing exactly CARD_TYPE and nothing else",
        ext.get("message") == "unsupported fields: CARD_TYPE", ext)

    # Moonraker wraps a webhook's answer in {"result": ...}.
    ok_body, bad_body = json.dumps({"result": stock}), json.dumps({"result": ext})
    print("  --   bodies the box would see on the wire:")
    print("       " + ok_body)
    print("       " + bad_body)

    for ch, want in [(0, True), (3, True), (4, False), (-1, False)]:
        body = dict(payloads["stock"], channel=ch)
        got = b3.handle(body)
        chk("channel %d %s" % (ch, "accepted" if want else "refused"),
            (got == {"state": "success"}) == want, got)

    def classify(body):
        r = subprocess.run([clas], input=body, capture_output=True, text=True)
        # rstrip("\n") not strip(): an empty message leaves a trailing tab that
        # strip() would eat, collapsing the two fields into one.
        return (r.stdout.rstrip("\n") + "\t").split("\t")[:2]

    verdict, _ = classify(ok_body)
    chk("firmware reads their success as OK", verdict == "OK", verdict)
    verdict, msg = classify(bad_body)
    chk("firmware reads their refusal as BAD_FIELD, so Auto retries",
        verdict == "BAD_FIELD", verdict)
    chk("...and the reason names CARD_TYPE", "CARD_TYPE" in msg, msg)

    print("\nCONTRACT: " + ("ALL PASS" if not fails else "%d FAILED" % fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
