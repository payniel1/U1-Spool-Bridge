# u1-spool-bridge

Read the RFID tag off a filament spool with an **ESP32 + PN532**, then push the
material into a **Snapmaker U1** slot over WiFi. Driven from a small web UI
served by the board itself.

```
  spool tag            XIAO ESP32-C5 / C3              Snapmaker U1
 ┌──────────┐  13.56MHz  ┌──────────┐    WiFi/HTTP   ┌──────────────┐
 │ Bambu    │◄──────────►│  PN532   │───────────────►│ Moonraker    │
 │ QIDI     │            │  decode  │  POST /printer │ filament_    │
 │ OpenSpool│            │  web UI  │  /filament_    │ detect.set   │
 └──────────┘            └──────────┘   detect/set   └──────────────┘
```

One reader per box, on HSU, and no transport to choose. Two boards build from
this source:

| Board | Flash | Radio | Notes |
|---|---|---|---|
| **Seeed XIAO ESP32-C5** | 8 MB | 2.4 + 5 GHz | The default. Thumbnail-sized, one USB-C port. |
| **Seeed XIAO ESP32-C3** | 4 MB | 2.4 GHz only | Cheaper. Needs `min_spiffs`; no status LED. |

**The wiring is identical on both** — D4, D5, D2 — so one drybox harness fits
either board. Only the GPIO numbers behind those pads differ, and those are
build flags. A C5 devkit builds too, with different pins.

Put one in each drybox and they find each other: the fleet view groups them by
printer, and one upload updates every box on the network.

---

## ⚠️ Read this first: firmware requirement

The write path used here is `POST /printer/filament_detect/set`, which is added by the
community **[SnapmakerU1-Extended-Firmware](https://github.com/paxx12/SnapmakerU1-Extended-Firmware)**
(paxx12). It patches Klipper's `filament_detect` object to be writable.

**Stock Snapmaker firmware does not expose this endpoint.** On stock firmware the U1
only trusts its own RSA-signed tags, and there is no supported way to set a slot's
material remotely. If you are on stock firmware, this project will read your spools
and show you everything — but the send will fail with a 404.

If you would rather not run custom firmware, the board also exposes
`GET /api/tagjson`, which returns the OpenSpool JSON for the spool currently on the
reader. You can write that onto an NTAG215 with a phone (NFC Tools) and let the
printer read it the normal way.

---

## Hardware

| Part | Notes |
|---|---|
| **Seeed XIAO ESP32-C5** | The default target. Dual-band Wi-Fi 6, 8 MB flash, one USB-C port, thumbnail-sized. |
| **…or a Seeed XIAO ESP32-C3** | 4 MB, 2.4 GHz only, no user LED. Same wiring, same enclosure. See [docs/wiring.html](docs/wiring.html). |
| …or a C5 devkit | `esp32-c5-devkitc-1`. Same firmware, different pins. |
| PN532 NFC module | The common red "V3" breakout. **Both DIP switches OFF.** |
| 4 jumper wires | Plus power. |
| 100 µF + 0.1 µF capacitors | Per module. Not optional — see [docs/capacitors.html](docs/capacitors.html). |

### One reader, on HSU

Every board runs a single PN532 over **HSU (UART)**. There is no transport to
pick and no second reader to configure: the firmware speaks HSU and nothing else.

That narrowing is deliberate. The `ESP_ERR_INVALID_STATE` fault that used to take
readers out for good was an I2C-driver fault — one corrupted transaction latches
the ESP-IDF `i2c_master` driver, and every later call fails identically until the
box reboots. A UART has no such state: a glitched frame costs you one read and
the next one is fine. SPI dodges the latch too, but it wants four signal wires on
different pads and the driver clocks it at 1 MHz over the same unshielded dupont,
which is a step backwards for a drybox. What is left is the one that works.

**The wiring did not change**, and that is not a coincidence. The Elechouse V3
and its clones put HSU on the *same header pins* as I2C — I2C labels printed on
the front of the module, HSU labels on the back. The SDA pad is the module's TX
and the SCL pad is its RX, so the wire already running to SDA carries the
module's transmit, and the firmware makes that GPIO its RX. The crossover happens
in the pin assignment, not in the loom.

Set **both DIP switches OFF** and wire it as below.

### Wiring

| PN532 pad | XIAO ESP32-C5 | C5-DevKitC-1 | What it carries |
|---|---|---|---|
| SDA | **D4** (GPIO23) | GPIO 0 | board **RX** ← the module's TX |
| SCL | **D5** (GPIO24) | GPIO 1 | board **TX** → the module's RX |
| RSTO | **D2** (GPIO25) | GPIO 10 | reset — **required** |
| IRQ | **D0** (GPIO1) | — | unused; harmless to leave connected |
| VCC | 3V3 | 3V3 | — |
| GND | GND | GND | — |

The pads keep their I2C names because that is what the module prints on them; the
firmware drives them as a serial port. Change any of them in `platformio.ini`
(`-DPIN_PN532_RX=…`, `-DPIN_PN532_TX=…`, `-DPIN_PN532_RST=…`) if you rewire.

**RSTO is not optional.** The driver pulses it on every init, and driving it by
hand is the only recovery lever there is once a module has stopped answering.

Pins to leave alone: on the XIAO, D3 (GPIO7) is a strapping pin, D6/D7 are
UART0, and the underside pads are JTAG. On the C5 devkit avoid GPIO 2/7/27/28,
11/12 and 13/14.

**[docs/wiring.html](docs/wiring.html) is the same thing as a diagram** — open it in
a browser for a colour-coded picture, the DIP switch settings and the pins to avoid.

**[docs/capacitors.html](docs/capacitors.html) — fit the two decoupling capacitors.**
The XIAO and the PN532 share one 3V3 rail, and the C5 peaks at 403 mA transmitting on
5 GHz against Espressif's 600 mA supply figure. The PN532's 140 mA comes out of what
little is left, so a transmit burst dips the module's local rail and kills whatever
the reader had in flight. That page has the parts list, the polarity diagram and
the fitting steps; **[docs/capacitor-fitting.html](docs/capacitor-fitting.html)** is the
same job drawn — the failure mechanism, where the two parts sit, and the order to work
through a fleet. **[docs/solder-guide.html](docs/solder-guide.html)** is the bench detail:
which side of the board you are working on and why the pin order reverses when you flip it,
what a good joint looks like next to a cold one and a bridge, where the iron goes, and the
continuity check that happens before power. Treat all three as part of the build, not a
repair.

The link runs at 115200 baud on `Serial1`, which is the PN532's HSU default and
already set in `tag_reader.cpp`. Keep the wires short; the module is noisy above
~15 cm of dupont.

None of this replaces the decoupling capacitors. A sagging rail browns the module
out whatever it is talking over — HSU turns a fatal fault into a transient one,
and that is all. Fit both parts.

---

## Build & flash

```bash
pio run -e seeed_xiao_esp32c5 -t upload      # XIAO C5 — the default
pio run -e seeed_xiao_esp32c3 -t upload      # XIAO C3
pio run -e esp32-c5-devkitc-1 -t upload      # C5 devkit
pio device monitor                           # 115200 baud
pio test -e native                           # decoder tests on your PC, no hardware
```

**The two images are not interchangeable**, and the fleet updater enforces
that: it reads the chip id out of the file and refuses any box it does not
match. A mixed fleet therefore updates in two passes, each run from a box of
that chip — see [the fleet section](#updating-the-whole-fleet).

The C3 env pins `min_spiffs` and that is not optional. The app is about
1.41 MB and the default 4 MB partition table gives 1.25 MB OTA slots, so a
default C3 build has nowhere to write an update to.

Updating a box that's already deployed needs no USB:

```bash
pio run -e seeed_xiao_esp32c5-ota -t upload --upload-port u1-drybox-3.local
```

…or drag a `firmware.bin` onto the **Firmware** card in any box's web UI. The
image goes to the inactive app slot and the boot partition only switches once
it verifies, so a failed or interrupted update leaves the box running exactly
what it was running before. Settings survive.

For a fleet, press **Update all boxes** instead of Install. You upload the image
once, to the box you're on; that box then pushes it to every other box it can
find, one at a time, and reboots into it itself last. Pushing from the box
rather than the browser is what makes it work on boxes running older firmware,
which are the ones that need it.

It reads the file first and checks it against each box — a wrong chip is
refused outright, a wrong transport or reader count is offered unticked with
the reason, and a box already on that version is skipped. Afterwards each box
is asked what it is now running, so **DONE** means the version actually
changed rather than that an upload was accepted. See
[OTA](docs/FLASHING.md#updating-the-whole-fleet-at-once) for the details and
how to set a password — it's open on the LAN by default.

**Step-by-step guides:** [docs/FLASHING.md](docs/FLASHING.md) (Linux/macOS) ·
[docs/FLASHING-WINDOWS.md](docs/FLASHING-WINDOWS.md) (Windows 11) ·
[docs/ARDUINO-IDE.md](docs/ARDUINO-IDE.md) (no PlatformIO) — toolchain setup, baking
WiFi credentials in before you flash (worth doing before the first board, not the
eighth), port identification, per-board provisioning, OTA, and which update
command keeps a box's settings versus wiping them.

The Arduino core comes from the
[pioarduino](https://github.com/pioarduino/platform-espressif32) fork — the official
`platform-espressif32` ships no C5 Arduino core. Bump the release URL in
`platformio.ini` if you want a newer one.

### Verified against

All targets build clean — no warnings with `-Wall -Wextra`:

| | |
|---|---|
| platform | pioarduino espressif32 **55.03.311** (arduino-esp32 3.3.11, IDF 5.5.5) |
| toolchain | riscv32-esp-elf **14.2.0** |
| Adafruit PN532 | 1.3.4 (+ Adafruit BusIO) |
| ArduinoJson | 7.4.2 |
| AsyncTCP | 3.5.0 |
| ESPAsyncWebServer | 3.12.0 |

| Target | Notes |
|---|---|
| `seeed_xiao_esp32c5` *(default)* | The XIAO C5 on HSU. Ships as `firmware-c5.bin`. |
| `seeed_xiao_esp32c5-ota` | The same env with `upload_protocol = espota` |
| `seeed_xiao_esp32c3` | The XIAO C3, `min_spiffs`. Ships as `firmware-c3.bin`. |
| `seeed_xiao_esp32c3-ota` | The same, over the network |
| `esp32-c5-devkitc-1` | Same firmware, devkit pins |
| `native` | Decoder tests on the host, no hardware |

Two binaries ship, one per chip. Sizes as built:

| | flash | of its OTA slot | static RAM |
|---|---|---|---|
| C5 | 1,493,951 | 44.7% of 8 MB | 59,460 |
| C3 | 1,414,831 | 72.0% of `min_spiffs` | 46,012 |

The C3 is the smaller build because the 5 GHz radio code compiles out. The XIAO
C5's 8 MB layout gives two 3.19 MB app slots; the C3 gets two 1.875 MB slots from
`min_spiffs`. Either way there are two, which is what makes OTA possible.

All six environments build with no warnings under `-Wall -Wextra`.

The Spoolman request sequence was replayed against a real **Spoolman 0.26.1** — field
creation, `extra` merge semantics, the JSON-encoded value format, the list and single
spool response shapes, and the add-to-current / remove-from-others link behaviour.
Note that Spoolman omits null fields from responses entirely, which is why the parser
defaults every read rather than assuming a key is present.

---

## Wi-Fi bands

**On a C3 there is nothing to choose** — it is 2.4 GHz only and the band control
does not appear. The rest of this section is about the C5.

Settings gains a **WiFi band** control on a dual-band chip: *Auto*, *2.4 GHz only*,
or *5 GHz only*. The header grows a pill showing the band you're actually associated
on plus RSSI, so you can tell at a glance whether the radio did what you asked.

Three things worth knowing:

- **The band has to be picked before association**, so changing it reboots the
  bridge — same as changing the SSID.
- **A band-restricted join that fails retries across both bands** before falling
  back to AP mode. Selecting *5 GHz only* on a network that doesn't have a 5 GHz
  SSID won't strand you. When that happens the band pill turns amber and reads
  `2.4 GHz — 5 GHz only was requested`, and the boot log says so outright, so a
  box quietly running on the wrong band is visible rather than something you
  discover weeks later.
- **The setup access point stays on 2.4 GHz** regardless of the setting. A 5 GHz-only
  AP is invisible to a lot of phones, and that's the network you need to reach in
  order to fix a bad config.

Note that this is about the *bridge's* link, not the printer's. The two only need to
be on the same network — the router bridges the bands, so a 5 GHz bridge talks to a
2.4 GHz printer without trouble. (The exception is a router with band or client
isolation switched on, e.g. a guest SSID.)

`HAS_DUAL_BAND` is derived from the SoC's `SOC_WIFI_SUPPORT_5G` capability flag
rather than from the target name, so the 5 GHz controls appear or don't with no
`#ifdef` soup in the application code — and a C3 build drops the radio code
entirely, which is why it comes out ~70 KB smaller than the C5 one.

## First run

1. On first boot there are no WiFi credentials, so the board comes up as an access
   point: **`U1-SpoolBridge`**, password `spoolbridge`.
2. Join it and open <http://192.168.4.1/>.
3. In **Settings**, enter your WiFi SSID/password and the U1's IP address, then save.
   The board reboots and joins your network.
4. After that it's at `http://u1-spool-bridge.local/` (or whatever IP your router
   hands out — the serial monitor prints it).

Press **Test printer** to confirm the U1 answers on `/printer/info`.

---

## Using it

Put a spool tag on the PN532. The UI shows what it found. By default nothing is sent
yet — see **When does it send?** below.

Everything on the card is editable before you send, so an unrecognised tag (or a
spool with no tag at all) is still a two-second job: fill in the fields, pick a slot,
hit **Send to printer**.

## When does it send?

A reader mounted anywhere near the filament path sees spools it isn't being shown on
purpose. Two things stop that turning into a stream of writes to the printer.

**The dwell filter** comes first, in the reader itself. A tag has to sit in the field
continuously for `dwellMs` (default 700 ms) before it's even decoded. A spool carried
past the antenna never becomes a scan at all — which also spares the ~1 second of
MIFARE traffic that decoding a Bambu tag costs.

**The gate** decides what happens to a scan that does settle. Every scan is held as
*pending*; the mode picks what releases it:

| Mode | Behaviour |
|---|---|
| **Send when a spool is inserted** *(default)* | For a reader mounted in one drybox. A spool appearing in the box goes to that box's bound slot. See [A reader per drybox](#a-reader-per-drybox). |
| **Send when a slot loads** | For a bench reader serving several slots. The bridge watches `print_task_config.filament_exist[]` over Moonraker; when a slot goes from empty to occupied, the pending spool is sent **to that slot**. |
| **Send on every scan** | The old behaviour, still rate-limited by the cooldown. |
| **Send only when armed** | Nothing goes out until you press **Arm slot N**. One shot, then it disarms. If a spool is already on the reader, arming sends it immediately. |
| **Manual only** | Only the **Send to printer** button. |

On-load mode is the interesting one, because the printer tells you *which* slot — so
you stop having to pick one. Scan the spool, feed it in, and it lands in the slot it
physically went into. `filament_exist` is the same field the U1's own filament UI
reads to decide whether a slot is occupied.

Two guards apply in every mode. A pending scan **goes stale** after `scanValidS`
(default 5 min), so loading a spool half an hour after waving a different one past
doesn't misfile it. And a **cooldown** (default 30 s) suppresses the same tag going
to the same slot twice — a spool drifting in and out of range mid-print can't rewrite
what's already there.

The card under the slot buttons always spells out what will happen next, because
"scan a spool and nothing visibly occurs" is otherwise indistinguishable from broken.
The slot buttons carry a green dot when the printer reports filament in them.

If the bridge can't read slot state — stock firmware, printer off, wrong host — it
says so there and backs off to one attempt every 15 s rather than hammering the main
loop. Nothing auto-sends in that state; the **Send to printer** button still works.

### What it can read

| Tag | Chip | Status |
|---|---|---|
| **OpenSpool** | NTAG213/215/216 | Full. NDEF JSON — the format the U1 itself uses. |
| **Bambu Lab** | MIFARE Classic 1K | Full. Keys derived from the tag UID; material, colour, temps, weight, spool length, tray UID, production date. |
| **QIDI** | MIFARE Classic 1K (FM11RF08S) | Material + colour codes. The tag carries nothing else, so temperatures come from the material defaults. |
| **Creality CFS** | MIFARE Classic 1K | Detected, not decoded — the payload is AES-encrypted and the keys aren't public. Fill the fields in by hand. |

Bambu key derivation is `HKDF-SHA256(ikm = UID, salt = 9a759cf2…, info = "RFID-A\0")`,
16 × 6-byte sector keys, from the public
[Bambu-Research-Group/RFID-Tag-Guide](https://github.com/Bambu-Research-Group/RFID-Tag-Guide)
work. This reads tags you already own; it does not forge the RSA signature block, and
nothing here writes to a Bambu tag.

### What gets sent

```jsonc
POST http://<printer>/printer/filament_detect/set
{
  "channel": 0,                    // slot 1..4 -> channel 0..3
  "info": {
    "VENDOR": "Bambu Lab",
    "MAIN_TYPE": "PLA",            // PLA PETG ABS ASA TPU PVA PC PA
    "SUB_TYPE": "Matte",           // Basic Matte SnapSpeed Silk Support HF 95A "95A HF"
    "RGB_1": 1193046,              // 0xRRGGBB as an int
    "RGB_2": 0, "RGB_3": 0, "RGB_4": 0, "RGB_5": 0,
    "ALPHA": 255,
    "HOTEND_MIN_TEMP": 190,
    "HOTEND_MAX_TEMP": 230,
    "BED_TEMP": 60,
    "CARD_UID": [4, 26, 43, 60],   // so the printer tracks physical tag presence
    "CARD_TYPE": "MIFARE_1K",
    "SKU": 0
  }
}
```

Vendor strings get folded onto the U1's vocabulary before sending — `PETG Rapido`
becomes `PETG` / `HF`, `PAHT-CF` becomes `PA` / `Basic`, `TPU-Aero` becomes
`TPU` / `95A HF`, and so on. If a slicer profile doesn't match, turn on
**Report vendor as "Generic"** in Settings, which is what the extended firmware's
own generic mode does.

---

## A reader per drybox

The design case is one node per box: **an ESP32-C5 + PN532 inside each drybox**, each
bound to one printer and one slot, all talking to the printers over WiFi. Eight boxes
across two U1s is eight identical boards running identical firmware with different
settings.

Don't try to hang eight PN532s off one board. The firmware drives exactly one
reader; HSU is point-to-point, so every extra module would want a serial port of
its own; and you would still be running long unshielded links out to each box,
with readers close enough together to interfere at 13.56 MHz. A board per box
sidesteps all of it.

### What changes with a fixed mount

A spool in a drybox is *resident*, not presented, so the model inverts: the tag is
there permanently and what matters is when it appears and disappears.

**The reader's identity picks the slot.** In `Send when a spool is inserted` mode (the
default) the box's own slot binding decides where the data goes. No load-event
round-trip, no ambiguity about which slot a spool went into.

**Absence is debounced.** A spool sitting at the edge of the antenna's range drops out
for a poll or two — vibration, a slightly-off mount, a cold morning. Treating that as
a removal would make it look like a fresh insertion a second later and rewrite the
printer for no reason. A tag has to be gone for `absenceMs` (default 3 s) before the
box counts as empty.

**Boot re-asserts.** If a spool is already in the box at power-up it's sent once, so a
node that reboots doesn't leave the printer with stale data. Turn off *Re-send on
boot* if you'd rather it stayed quiet.

### Two-slot dryboxes

A box with two slots takes **two complete nodes** — two ESP32s, two PN532s, sharing
nothing but the enclosure. No special build, no configuration beyond giving each its
own name and slot:

```bash
curl -X POST http://u1-box-a3f2.local/api/settings -H 'Content-Type: application/json' \
  -d '{"boxName":"Drybox 4A","defaultChannel":2,"printerHost":"192.168.1.42"}'
curl -X POST http://u1-box-b17c.local/api/settings -H 'Content-Type: application/json' \
  -d '{"boxName":"Drybox 4B","defaultChannel":3,"printerHost":"192.168.1.42"}'
```

They show up as `u1-drybox-4a.local` and `u1-drybox-4b.local`, and appear as separate
tiles in each other's fleet view.

The one thing worth attention is **RF crosstalk**. Two 13.56 MHz antennas in one
enclosure are close enough to interfere, and two independent boards can't coordinate
their polling. Mitigations, in order of effectiveness:

- **Separation.** Get the antennas as far apart as the box allows, ideally 10 cm+, and
  avoid pointing them at each other. Mounting them on opposite walls beats side by side.
- **A metal or ferrite shim** between the two antennas if separation isn't possible.
- **Polling jitter**, which the firmware does for you: the scan interval is randomised
  by up to +25% each cycle. Two boards on fixed 400 ms timers can drift into phase and
  stay there, giving one of them a persistent blind spot; jitter guarantees they
  de-phase within a few cycles.

The dwell filter also helps — a read has to succeed repeatedly over 700 ms to count, so
an occasional collided poll costs nothing. If you see a lane flapping between present
and empty in the activity log, that's crosstalk: move the antennas apart, or raise
`absenceMs`.

### Provisioning eight of them

Every board self-names from its MAC (`Box A3F2` → `u1-box-a3f2.local`), so eight fresh
units are individually reachable straight away rather than fighting over one hostname.

Bake the network in at flash time and there's nothing to click:

```ini
build_flags =
    ${common.build_flags}
    -DDEFAULT_WIFI_SSID='"HomeNet"'
    -DDEFAULT_WIFI_PASS='"hunter2"'
    -DDEFAULT_SPOOLMAN_HOST='"192.168.1.20"'
```

Flash all eight, then give each its identity over HTTP:

```bash
# left-hand printer, slots 1-4
curl -X POST http://u1-box-a3f2.local/api/settings -H 'Content-Type: application/json' \
  -d '{"boxName":"Drybox 1","defaultChannel":0,"printerHost":"192.168.1.42"}'
curl -X POST http://u1-box-b17c.local/api/settings -H 'Content-Type: application/json' \
  -d '{"boxName":"Drybox 2","defaultChannel":1,"printerHost":"192.168.1.42"}'
# ...
# right-hand printer, slots 1-4
curl -X POST http://u1-box-0d51.local/api/settings -H 'Content-Type: application/json' \
  -d '{"boxName":"Drybox 5","defaultChannel":0,"printerHost":"192.168.1.43"}'
```

`defaultChannel` is 0-based; the UI shows it as slot 1–4. Renaming a box changes its
mDNS name too, so `Drybox 3` becomes `u1-drybox-3.local`.

### The fleet view

Each node advertises `_u1spool._tcp` over mDNS, and every box's **Other boxes** card
fills itself in: it asks when the page opens and again every 30 s while the tab is
visible, and stops asking when it is not. The box refuses to rescan more often than
once every 10 s however often it is asked, because each scan stalls that box's reader
for as long as the mDNS query and the per-peer fetches take.

Each tile shows what that box holds — filament, colour, remaining weight, slot,
printer, and whether that printer is answering — and links to its own UI, so one
bookmark reaches the whole fleet.

**Boxes group by the printer they feed**, which needs no setup: every box already
knows its printer. Eight boxes across two machines arrive as two groups of four. The
box you are looking at appears in its group too, marked *(this box)* — a group of four
slots reads wrong with a hole where you are standing.

To arrange them another way, put a label in **Group** in Settings; it overrides the
printer for that box. Each group header carries a summary (`3/4 loaded · 1 on older
fw`), folds away when you tap it — collapsed groups are remembered in that browser —
and has its own **Update** button that runs a firmware update over just that group.

**Renaming a group renames it everywhere.** Tap the name in the header, type a new
one, press Enter: the box applies it to itself and then writes it to every other box
in that group. The push goes box-to-box, not from your browser, and carries *only* the
group name — every field in the settings parser is `isNull()`-guarded, so nothing else
on those boxes is touched.

Nothing is centralised: each node talks to its own printer directly. Losing one box
costs you that box.

### Updating the whole fleet

Pick a `.bin` in the **Firmware** card and press **Update all boxes** instead of
Install. You upload it **once**, to the box you are on. That box writes it to its
inactive app slot, reads it back out of flash, and POSTs it to each of the others in
turn, confirms each one, and reboots into it itself last.

That indirection is the whole trick. A browser posting to a box it was not served from
is a cross-origin request, and cross-origin is enforced by the *receiving* box —
firmware older than 1.12.0 sends no CORS headers and has no `OPTIONS` route, so the
browser refuses to send the image at all. Those old boxes are exactly the ones an
update is for. A box talking to another box has no such rule. It also means your phone
uploads 1.5 MB once instead of once per box.

Before anything is sent, the browser reads the image and checks it against every box:

| From the image | If it disagrees with the box |
| --- | --- |
| chip id (`0x17` = C5, `0x05` = C3) | **blocked**, no override |
| `bus=` in the build marker | offered unticked — it would boot, but that reader would stop working |
| `rc=` reader count | offered unticked, same reason |
| `fw=` version | skipped if the box already has it |

Those last three come from a marker the firmware bakes into its own image
(`U1SB-FINGERPRINT-v1|fw=…|tgt=…|bus=…|rc=…|end`), so a file that is a valid ESP32
image but belongs to some other project is refused outright. The ESP-IDF app
descriptor cannot do this job — its version field is filled in by arduino-lib-builder
with its own git hash.

**A mixed C5/C3 fleet updates in two passes**, one per chip, and each pass must be run
from a box of that chip: the driving box holds the image for the others, so an image it
cannot take is one it cannot pass on. The plan says so and refuses to start rather than
failing halfway.

**DONE means the version changed**, not that an upload was accepted. A box reboots the
moment the image verifies, so the reply usually never arrives — and a box can answer
`ok` while still running the old firmware. Each one is asked afterwards what it is now
running. Failures say which: `wrong OTA password`, `still running 1.13.0`, `no answer
after the upload`.

A failure is not a brick. Every box writes to its inactive slot and only switches on a
verified image, so a box that fails an update is still running exactly what it was.

---

## Spoolman

Point the bridge at a [Spoolman](https://github.com/Donkie/Spoolman) server (Settings →
*Use Spoolman*, host, port — 7912 by default) and the tag stops being the source of
truth. It becomes an identifier, and Spoolman says what's actually on the spool.

**Scan → resolve.** The bridge looks the tag UID up in the `card_uids` extra field —
the same field the U1 Extended Firmware's SpoolLink uses, so the two agree on what a
tag means. On a hit it takes Spoolman's vendor, material, colour, temperatures and
diameter, shows how much filament is left, and pushes that to the printer.

**Spoolman wins.** If a Bambu tag says one thing and your Spoolman record says
another, the record wins — you may have corrected the temperatures or renamed the
colour, and that shouldn't be undone by a factory tag. Anything Spoolman leaves blank
falls back to whatever the tag said.

**Blank tags work.** This is the cheap setup: stick an unprogrammed NTAG215 on a
spool, scan it, hit **Link to a Spoolman spool**, pick the spool from the list. From
then on that tag resolves to a full filament definition even though there's nothing
written on it. Linking also strips the UID off any *other* spool claiming it — two
spools answering to one tag makes lookups a coin flip.

**Write-back.** When a spool is sent to a slot, its Spoolman record gets
`location` set and one line appended to its comment, so you have a history of what
went where and when.

The location default is **`{group}`** — the group name alone, with no slot. Spoolman
groups its list *by* location, so the location string is the heading: four boxes
feeding one printer all file under `Printer A`, one heading holding four spools. Put
`{slot}` in it and every box gets a location of its own, which is eight headings
instead of two. The field takes `{group}`, `{slot}` and `{box}`, a preview under it
shows exactly what will be filed and warns when a format would split a group up, and
**Apply to every box** pushes one format to the whole fleet so you set it once rather
than in eight Settings pages. `{group}` falls back to the box name when a box has no
group, so ungrouped boxes do not all collapse into one blank location.

Changing the format — or renaming a group — makes every affected box immediately
re-file whatever it currently holds, so Spoolman catches up instead of showing the old
location until that spool next happens to be reloaded. The re-file rewrites the
location only: renaming a group three times should not leave three "loaded into"
entries describing one load. The comment is trimmed
from the top on whole-line boundaries to stay under Spoolman's 1024-character limit.
Both behaviours are individually switchable.

Timestamps come from NTP, so set an NTP server and a POSIX `TZ` string in Settings.
If the clock never lands, the comment lines are written without a date rather than
with a fake one.

### If you already run SpoolLink

You don't strictly need any of this — SpoolLink resolves `CARD_UID` printer-side, and
the bridge already sends it. Doing the lookup on the bridge instead buys you three
things: it works when Moonraker has no `[spoolman]` block; the remaining weight and
matched filament show up on the scan card *before* you commit; and the linking flow
means you never have to type a UID into Spoolman's web UI by hand. Both paths write
the same field, so running both is fine.

---

## Materials

The picker offers 30 types, grouped as Common / Carbon fibre / Glass fibre / Engineering:

| Group | Types |
|---|---|
| Common | PLA, PETG, **PCTG**, ABS, ASA, TPU, PVA |
| Carbon fibre | PLA-CF, PETG-CF, PET-CF, ABS-CF, ASA-CF, PC-CF, PA-CF, PAHT-CF, PP-CF, PPA-CF, PPS-CF |
| Glass fibre | PLA-GF, PETG-GF, PET-GF, ABS-GF, ASA-GF, PC-GF, PA-GF, PP-GF |
| Engineering | PC, PA, PAHT, PET, PP, PPA, PPS, HIPS |

**Why these are safe to send.** The Extended Firmware's `filament_detect` design doc says
`MAIN_TYPE` takes any string — PLA, PETG, ABS, TPU and PVA get the printer's RFID-protocol
mapping, and *"other values accepted but not RFID-protocol-mapped"*. So a `PAHT-CF` reaches
the slot and displays; it simply isn't in the printer's own tag-decoding table, which does
not matter when the bridge is the thing doing the decoding.

**Behaviour change in 1.10.0.** `normalizeMainType()` used to flatten every filled grade
onto its base — `PLA-CF` became `PLA`, `PAHT-CF` became `PA` — and folded `PCTG` into
`PETG`. It no longer does. A decoded tag that says PA-CF now sends PA-CF, because a PA-CF
spool is not a PA one and the printer will take the string.

The matcher works out the base family and the fill separately, then composes them. Two
details worth knowing:

- **Order is most-specific-first**, so PAHT is not shadowed by PA, PPA and PPS are not
  shadowed by PP, and PCTG is shadowed by neither PETG nor PC. There are tests for exactly
  these collisions.
- **`CF` and `GF` must appear as tokens**, not bare substrings — otherwise a vendor name
  that happens to contain the letters would silently promote a plain filament to a
  composite. `PLA CF` is a composite; `PLA Scfold` is not.

Filled grades get default hotend temperatures 10 °C above the unfilled base, bed unchanged.
Anything the tag or Spoolman actually specifies still wins over the default.

---

## Loaded in the printer

The **Loaded in the printer** card shows all four slots as the machine itself reports them:
colour, vendor, material, nozzle and bed temperatures, and the tag UID if `CARD_UID` was
sent. It is a readback of `filament_detect.info[]` — the same object this box writes to —
so it is the printer's answer, not our memory of what we sent.

That distinction is the point. The reader's own panel goes blank the moment you lift the
spool out of the drybox to load it, which is exactly when you want to check what went
across. This card keeps showing.

It also makes a disagreement visible. If a slot reads something other than what you sent,
either the write did not land or something else has overwritten it since — and you can see
that without walking to the machine.

**When the printer has nothing to say, the dryboxes answer instead.** The U1 only reports
filament data for a slot once that filament is actually in the machine, so between putting
a spool in a box and the printer pulling it through — which in practice is when a print
starts — the readback is blank even though the answer is known. A slot the printer cannot
describe falls back to the drybox's own reader, marked **IN THE BOX**, naming which box it
is in, how much is left, and what the printer reports for that slot:

```
SLOT 1  SUNLU PLA Basic    IN THE BOX  in Drybox 1 · 740 g · printer reports this slot empty
SLOT 2  3D Fuel PCTG Basic             220–240°C nozzle · 70°C bed · UID 042312FD400289
```

Slot 2 there is genuinely loaded, so the printer's own data wins — it carries the
temperatures and the card UID, which a drybox reading cannot. The slot your own box covers
fills instantly; the rest arrive with the fleet scan.

A slot whose `CARD_UID` matches a tag this box has read is tagged **THIS BOX**, so across
eight dryboxes and two printers you can tell which box fed which slot.

Refreshed every 15 s, and immediately after a send, after a slot's occupancy changes, or
when you press **Refresh**. The periodic timer is only a backstop for changes made at the
machine itself.

On stock firmware the card reports `filament_detect not exposed` and falls back to showing
occupancy alone — presence comes from `print_task_config`, which stock does expose.

---

## Timing settings

Everything in **Settings** that takes a duration, what it governs, and the range it will
accept. Defaults are sane for a drybox; the ones worth touching are marked.

### Reader — deciding a tag is there, or gone

| Setting | Default | Range | What it does |
|---|---|---|---|
| **Scan interval ms** | 400 | 100–5000 | How often the reader looks. Each interval gets up to +25% random jitter, so two boards sharing one drybox don't drift into lockstep and blind each other. Lower costs power and web-UI responsiveness for little gain. |
| **Dwell ms** | 700 | 0–10000 | How long a tag must sit still before it is decoded at all. This is what stops a spool *carried past* the antenna from triggering a send — and it spares you a second of MIFARE traffic against a tag that has already gone. |
| **Absence ms** | 3000 | 200–60000 | How long a tag must be gone before it counts as removed. A spool at the edge of the field blinks out for a poll or two; without this debounce that reads as a removal and then a fresh insertion, and the printer gets rewritten for nothing. |

### Gate — when a scan may reach the printer

| Setting | Default | Range | What it does |
|---|---|---|---|
| **Cooldown s** | 30 | 0–3600 | Minimum gap between sends. Stops a flapping read turning into a stream of writes. **0 disables it.** |
| **Scan valid for s** | 300 | 5–3600 | How long a pending scan stays fresh while waiting for the load to happen. In the default on-load mode you scan the spool, then physically load it; take longer than this and the scan is discarded rather than attached to whatever loads next. |
| **Arm timeout s** | 120 | 5–3600 | How long a slot stays armed after you press **Arm slot N** before it gives up. |

### Printer

| Setting | Default | Range | What it does |
|---|---|---|---|
| **Slot poll ms** | 1000 | 250–10000 | How often to ask Moonraker which slots are occupied — the edge that releases a pending scan in on-load mode. If the query fails the firmware backs off to 15 s on its own and says so in the log, then returns to this value once the printer answers again. |

### Clock

| Setting | Default | What it does |
|---|---|---|
| **NTP server** | `pool.ntp.org` | Only used so the Spoolman comment trail carries real dates. Nothing else in the firmware needs the wall clock, and everything works if it never syncs. |
| **Timezone (POSIX TZ)** | `UTC0` | POSIX TZ string, not an IANA name — `CET-1CEST,M3.5.0,M10.5.0/3`, not `Europe/Berlin`. Note the POSIX sign convention is inverted from what you expect: US Central is `CST6CDT,M3.2.0,M11.1.0`. |

**If a spool sits at the very edge of the antenna's range**, it will blink in and out and you
will see repeated `spool removed` in the log. Raising **Absence ms** to 8000–10000 will quiet
that and stop the pointless re-sends — but it is masking the symptom. The real fix is moving
the spool or the antenna a centimetre or two; see the `tag ... detected but not decoded`
lines, which name the UID doing it.

---

## HTTP API on the board

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/` | Web UI |
| `WS` | `/ws` | Live status, tag reads, log lines |
| `GET`/`POST` | `/api/settings` | Read / write config (passwords are never echoed back) |
| `POST` | `/api/send` | `{"channel":0,"spool":{…}}` — queued, result comes back over the websocket |
| `POST` | `/api/rescan` | Forget the last UID so the same spool triggers again |
| `GET` | `/api/arm?channel=N` | Arm slot N+1 for the next settled scan |
| `GET` | `/api/brief` | Compact status — what the other boxes read |
| `GET` | `/api/fleet` | Browse the other boxes over mDNS; answers over the websocket |
| `GET` | `/api/ping` | Probe the printer; result over the websocket |
| `GET` | `/api/tagjson` | OpenSpool JSON for the current spool |
| `GET` | `/api/spoolman/spools` | Trimmed spool list for the picker; arrives over the websocket |
| `POST` | `/api/spoolman/link` | `{"spoolId":42}` — attach the tag on the reader to that spool |
| `POST` | `/api/ota` | Firmware upload; multipart body, written to the inactive slot |
| `POST` | `/api/radiotest` | Reboot into the 5-minute radio-off reader test |
| `POST` | `/api/slots` | Re-read what the printer says is loaded; answer over the websocket |

### Testing the printer path without loading anything

`/api/send` bypasses the gate completely — no cooldown, no pending scan, no load event, no
tag on the reader. It is the way to prove the printer half works before you involve any
hardware:

```bash
# fabricate a spool entirely — nothing needs to be on the reader
curl -X POST http://u1-box-ffcf.local/api/send \
  -H 'Content-Type: application/json' \
  -d '{"channel":0,"spool":{"vendor":"Generic","mainType":"PLA","subType":"Basic",
       "rgb":16711680,"hotendMin":190,"hotendMax":230,"bedTemp":60,"weightG":1000}}'

# minimal version — every field has a default
curl -X POST http://u1-box-ffcf.local/api/send -H 'Content-Type: application/json' \
  -d '{"channel":0,"spool":{}}'

# use whatever tag is on the reader right now, but skip the gate
curl -X POST http://u1-box-ffcf.local/api/send -H 'Content-Type: application/json' \
  -d '{"channel":0}'
```

**The 200 you get back only means "queued".** The blocking HTTP call to the printer runs
from `loop()` afterwards, and its real outcome — success, 404 on stock firmware, 401 for a
missing API key — arrives over the websocket and lands in the **Activity** log. Watch there,
or on the serial console, not at curl's exit code.

The same path is behind the web UI's **Send to printer** button, which posts the current
form contents. Editing the form by hand and pressing it is the no-terminal version of the
above. And switching the trigger dropdown to **Send on every scan** makes real tags fire
immediately without waiting for a load, which is the more convenient mode for bench work.

Values are normalised on the way in exactly as a decoded tag would be, so a hand-made spool
exercises the same code path as a real one.

Blocking work (the HTTP call to the printer) is queued and run from `loop()`, never
on the AsyncTCP task — that's what keeps the UI responsive while a send is in flight.

Settings keys worth knowing for scripted provisioning, since they are new and
not obvious: `wifiTxPower` (0 = leave the radio at its 20 dBm default, otherwise
8–20 dBm — values outside that are clamped) and `wifiBand` (0 auto, 1 = 2.4 GHz,
2 = 5 GHz). `wifiTxPower` applies live; `wifiBand` reboots the box.

Each lane in `/api/brief` carries a `resets` count — how many times that reader has
had to be closed, hard-reset over RSTO and re-initialised since boot. Worth polling across the fleet: it's the earliest
warning that a particular box's wiring is going marginal, well before it drops out.

```
curl -s http://u1spool-a.local/api/brief | jq '{box, lanes: [.lanes[] | {slot, reader, resets}]}'
```

---

## Layout

```
include/
  config.h        pin map, AP credentials, version
  spool_data.h    vendor-neutral spool struct + normalisation
  decoders.h      pure decoders (no hardware, unit-testable)
  crypto_hkdf.h   SHA-256 / HMAC / HKDF
  tag_reader.h    PN532 front end
  u1_client.h     Moonraker client
  spoolman.h      Spoolman client
  spoolman_fields.h  card_uids / JSON encoding (hardware-free, unit-tested)
  send_gate.h     when a scan may reach the printer (hardware-free, unit-tested)
  settings.h      the NVS struct — and the static_assert layout guard
  ota.h           over-the-air updates
  fleet_ota.h     box-to-box firmware and settings push
  fleet_wire.h    the multipart body a box puts on the wire (hardware-free, unit-tested)
  web_ui.h        web server
  web_page.h      the UI, embedded as PROGMEM
src/
  main.cpp        setup + the one non-blocking loop
  tag_reader.cpp  the PN532 on HSU, reset/recovery, dwell filtering
  dec_openspool.cpp  NDEF TLV walk + OpenSpool JSON
  dec_bambu.cpp      key derivation + block map
  dec_qidi.cpp       material/colour code tables
test/test_decoders/  48 host-side tests
arduino/u1_spool_bridge/   include/ + src/ flattened into an Arduino IDE sketch
docs/               flashing guides, wiring and capacitor pages
licenses/           GPL-3.0, LGPL-3.0 and LGPL-2.1 texts, for binary releases
```

**One thing to know before editing `Settings`.** It is persisted to NVS as a raw
struct, so its byte layout is a storage format, not an implementation detail.
`settings.h` pins every field offset with a `static_assert`. Insert a field in the
middle rather than appending it and every deployed box reads its neighbours' bytes as
its own — which for `wifiSsid`/`wifiPass` means a fleet that quietly drops off the
network during an update and has to be reconfigured one box at a time over its AP.
Append, and the loader overlays the older, shorter blob onto defaults.

The decoders deliberately don't touch Arduino APIs, which is why `pio test -e native`
can run them on your laptop against synthetic tag dumps.

## Troubleshooting

**"PN532 not answering on HSU"** — both DIP switches must be OFF, or the two
signal wires are swapped. The serial monitor prints the module's own firmware
version when it does work: `Reader 1 on UART ready (PN532 fw 1.6) -> slot 1`.

**Stale bytes on the port** — worth knowing about the transport itself. HSU has
no framing to resynchronise on: the driver's serial read is a bare `readBytes()`,
so whatever is waiting in the buffer is taken as the reply. `readPassiveTargetID()`
also returns the moment its ACK wait times out, without draining what the module
sends afterwards — and on an idle antenna that path runs every poll. Left alone,
the leftovers shift every later read and the reader looks like it has stopped
answering when it is perfectly fine.
The firmware drains the port at the top of every poll cycle, which is why you should be
on 1.9.4 or later before drawing conclusions from a reset counter.

**The reader stops answering, or the reset counter climbs** — the module has gone
quiet and the firmware has closed the port, pulsed RSTO and re-initialised it (see
**Reader resets** below). On HSU that recovery works and the reader comes back;
there is no driver state left wedged behind it. What makes it necessary in the
first place is physical, and it's nearly always one of:

- **Wires too long.** Dupont jumpers over ~15 cm are marginal for this module
  whatever is running over them. Keep the two signal wires under 10 cm, or move to
  a short ribbon.
- **Brown-out when WiFi transmits.** The C5's TX bursts pull tens of mA. If the PN532
  shares a thin USB lead or a long 3V3 run, its rail sags and it drops mid-byte. A
  100 µF electrolytic plus a 0.1 µF ceramic across the PN532's 3V3/GND fixes this, and
  it's the single most common cause of "works for a minute, then dies".

  **Check the timestamp.** The board pings the printer and Spoolman once a minute,
  and the first one lands at t≈60 s. If your failures cluster on that cadence —
  60 s, 120 s, 180 s — the ping's HTTP round-trip is the TX burst that's tipping the
  rail over, and it's a power problem, not a signal-integrity one. The reset counter
  makes this easy to see: if it climbs roughly once a minute, that's your answer.

  If the boxes are already assembled and you'd rather not resolder eight of them,
  **Settings → WiFi TX power** turns the radio down (try 13 dBm). It applies live,
  no reboot, so you can bisect a misbehaving box from the UI. It cuts the current
  spike rather than the sag itself, so it's a mitigation, not a cure — and only
  use it on a box with RSSI to spare.
- **RSTO not wired.** Recovery hard-resets the PN532 through it, and on HSU that
  is the whole of the recovery — there is no bus to cycle instead. Without it a
  module that has stopped answering may well stay that way until you power-cycle
  the box.

**Settling whether it is power or wiring** — the web UI's **Reader diagnostics**
card runs a five-minute test with the radio switched off, then reboots back to
normal by itself. Watch it on the USB serial console: the board is deliberately
off the network for the whole run, and it prints its verdict there.

- No reader errors in five minutes → WiFi transmit current is browning out the
  PN532. Fit the capacitors, or turn TX power down in Settings.
- Errors anyway → the wiring is at fault. Shorten the two signal wires.

Note the board reboots at both ends of the test, so the reset counter is zero
when it starts and zero again afterwards — read the result from the serial log,
not from the counter.

**Reader resets** — the Reader pill turns amber and reads `Reader (n resets)` once
the firmware has had to reset the module. It's still working; the count is telling
you the wiring in that box is marginal and wants the capacitor treatment above. A box that
climbs into double digits over a day is one bad connection away from dropping out.

**A reader that failed at boot comes back on its own** — the firmware retries a downed
reader every 30 seconds, so re-seating a connector fixes it without a reboot or a trip
to the drybox.

**Tag reads but send fails with 404** — you're on stock Snapmaker firmware; see the
warning at the top.

**Send fails with 401** — Moonraker wants an API key. Put it in Settings.

**Spoolman won't connect** — press **Ping** and read the Activity log; the firmware names
the reason rather than just showing a red dot.

| Message | Means |
|---|---|
| `Spoolman host is set but "Use Spoolman" is unticked` | Tick the checkbox. Typing a host does not enable it. |
| `no Spoolman host set` | Host field is empty. |
| `no WiFi` | The box isn't associated; Spoolman is not the problem. |
| `HTTP error connection refused` | Right host, nothing listening on that port. |
| `HTTP error connection lost` / timeout | Wrong host, firewalled, or a different subnet. |
| `Spoolman returned HTTP 404` | Reached *something*, but not Spoolman's API — usually a reverse proxy serving a path prefix. |

Things worth checking, in order:

- **The "Use Spoolman" checkbox.** Setting a host does not switch it on by itself.
- **Port 7912** is Spoolman's default. A Docker run with a mapped port, or a reverse proxy,
  will differ.
- **Use the IP, not a `.local` name.** mDNS resolution from the ESP32's HTTP client is
  unreliable; the printer host has the same caveat.
- **Paste anything you like into the host field** — from 1.9.7 a scheme, a path, and a
  `:port` are all stripped out, and a pasted port is moved into the port field. Earlier
  builds stored it raw, which quietly produced `http://http://192.168.1.20:7912`.

Two real limitations, not bugs to work around:

- **Plain HTTP only.** The client builds `http://host:port`. A Spoolman behind an HTTPS
  reverse proxy or Home Assistant ingress cannot be reached.
- **No path prefix.** Requests go to `/api/v1/...` at the root, so a proxy that serves
  Spoolman under `/spoolman` will 404. Point the box at the container directly instead.

**Bambu tag reads as "Unrecognised"** — some newer Bambu spools use FM11RF08S chips
with a backdoor-key variant that this simple reader doesn't handle. The UI still
gives you the UID, and you can fill the material in by hand.

**Nothing happens when I re-present the same spool** — that's on purpose. Lift the
tag off the reader and put it back, or press **Re-scan tag**.

**I scan a spool and nothing is sent** — expected in the default on-load mode. The
line under the slot buttons says what it's waiting for. Switch to *Send on every
scan* for the old behaviour.

**The slot dots never light up** — the bridge can't read `print_task_config` from the
printer. Stock firmware may not expose it; check the printer host and that Moonraker
answers `curl 'http://<host>/printer/objects/query?print_task_config'`.

**A spool gets assigned to the wrong slot** — in on-load mode the spool goes to
whichever slot the printer reports filling next. If you scan spool A, then physically
load spool B, B's slot gets A's data. Scan the spool you're about to load, not the
one you just took out.

**Set 5 GHz but the pill says 2.4 GHz, especially after a power cycle** — fixed in
1.10.1; if you are on an earlier build this is a firmware bug, not your network.
`WiFi.setBandMode()` refuses with *"You need to start WiFi first"* until the radio's
`STA_START` event has been processed, and that event is delivered asynchronously on the
event task. Earlier builds called it immediately after `WiFi.mode(WIFI_STA)` and ignored
the `false`. A cold power-on is slower to reach that point than a soft reset, so the race
was usually lost exactly then — the band mode was never applied, the radio stayed on AUTO,
and it associated on whichever band the AP offered first, normally 2.4 GHz. Now it waits up
to 600 ms for the radio to accept the setting. (`setTxPower()` shares the same precondition
and was fixed the same way in 1.9.5.)

If it still lands on 2.4 GHz on 1.10.1, the fallback is genuine: the join on 5 GHz really
failed, the pill will say so, and the usual cause is that the AP has no 5 GHz radio for
that SSID or is on a DFS channel the C5's regulatory domain won't use.

## Sources

- [SnapmakerU1-Extended-Firmware — filament_detect design doc](https://github.com/paxx12/SnapmakerU1-Extended-Firmware/blob/develop/docs/design/filament_detect.md)
- [Custom Snapmaker U1 Firmware — RFID filament tag support](https://snapmakeru1-extended-firmware.pages.dev/rfid_support)
- [DnG-Crafts/U1-RFID — Snapmaker U1 RFID programming](https://github.com/DnG-Crafts/U1-RFID)
- [spuder/OpenSpool](https://github.com/spuder/OpenSpool)
- [Bambu-Research-Group/RFID-Tag-Guide](https://github.com/Bambu-Research-Group/RFID-Tag-Guide)
- [QIDI Box RFID specification](https://wiki.qidi3d.com/en/QIDIBOX/RFID)
- [TinkerBarn/BoxRFID-Touch — prior art, ESP32 + PN532 for U1 tags](https://github.com/TinkerBarn/BoxRFID-Touch)

## Licence

MIT — see [LICENSE](LICENSE). Use it, change it, sell it; keep the copyright
notice. The reverse-engineered tag formats here are used for reading spools you
already own.

That covers the code in this repository, which is all first-party. A **compiled
image is a different matter**: it statically links AsyncTCP and
ESPAsyncWebServer (both **LGPL-3.0**) and the arduino-esp32 core
(**LGPL-2.1**). That does not affect how this source is licensed, but anyone
handing someone else a `.bin` is distributing LGPL code and takes on those
obligations.

Building it for your own dryboxes is not distribution and none of it applies to
you. If you do redistribute a binary, see
[THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md) — it is the required notice,
it explains how this project satisfies the relinking clause, and
[`licenses/`](licenses/) holds the texts.

Not legal advice. If you intend to ship this commercially, read the licences
yourself.
