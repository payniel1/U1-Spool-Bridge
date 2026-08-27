# Building with the Arduino IDE

The project ships an Arduino-ready copy of itself at
**`arduino/u1_spool_bridge/`**. Open that folder's `.ino` and the IDE will
compile the whole firmware.

Worth knowing before you start: this is a genuine alternative, not a downgrade —
same source, same compiler, same core. What you give up is the per-board pin
configuration (the IDE has nowhere to put it, so the sketch is hard-wired to the
XIAO's pads) and the automated eight-board workflow. If you're doing all eight,
[FLASHING.md](FLASHING.md) with PlatformIO is genuinely less work. For one or two
boards, or if you already live in the Arduino IDE, this is fine.

**Two settings decide whether this works at all**, and one of them is not the
default. They're in step 5.

---

## 1. Install the Arduino IDE

Version 2.x from [arduino.cc/en/software](https://www.arduino.cc/en/software).

## 2. Add the ESP32 boards

**File → Preferences → Additional Board Manager URLs**, paste:

```
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

Then **Tools → Board → Boards Manager**, search `esp32`, install the Espressif
Systems package.

You need **version 3.3.0 or newer** — the ESP32-C5 didn't exist in the core
before that. If `XIAO_ESP32C5` doesn't appear in the board list later, this is
why: update the package.

## 3. Install the libraries

**Tools → Manage Libraries**, then install these five by exact name:

| Library | Author | Notes |
|---|---|---|
| Adafruit PN532 | Adafruit | The reader driver |
| Adafruit BusIO | Adafruit | Usually offered automatically with the above — accept |
| ArduinoJson | Benoit Blanchon | **Version 7.x.** 6.x will not compile |
| Async TCP | **ESP32Async** | |
| ESP Async WebServer | **ESP32Async** | |

Take care with the last two. Several forks share those names (me-no-dev,
dvarrel, others) and they are not interchangeable — the ones that work here are
published by **ESP32Async**. Check the author line in the Library Manager entry
before clicking Install.

## 4. Open the sketch

**File → Open**, navigate to `arduino/u1_spool_bridge/` and open
`u1_spool_bridge.ino`.

You'll see about 25 tabs. `u1_spool_bridge.ino` is deliberately empty — `setup()`
and `loop()` live in `main.cpp`. The IDE only needs a `.ino` named after the
folder in order to open the sketch; it compiles every `.cpp` beside it.

## 5. The two settings that matter

**Tools → Board → esp32 → XIAO_ESP32C5**

Then, and this is the one people lose an hour to:

**Tools → Partition Scheme → `Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)`**

The IDE defaults this board to *Default 4MB with spiffs*, which allows a 1.25 MB
application. The firmware is about 1.36 MB, so the default fails at the very end
of a long compile with:

```
Sketch too big; see https://support.arduino.cc/... for tips on reducing it.
text section exceeds available space in board
```

*Minimal SPIFFS* gives 1.9 MB and keeps two OTA slots, so network updates still
work. *Huge APP* also fits but has no second slot — pick it only if you don't
want OTA.

**Tools → USB CDC On Boot → `Enabled`** (it already is by default). The XIAO has
no USB-to-UART chip, so with this disabled the Serial Monitor stays blank and
you'll think the board is dead.

Leave everything else alone. Flash Size is already 8MB and there's only one
option.

## 6. Put your WiFi in

Click the **config.h** tab. Near the top:

```cpp
// ===========================================================================
// ARDUINO IDE USERS: put your network here, then save.
//
//   #define DEFAULT_WIFI_SSID     "YourNetwork"
//   #define DEFAULT_WIFI_PASS     "YourPassword"
//   #define DEFAULT_SPOOLMAN_HOST "192.168.1.20"
```

Uncomment the lines you want and fill them in — ordinary C string literals, no
special quoting.

You can skip this entirely; the board will come up as its own access point
(`U1-SpoolBridge`, password `spoolbridge`) and you configure it at
`http://192.168.4.1/` from a phone. That's fine for one board and tedious for
eight.

## 7. Wiring

Already the defaults, by the XIAO's silkscreen:

| PN532 | XIAO pad | Carries |
|---|---|---|
| SDA | **D4** | board RX ← the module's TX |
| SCL | **D5** | board TX → the module's RX |
| RSTO | **D2** | reset — required |
| IRQ | **D0** | unused; harmless to leave connected |
| VCC | **3V3** | |
| GND | **GND** | |

To change the pins, edit the `PIN_PN532_RX` / `_TX` / `_RST` defines in
`config.h`.

### Set both DIP switches off

That is HSU (UART), and it is the only transport the firmware has — there is
nothing to pick and no `#define` to uncomment. The pads keep their I2C names
because that is what the module prints on them: the Elechouse V3 and its clones
put HSU on the same header pins, with the I2C labels on the front of the board
and the HSU labels on the back, the SDA pad being the module's TX and SCL its RX.
The firmware assigns RX/TX to match, so the crossover is in the pin map, not the
loom.

Leave RSTO connected. The driver pulses it on every init, and it is how the
firmware recovers a module that has stopped answering.

This does not remove the need for the decoupling capacitors; see
[capacitors.html](capacitors.html).

## 8. Upload

Plug the board in — one USB-C port, no drivers needed on Windows 11, macOS or
Linux. Pick it under **Tools → Port** (it enumerates as `2886:0067`).

Press **Upload**. First compile takes a few minutes; later ones are quicker.

If it can't connect, put the board in download mode by hand: hold **B**, tap
**R**, release **B**, then Upload again.

## 9. Watch it boot

**Tools → Serial Monitor**, set the baud dropdown to **115200**:

```
=== u1-spool-bridge 1.9.2 ===
Box "Box A3F2" -> slot 1 on (unset) (http://u1-box-a3f2.local/)
Reader 1 on UART ready (PN532 fw 1.6) -> slot 1
Joining YourNetwork....
Connected on 5 GHz (RSSI -58 dBm): http://192.168.1.79/  (http://u1-box-a3f2.local/)
OTA ready on u1-box-a3f2.local (no password)
Web UI up on port 80.
```

`Reader 1 init failed` means the PN532 isn't answering — check that both DIP
switches are off and that the two signal wires aren't swapped. Everything else still runs, so you can fix the
wiring without reflashing.

Note the `Box XXXX` name; it comes from the board's own MAC, so every board gets
a different one.

## 10. Name it and point it at a printer

From any terminal on the same network:

```bash
curl -X POST http://u1-box-a3f2.local/api/settings \
  -H 'Content-Type: application/json' \
  -d '{"boxName":"Drybox 1","defaultChannel":0,"printerHost":"192.168.1.42"}'
```

On Windows PowerShell use `Invoke-RestMethod` instead — see
[FLASHING-WINDOWS.md](FLASHING-WINDOWS.md#8-name-it-and-bind-its-slot).

Or do it in the web UI: open `http://u1-box-a3f2.local/`, expand **Settings**,
fill in Box name and Printer host, click a slot button, Save.

`defaultChannel` is 0-based: 0,1,2,3 are slots 1–4.

## 11. Updating later

Because you chose a partition scheme with OTA, later updates don't need the
cable. The easiest route from the Arduino IDE:

**Sketch → Export Compiled Binary** (Ctrl+Alt+S), which writes a `.bin` into
`arduino/u1_spool_bridge/build/`. Then open the box's web UI, find the
**Firmware** card, and upload that file. Take the plain
`u1_spool_bridge.ino.bin` — **not** `...merged.bin` or `...bootloader.bin`.

The Arduino IDE can also upload over the network directly: with the board on
your LAN, its `u1-drybox-1.local` entry appears under **Tools → Port** as a
network port. Selecting it and pressing Upload does an espota push. This is
occasionally flaky in IDE 2.x; the web upload is more predictable.

---

## Differences from the PlatformIO build

Not problems, just things that differ:

| | PlatformIO | Arduino IDE |
|---|---|---|
| App partition | 3.19 MB (`default_8MB`) | 1.88 MB (`min_spiffs`) |
| Firmware size | 1.36 MB (43%) | 1.36 MB (72%) |
| Warnings | `-Wall -Wextra` | core defaults |
| Pin config | per board in `platformio.ini` | hard-wired to the XIAO in `config.h` |
| Unit tests | `pio test -e native` | not available |

Both layouts keep two OTA slots and put NVS at the same offset, so settings
survive either way. You can flash a box with one toolchain and OTA it with the
other later — the app fits both.

**The sketch folder is a copy.** `arduino/u1_spool_bridge/` holds duplicates of
`src/` and `include/`. Editing one does not change the other. If you pull a newer
version of the project and want it in the IDE:

```bash
cp src/*.cpp include/*.h arduino/u1_spool_bridge/
```

---

## Troubleshooting

**`XIAO_ESP32C5` isn't in the board list**
ESP32 core older than 3.3.0. Boards Manager → esp32 → Update.

**`Sketch too big` / `text section exceeds available space`**
Partition Scheme. Step 5.

**`fatal error: ArduinoJson.h: No such file or directory`**
Library not installed, or ArduinoJson 6.x is installed instead of 7.x.

**Compiles, but errors mentioning `AsyncWebServerRequest` or `AsyncTCP`**
You have one of the other forks. Uninstall it and install the **ESP32Async**
versions of both *Async TCP* and *ESP Async WebServer*.

**Serial Monitor is blank**
USB CDC On Boot is Disabled, or the baud isn't 115200. The XIAO has no UART
bridge, so CDC is the only way out.

**Upload fails to connect**
Hold **B**, tap **R**, release **B**, then Upload. Failing that, try another
USB-C cable — charge-only cables are the usual culprit.

**`A fatal error occurred: Packet content transfer stopped`**
Try a lower speed: Tools → Upload Speed → 115200.

**The reader drops out after a while**
The firmware closes the port, pulses RSTO and re-initialises the module on its
own, so it usually comes back — but the underlying cause is physical: shorten the
two signal wires, and put 100 µF + 0.1 µF across the PN532's 3V3 and GND. See the
Troubleshooting section of the README for the full list.

---

## Sources

- [Installing the Arduino ESP32 core](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html)
- [Seeed XIAO ESP32-C5 getting started](https://wiki.seeedstudio.com/xiao_esp32c5_getting_started/)
