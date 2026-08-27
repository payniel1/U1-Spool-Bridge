# Third-party licences

The code in this repository is MIT (see [LICENSE](LICENSE)). A **compiled
firmware image is not** — it statically links several libraries, two of which
are LGPL-3.0 and one LGPL-2.1. This file is the notice required by those
licences, and it travels with every binary release.

Nothing here restricts *building the firmware for your own dryboxes*. Building
for yourself is not distribution, and none of the obligations below apply to
you. They apply when you hand someone else a `.bin`.

## What is in the image

| Component | Licence | Source |
| --- | --- | --- |
| **AsyncTCP** `^3.3.2` | **LGPL-3.0** | https://github.com/ESP32Async/AsyncTCP |
| **ESPAsyncWebServer** `^3.7.0` | **LGPL-3.0** | https://github.com/ESP32Async/ESPAsyncWebServer |
| **arduino-esp32 core** (via pioarduino `55.03.311`) | **LGPL-2.1-or-later** | https://github.com/espressif/arduino-esp32 |
| ESP-IDF | Apache-2.0 | https://github.com/espressif/esp-idf |
| Adafruit PN532 `^1.3.4` | BSD 3-clause | https://github.com/adafruit/Adafruit-PN532 |
| Adafruit BusIO | MIT | https://github.com/adafruit/Adafruit_BusIO |
| ArduinoJson `^7.2.0` | MIT | https://github.com/bblanchon/ArduinoJson |
| u1-spool-bridge (this project) | MIT | this repository |

Licence texts are in [`licenses/`](licenses/). Both `GPL-3.0.txt` and
`LGPL-3.0.txt` are included because LGPL-3.0 is not a standalone document — it
"incorporates the terms and conditions of version 3 of the GNU General Public
License, supplemented by the additional permissions listed below."

## How a binary release satisfies the LGPL

LGPL-3.0 section 4 covers a "Combined Work" — an application statically linked
with the library, which is exactly what an ESP32 firmware image is. It asks for
four things.

**4(a) — prominent notice.** This file, shipped with every binary.

**4(b) — a copy of the GNU GPL and the LGPL.** [`licenses/`](licenses/).

**4(c) — copyright notices during execution.** The firmware displays no
copyright notices while running, so this clause does not bite. The web UI links
back to this repository.

**4(d) — the relinking requirement.** This is the one with teeth. It offers two
routes:

- *4(d)(1), a shared library mechanism.* **Impossible here.** An ESP32 image is
  statically linked into flash; there is no dynamic linker and no library
  already present on the device.
- *4(d)(0), give the user what they need to relink.* **This is the route this
  project takes.**

The complete application source is in this repository under MIT, and it builds
with a single `pio run`. Every dependency is version-pinned in
[`platformio.ini`](platformio.ini), including the toolchain. So anyone can
clone the repository, replace AsyncTCP or ESPAsyncWebServer with a modified
version, rebuild, and get a working image — which is a fuller satisfaction of
4(d)(0) than shipping relinkable object files would be.

**4(e) — Installation Information.** The firmware is not "User Product"
consumer hardware with locked-down installation; images are flashed over USB
with `esptool` or over the network through the web UI, both documented in
[`docs/FLASHING.md`](docs/FLASHING.md). Nothing prevents a user installing a
modified build.

LGPL-2.1 section 6 asks essentially the same of the arduino-esp32 core, and the
same public buildable source satisfies it.

## If you redistribute a binary

Ship these three things with it and you are on the same footing as this
project:

1. **This file**, unmodified.
2. **The [`licenses/`](licenses/) directory.**
3. **A link to the exact commit or tag** whose source produced that binary.
   Point at a tag, not at `main` — `main` will move, and then the source no
   longer corresponds to the binary you shipped, which is the thing 4(d)(0)
   actually requires.

If you have modified this project's own source, publishing your changes is
courteous but not required — MIT does not ask for it. The LGPL obligations
above are about the *libraries*, and they persist whatever you do to the
MIT-licensed parts.

---

Not legal advice. This is a description of what the licences say and how the
project is arranged to satisfy them. If you intend to ship this commercially,
read the licence texts yourself.
