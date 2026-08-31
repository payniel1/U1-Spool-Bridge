# Flashing the boards

Written for the eight-box setup: two Snapmaker U1s, four dryboxes each, one
**Seeed XIAO ESP32-C5** + PN532 per box. The single-board case is the same thing
done once.

The XIAO makes this easier than a devkit does: one USB-C port, so there's no
wrong port to pick, and 8 MB of flash.

On Windows 11? Use **[FLASHING-WINDOWS.md](FLASHING-WINDOWS.md)** instead —
PowerShell's `curl` isn't curl, and a few other things differ enough to matter.

**Do step 3 before you flash anything.** Baking your WiFi credentials into the
build is the difference between eight rounds of access-point setup and eight
one-line curl commands.

---

## 1. What you need

| | |
|---|---|
| Boards | Seeed XIAO ESP32-C5, one per drybox |
| Cable | USB-C **data** cable — a charge-only cable is the classic time sink |
| Computer | Linux, macOS or Windows. No drivers needed — the XIAO uses the ESP32-C5's native USB |

The XIAO has a **single USB-C port**, wired straight to the chip's native
USB-Serial-JTAG. It carries power, flashing and the serial log, and there's no
second port to get wrong. It enumerates as USB `2886:0067`.

Its two buttons are marked **B** (boot) and **R** (reset) and are small enough
to want a fingernail or a pen tip.

---

### Wiring, by the XIAO's silkscreen

| PN532 | XIAO pad | GPIO | Carries |
|---|---|---|---|
| SDA | **D4** | 23 | board RX ← the module's TX |
| SCL | **D5** | 24 | board TX → the module's RX |
| RSTO | **D2** | 25 | reset — required |
| IRQ | **D0** | 1 | unused; harmless to leave connected |
| VCC | **3V3** | — | |
| GND | **GND** | — | |

**Set both PN532 DIP switches OFF.** That is HSU (UART), which is the only
transport the firmware speaks. The pads keep their I2C names because that is what
the module prints on them — the Elechouse V3 and its clones put HSU on the same
header pins, I2C labels on the front and HSU labels on the back, with the SDA pad
being the module's TX and SCL its RX. The firmware assigns RX/TX to match, so
there is nothing to cross over in the loom.

Avoid D3, D6 and D7 if you rewire: D3 is GPIO7 (a strapping pin), D6/D7 are
UART0. The pads on the underside are JTAG.

---

## 2. Install PlatformIO

You only need the command-line core.

```bash
pip install platformio           # or: pipx install platformio
pio --version                    # expect 6.1.x or newer
```

**Linux only** — give yourself access to serial devices, then log out and back in:

```bash
sudo usermod -aG dialout $USER   # Debian/Ubuntu/Raspberry Pi OS
sudo usermod -aG uucp $USER      # Arch
```

Skipping this produces `Permission denied: '/dev/ttyACM0'` later.

The first build downloads the ESP32 toolchain (a few hundred MB). That happens
once, not once per board.

---

## 3. Bake in your WiFi

Unzip the project and open `platformio.ini`. Find the `[common]` section and add
your network to `build_flags`:

```ini
[common]
build_flags =
    -std=gnu++17
    -Wall
    -Wextra
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DCORE_DEBUG_LEVEL=2
    -DDEFAULT_WIFI_SSID='"YourNetwork"'
    -DDEFAULT_WIFI_PASS='"YourPassword"'
    -DDEFAULT_SPOOLMAN_HOST='"192.168.1.20"'
```

Mind the quoting: `'"like this"'` — single quotes wrapping double quotes. The
shell strips the singles, the compiler needs the doubles.

Leave the printer host out. It differs between your two machines, and it's one
of the two things you'll set per box anyway.

Every board flashed with this joins your network on first boot and is reachable
immediately. Without it, each board comes up as its own access point and you'd
be joining `U1-SpoolBridge` eight times from your phone.

---

## 4. Find the port

Plug in one board, native USB port, then:

```bash
pio device list
```

You're looking for:

| OS | Port |
|---|---|
| Linux | `/dev/ttyACM0` |
| macOS | `/dev/cu.usbmodem*` |
| Windows | `COM4` (varies) |

On the XIAO there's only the native port, so you'll see one entry per board —
`USB VID:PID=2886:0067` confirms it's a XIAO.

If nothing appears: bad cable (try another — this is the most common cause), or
the board needs manual download mode. Hold **B**, tap **R**, release **B**. The
port will appear and stay until the next reset.

---

## 5. Flash the first board

```bash
cd u1-spool-bridge
pio run -t upload
```

`seeed_xiao_esp32c5` is the default environment, so no `-e` needed. If you have
several boards plugged in at once, name the port explicitly:

```bash
pio run -t upload --upload-port /dev/ttyACM0
```

A good run ends with `Hash of data verified.` and `Leaving... Hard resetting`.

If it can't sync (`Failed to connect to ESP32-C5`), hold **B**, tap **R**,
release **B**, and re-run. Some USB hubs also refuse to negotiate
the 460800 baud default; a direct port fixes it, or lower it with
`--upload-speed 115200`.

---

## 6. Watch it come up

```bash
pio device monitor -p /dev/ttyACM0 -b 115200
```

You should see something like:

```
=== u1-spool-bridge 1.9.2 ===
Box "Box A3F2" -> slot 1 on (unset) (http://u1-box-a3f2.local/)
Reader 1 on UART ready (PN532 fw 1.6) -> slot 1
Joining YourNetwork....
Connected on 5 GHz (RSSI -58 dBm): http://192.168.1.79/  (http://u1-box-a3f2.local/)
Web UI up on port 80.
```

**Write down that `Box XXXX` name** — it's derived from the board's own MAC, so
every board gets a different one and you can flash all eight before configuring
any of them. If instead you see `Reader 1 init failed`, the PN532 isn't
answering: check the wiring and that **both** DIP switches are off. The firmware
runs fine without it — you can fix the wiring later
without re-flashing.

Ctrl+C to exit the monitor.

---

## 7. Name it and bind its slot

Two values per box: what it's called, and which printer slot it feeds.

```bash
curl -X POST http://u1-box-a3f2.local/api/settings \
  -H 'Content-Type: application/json' \
  -d '{"boxName":"Drybox 1","defaultChannel":0,"printerHost":"192.168.1.42"}'
```

`defaultChannel` is **0-based** — 0,1,2,3 are slots 1,2,3,4 in the UI. Renaming
also changes the mDNS name, so this box becomes `u1-drybox-1.local`.

Then open `http://u1-drybox-1.local/` and check the header reads
`Drybox 1 → slot 1`.

If `.local` names don't resolve on your machine (some Windows setups, some
corporate networks), use the IP the serial monitor printed.

---

## 8. The other seven

Flash them all first, configure afterwards — each answers to its own MAC-derived
name, so they don't collide.

For repeat flashing you don't need to re-run the whole build. The first build
produced a single combined image:

```
.pio/build/seeed_xiao_esp32c5/firmware.factory.bin
```

Bootloader, partition table and app in one file, flashed at offset `0x0`:

```bash
# XIAO ESP32-C5
esptool --chip esp32c5 --port /dev/ttyACM0 --baud 921600 \
        write-flash 0x0 .pio/build/seeed_xiao_esp32c5/firmware.factory.bin

# XIAO ESP32-C3
esptool --chip esp32c3 --port /dev/ttyACM0 --baud 921600 \
        write-flash 0x0 .pio/build/seeed_xiao_esp32c3/firmware.factory.bin
```

**Always `0x0`, on both chips** — and that is the point of the merged image.
The bootloader does not live in the same place on the two parts: it starts at
`0x2000` on the C5 and at `0x0` on the C3. The factory image is padded so each
piece lands where its chip expects it, so you never have to know that. Flashing
the *plain* `firmware.bin` at `0x10000` instead only works on a chip that
already has a bootloader and partition table on it.

(esptool v4 and older spell it `write_flash`.) This is faster than `pio run`,
and it's the file to copy to another machine — that machine needs only
`pip install esptool`, not the whole toolchain.

Then the eight one-liners. Left printer:

```bash
curl -X POST http://u1-box-a3f2.local/api/settings -H 'Content-Type: application/json' \
  -d '{"boxName":"Drybox 1","defaultChannel":0,"printerHost":"192.168.1.42"}'
curl -X POST http://u1-box-b17c.local/api/settings -H 'Content-Type: application/json' \
  -d '{"boxName":"Drybox 2","defaultChannel":1,"printerHost":"192.168.1.42"}'
curl -X POST http://u1-box-4d91.local/api/settings -H 'Content-Type: application/json' \
  -d '{"boxName":"Drybox 3","defaultChannel":2,"printerHost":"192.168.1.42"}'
curl -X POST http://u1-box-0d51.local/api/settings -H 'Content-Type: application/json' \
  -d '{"boxName":"Drybox 4","defaultChannel":3,"printerHost":"192.168.1.42"}'
```

Right printer — same slots, different host:

```bash
curl -X POST http://u1-box-77ae.local/api/settings -H 'Content-Type: application/json' \
  -d '{"boxName":"Drybox 5","defaultChannel":0,"printerHost":"192.168.1.43"}'
# ...and so on for 6, 7, 8
```

For a **two-slot drybox** the two boards in it are just two ordinary nodes:

```bash
curl ... -d '{"boxName":"Drybox 4A","defaultChannel":2,"printerHost":"192.168.1.42"}'
curl ... -d '{"boxName":"Drybox 4B","defaultChannel":3,"printerHost":"192.168.1.42"}'
```

Finally, open any box's UI and hit **Refresh** under *Other boxes*. All eight
should be listed. That's your proof the fleet is up.

---

## 9. Updating a board later

**This distinction matters once a box is configured.**

| Command | Settings |
|---|---|
| `pio run -e … -t upload` | **Preserved.** Writes bootloader, partition table and app, and never touches the NVS region where settings live. |
| `esptool write-flash 0x0 firmware.factory.bin` | **Erased.** The combined image spans the NVS region and blanks it. The board comes back as `Box XXXX` with no name, slot or printer. |
| `pio run -t erase` | **Erased**, deliberately — for handing a board on or starting clean. |

So: `pio run -t upload` for updates, the factory image for first flash and
recovery. If you do wipe one by accident, re-run its curl line.

### Updating over the air

Once a box is running this firmware, later updates don't need USB.

**From the command line** — the fleet path:

```bash
pio run -e seeed_xiao_esp32c5-ota -t upload --upload-port u1-drybox-3.local
```

All eight, in one go:

```bash
for b in 1 2 3 4 5 6 7 8; do
  echo "== Drybox $b"
  pio run -e seeed_xiao_esp32c5-ota -t upload --upload-port u1-drybox-$b.local \
    || echo "   FAILED — still on the old firmware, retry later"
done
```

If you set an OTA password, add `--upload-flags --auth=YOURPASSWORD`.

**From the browser** — for a single box, or when you don't have the toolchain
to hand. Open the box's UI, find the **Firmware** card, pick
`.pio/build/seeed_xiao_esp32c5/firmware.bin` and press **Install**. A progress
bar runs, then the box reboots and the page reconnects on its own.

Note that OTA takes `firmware.bin` — the plain application image — not
`firmware.factory.bin`. The factory image includes a bootloader and partition
table, which is exactly what OTA must not replace.

### "Loaded in the printer"

This card shows what is in each of the printer's four slots. Two sources feed
it, and it says which one a row came from.

The printer's own readback is preferred, because it carries the temperatures
and the card UID. But the U1 only reports filament data for a slot **once that
filament is actually in the machine** &mdash; put a spool in a drybox and the
printer has nothing to say about it until the filament is pulled through, which
in practice is when a print starts.

So a slot the printer cannot describe falls back to the drybox's own reader,
marked **IN THE BOX**, showing which box it is in and how much is left, and
saying plainly what the printer reports for that slot. That is the answer to
"what is loaded" long before the printer is willing to confirm it.

The boxes are found by the fleet scan, so this fills in on its own within a few
seconds of opening the page; the slot the box you are on covers appears
immediately.

### The fleet view

The **Other boxes** card fills itself in. It asks when the page opens, again
every 30 s while the tab is visible, and stops asking when it is not — each
scan stalls that box's reader for as long as the mDNS query and the per-peer
fetches take, so the box refuses to run one more often than every 10 s however
often it is asked. The Refresh button still works if you are impatient.

Boxes are grouped by **the printer they feed**, which needs no setup: each box
already knows its printer. Your eight fall into two groups of four, each tile
showing its slot and what is loaded. The box you are looking at appears in its
own group too, marked *(this box)* — a group of four reads wrong with a hole
where you are standing.

**Renaming a group renames it everywhere.** Tap the group's name in the header,
type a new one, press Enter. The box you are on applies it to itself and then
writes it to every other box in that group, so you do not type the same label
into four Settings pages. The push happens box-to-box for the same reason the
firmware push does — a browser cannot POST settings to a box it was not served
from — and it sends only the group name, so nothing else on those boxes is
touched.

You can also set **Group** in a single box's Settings; either way it overrides
the printer for that box. Leave it blank and the printer is used.

**Spoolman.** Spoolman files each spool under a *location*, and groups its list
by that, so putting the group name in the location makes the two views agree.
The **Spoolman location** field understands three tokens:

| Token | Becomes |
| --- | --- |
| `{group}` | the box's group &mdash; or its box name if it has none, so ungrouped boxes do not all collapse into one blank location |
| `{slot}` | the slot number, 1&ndash;4 |
| `{box}` | the box name |

The default is `{group}` **on its own, with no slot**, and that is deliberate.
Spoolman groups its list by location, so the location string *is* the heading.
Put `{slot}` in it and every box gets a location of its own: eight dryboxes
become eight headings, which is the clutter the grouping was meant to remove.
Leave it out and the four boxes feeding one printer all file under `Printer A`,
which is one heading holding four spools. The preview under the field warns you
when a format would split a group up.

Two buttons sit under the field. **Use the group name** fills it in with
`{group}`; **Apply to every box** then pushes that format to the whole fleet,
so you set it once rather than in eight Settings pages. A preview shows exactly
what Spoolman will file under before you commit.

Whenever the location changes &mdash; a fleet-wide format rollout, or just
renaming a group &mdash; each affected box immediately re-files whatever it has
loaded, so Spoolman catches up instead of showing the old location until that
spool next happens to be reloaded. The re-file rewrites the location only; it
does not add another line to the spool's comment history, since the load it
would describe already happened.

Boxes already in service keep the format they have until you press **Apply to
every box**.

Each group header carries a summary (`3/4 loaded · 1 on older fw`), folds away
when you tap it — collapsed groups are remembered in that browser — and has its
own **Update** button, which runs a fleet update over just that group. Handy
when you want everything feeding one printer updated and the other left alone
mid-print. The box you are driving from is always included, because it is the
one that holds the image the others are fed from.

### Two chips, two images

There are two builds now, and they are not interchangeable:

| Board | Env | Image | Flash |
| --- | --- | --- | --- |
| Seeed XIAO ESP32-C5 | `seeed_xiao_esp32c5` | `firmware-c5.bin` | 8 MB, dual band |
| Seeed XIAO ESP32-C3 | `seeed_xiao_esp32c3` | `firmware-c3.bin` | 4 MB, 2.4 GHz only |

> **The released images are for the two XIAOs only.**
>
> Do not flash them to a devkit, even a C5 devkit — the chip id matches, so
> nothing will stop you.
>
> Two things are wrong with it. The **pins**: the release is built with the
> XIAO's GPIO 23/24/25, and a devkit's PN532 is on GPIO 0/1/10, so the reader
> is simply never found. And the **partition table**, which is the one that
> does damage. `firmware-c5-FACTORY.bin` carries the XIAO's 8 MB table:
>
> ```
> app0   app  ota_0  off=0x010000  size=0x330000
> app1   app  ota_1  off=0x340000  size=0x330000   <- ends at 8.00 MB
> ```
>
> A C5 devkit is a **4 MB** part. `app0` happens to end at 3.25 MB, so the box
> boots and looks perfectly healthy — and then the first OTA writes `app1` past
> the end of flash. The failure arrives weeks later, during a fleet update, on
> a box you had no reason to suspect.
>
> **For any devkit, build from source.** `pio run -e esp32-c5-devkitc-1 -t
> upload` sets the right pins and the right table, and the pre-build check
> refuses a table that does not fit the board.

Same wiring on both &mdash; D4, D5 and D2 &mdash; so a drybox harness moves
between them unmodified; only the GPIO numbers behind those pads differ, and
those are build flags. The C3 build must use `min_spiffs`: the app is ~1.41 MB
and the default 4 MB layout gives 1.25 MB OTA slots, so a default build has
nowhere to write an update to.

A C3 box has no 5 GHz radio (the band controls disappear from its web UI) and
no user LED (the variant defines neither `LED_BUILTIN` nor `RGB_BUILTIN`, so
the status light compiles out).

**A mixed fleet updates in two passes, one per chip.** The fleet updater reads
the chip id out of the image and blocks any box it does not match, so a C5
image offered to a C3 box is refused outright rather than flashed.

From **1.17.0** it also blocks a mismatch the chip id cannot see. A XIAO C5 and
a C5 DevKitC-1 are both `esp32c5`, and the XIAO build drives the reader on GPIO
23/24/25 where the devkit's is on 0/1/10 — so the image installs, reboots, and
the reader is silent. The build marker now carries that triple and the plan
refuses the combination, naming both. Both ends have to be on 1.17.0 for the
check to run; an older box reports no wiring and its row says so. Because the
box you are driving from is the one that holds the image for the others, you
have to run each pass from a box of that chip &mdash; press **Update all
boxes** from a C5 box for the C5 image, and from a C3 box for the C3 one. If
you try it the other way round the plan tells you so and will not let you
start.

### Updating the whole fleet at once

Eight boxes is seven repetitions too many. Pick the `.bin` in the **Firmware**
card of any one box and press **Update all boxes** instead of Install.

**The push happens from the box, not from your browser.** You upload the image
once, to the box whose page you are on; that box writes it to its spare app
slot, reads it back out and POSTs it to each of the others in turn, confirms
each one, and only then reboots into it itself.

That indirection is not decoration. A browser posting to a box it was not
served from makes a cross-origin request, and cross-origin is enforced by the
**receiving** box. Firmware older than 1.12.0 sends no CORS headers and has no
`OPTIONS` route, so its catch-all redirects the preflight and the browser
refuses to send the image at all — and those old boxes are exactly the ones an
update is for. A box talking to another box has no such rule. It also means
your phone uploads 1.5 MB once instead of once per box, which matters on a
patchy connection.

The box you drive from is therefore always part of the run and always last: it
is holding the image the others are fed from, so it cannot be unticked.

Before it sends anything, the browser reads the file and checks it against
every box it found:

| From the image | Checked against | If it disagrees |
| --- | --- | --- |
| chip id in the ESP32 header (`0x17` = C5, `0x0D` = C6) | the box's chip | **blocked**, and you cannot override it |
| `bus=` in the build marker | the box's transport | offered unticked — it would boot, but that box's reader would stop working |
| `rc=` in the build marker | how many readers the box drives | offered unticked, same reason |
| `pins=` in the build marker | which GPIOs the reader is on | **blocked** — same chip, different board |
| `fw=` in the build marker | the box's current version | unticked if it already has it |

The build marker is a string the firmware bakes into itself
(`U1SB-FINGERPRINT-v1|fw=…|tgt=…|bus=…|rc=…|pins=…|end`), so a file that is a valid
ESP32 image but belongs to some other project is refused outright.

Boxes running firmware older than 1.12.0 do not report their transport or chip
at all, so those three checks cannot run for them. Their row says so —
*"too old to report its build — check this is the uart image"* — rather than
leaving a blank that reads like a clean bill of health.

**"DONE" means the version changed.** Not that an upload was accepted. A box
reboots the moment the image verifies, so the reply to the upload usually never
arrives, and a box can also answer `ok` while still running the old firmware.
The pushing box asks each peer afterwards what it is now running, and only a
version that actually moved counts. Failures say which: `wrong OTA password`,
`still running 1.11.1`, `no answer after the upload`.

**A failure is not a brick.** Every box writes to its inactive slot and only
switches over on a verified image, so a box that fails an update is still
running exactly what it was before. Retry it, or Install to it directly.

**If your boxes have an OTA password**, put it in the OTA password field in
Settings before starting; it is used for every box in the run, so they all need
the same one.

### If an upload stops part-way

Two different things look the same from the browser and they need different
responses.

**Refused** — a specific message: wrong chip, wrong OTA password, OTA switched
off, image failed verification. That path cleans up after itself. Fix the
reason and press Install again; no reboot needed.

**Stalled** — the progress bar simply stops and the box goes quiet. The
connection died mid-image, so the final chunk never arrived. Nothing is
half-written (the image only becomes active after the whole thing verifies),
but the box parks itself: while an upload is in flight the main loop skips the
reader, the printer and any fleet work, and a retry is refused because the
write is still open.

From 1.16.1 the box gets itself out of that: if no bytes arrive for 45 seconds
it abandons the upload, logs *"upload stopped part-way — abandoned it, the box
is back to normal"*, and carries on. Wait a minute, then retry.

**On 1.16.0 and earlier there is no such timeout** — a stalled upload leaves
the box deaf until it is power-cycled. If you are updating a fleet off that
version, that is the one failure worth walking over for.

### What happens if an update goes wrong

The image is written to the **inactive** app slot, and the boot partition is
only switched after the whole thing has arrived and its checksum verifies. So:

- Upload interrupted, WiFi drops, power cut mid-transfer, corrupt file, wrong
  chip → the box carries on running the firmware it already had. Retry.
- Settings survive. OTA doesn't touch the NVS region.
- The box is unreachable for roughly 10–20 seconds while it writes and reboots.

The failure mode OTA can't protect you from is firmware that *installs
cleanly and then misbehaves* — a bad WiFi config, say. That still needs USB.
Which is an argument for updating one box first, watching it for a minute, and
only then looping over the other seven.

### Turning OTA off

It's on by default with no password, which suits a home LAN. Anyone on your
network can push firmware to these boards. To set a password:

```bash
curl -X POST http://u1-drybox-1.local/api/settings -H 'Content-Type: application/json' \
  -d '{"otaPassword":"something"}'
```

Or untick **Allow OTA updates** in Settings to disable it entirely — at the
cost of needing USB again. Sending `"otaPassword":"-"` clears a password.

---

## 10. When it goes wrong

**`Failed to connect to ESP32-C5: No serial data received`**
Manual download mode: hold **B**, tap **R**, release **B**, re-run.

**No port appears at all**
Charge-only USB cable, nine times out of ten. Try a different cable before
anything else. The XIAO has only one port, so that's not the variable here.

**`Permission denied: '/dev/ttyACM0'` (Linux)**
The `usermod` step in section 2, then log out and back in. `groups` should list
`dialout`.

**`A fatal error occurred: Packet content transfer stopped`**
Usually a hub or marginal cable. Plug into the machine directly, or add
`--upload-speed 115200`.

**Board flashes fine but never joins WiFi**
The credentials in section 3 didn't take, or the network is 5 GHz-only on an
SSID the board can't see. Check the serial log — it prints what it's joining.
The board falls back to an access point called `U1-SpoolBridge`
(password `spoolbridge`); join that and configure it at `http://192.168.4.1/`.

**`Reader 1 init failed: PN532 not answering on HSU`**
Both DIP switches must be off. Check the two signal wires aren't swapped — the
SDA pad is the module's TX and goes to the board's RX. Keep the dupont wires
short; the module gets unreliable past about 15 cm.

**`u1-box-xxxx.local` doesn't resolve**
mDNS isn't working on your machine or network. Use the IP address from the
serial log. On Linux, `sudo apt install avahi-daemon` usually fixes it.

**Wrong firmware on the wrong board**
No harm done — the C5 and C6 images aren't interchangeable, and esptool refuses
to flash a mismatched chip rather than bricking anything.

---

## Sources

- [Seeed XIAO ESP32-C5 getting started](https://wiki.seeedstudio.com/xiao_esp32c5_getting_started/) — pinout, buttons, flash size
- [PlatformIO CLI](https://docs.platformio.org/en/latest/core/userguide/index.html)
- [esptool documentation](https://docs.espressif.com/projects/esptool/en/latest/)
