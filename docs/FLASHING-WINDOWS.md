# Flashing from Windows 11

The same job as [FLASHING.md](FLASHING.md), written for Windows. Three things
differ enough to be worth their own guide: PowerShell's `curl` isn't curl,
PlatformIO wants a short folder path, and there's a PATH trap after installing it.

Everything below is **PowerShell** (the blue one, or Windows Terminal). Where
`cmd.exe` differs I've said so.

---


> **Before the first board:** set both PN532 DIP switches off and fit the
> capacitors. Both switches off is HSU (UART), which is the only transport the
> firmware speaks — the wiring is the same either way, so this is just two
> switches. And every module wants a 100 µF + 0.1 µF pair across its VCC/GND.
> Both are covered in the README and in `docs/capacitors.html`; doing them now is
> much less work than opening eight sealed boxes later.

## 1. Install Python

Get it from [python.org/downloads](https://www.python.org/downloads/) — **not**
the Microsoft Store version, which sandboxes paths in ways PlatformIO trips over.

In the installer, tick **"Add python.exe to PATH"** on the first screen. It's easy
to miss and everything else depends on it.

Check:

```powershell
python --version
pip --version
```

---

## 2. Install PlatformIO Core

```powershell
pip install platformio
```

Now the trap. pip installs `pio.exe` into a `Scripts` folder that often isn't on
PATH, so this may fail even though the install worked:

```powershell
pio --version
```

If you get *"The term 'pio' is not recognized"*, either close and reopen the
terminal (PATH changes don't apply to already-open windows), or just use the
module form everywhere instead — it always works:

```powershell
py -m platformio --version
```

Every `pio ...` command below can be written `py -m platformio ...`.

---

## 3. Unzip somewhere short

Extract the project to something like:

```
C:\u1-spool-bridge
```

Not Downloads, not a OneDrive folder, not a path with spaces. PlatformIO's
toolchain paths are long, and Windows' 260-character path limit produces
baffling compiler errors when the project starts deep in a tree. OneDrive is
worse — it can sync build artifacts mid-compile.

---

## 4. Plug in a board

The XIAO ESP32-C5 has one USB-C port wired to the chip's native USB.
**No drivers needed** — Windows 11 binds its built-in USB serial driver.

Open Device Manager (Win+X, then M) and look under **Ports (COM & LPT)**. You
want something like:

```
USB Serial Device (COM7)
```

Or ask PlatformIO:

```powershell
pio device list
```

`VID:PID=2886:0067` confirms it's a XIAO.

**Nothing appears?** Almost always a charge-only USB-C cable — try a different
one before anything else. If a cable you trust still doesn't work, put the board
into download mode by hand: hold **B**, tap **R**, release **B**. The port will
appear and stay until the next reset.

---

## 5. Bake in your WiFi

Open `C:\u1-spool-bridge\platformio.ini` in Notepad (or VS Code). Find the
`[common]` section and add three lines to `build_flags`:

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

The quoting is `'"like this"'` — a single quote, a double quote, your text, then
the same in reverse. **This is not shell quoting and it does not change on
Windows**; PlatformIO parses this file itself, the same way on every OS. It
handles passwords containing spaces correctly.

Do this before flashing anything. Without it, every board comes up as its own
WiFi access point and you'd be joining `U1-SpoolBridge` from your phone eight
separate times.

Leave `DEFAULT_PRINTER_HOST` out — it differs between your two printers and is
set per box in step 8.

---

## 6. Flash the first board

```powershell
cd C:\u1-spool-bridge
pio run -t upload
```

The first run downloads the ESP32 toolchain — several hundred MB, once, not once
per board. Later boards take about a minute.

If several boards are plugged in, name the port:

```powershell
pio run -t upload --upload-port COM7
```

A good run ends with `Hash of data verified.` and `Leaving... Hard resetting`.

**`Failed to connect to ESP32-C5`** → hold **B**, tap **R**, release **B**, run
it again.

**`could not open port 'COM7': Access is denied`** → something else has the port.
A serial monitor still running in another tab is the usual culprit; Arduino IDE
also holds ports open.

---

## 7. Watch it come up

```powershell
pio device monitor -p COM7 -b 115200
```

```
=== u1-spool-bridge 1.9.2 ===
Box "Box A3F2" -> slot 1 on (unset) (http://u1-box-a3f2.local/)
Reader 1 on UART ready (PN532 fw 1.6) -> slot 1
Joining YourNetwork....
Connected on 5 GHz (RSSI -58 dBm): http://192.168.1.79/  (http://u1-box-a3f2.local/)
Web UI up on port 80.
OTA ready on u1-box-a3f2.local (no password)
```

**Write down the `Box XXXX` name and the IP.** The name comes from the board's
own MAC, so all eight are different and you can flash them all before configuring
any of them.

`Ctrl+C` exits the monitor. Do that before flashing again, or step 6 will hit the
"Access is denied" error above.

---

## 8. Name it and bind its slot

**Do not use `curl` in PowerShell.** `curl` there is an alias for
`Invoke-WebRequest`, which takes completely different arguments — you'll get a
confusing parameter error. Use PowerShell's own tool:

```powershell
$body = @{
    boxName        = "Drybox 1"
    defaultChannel = 0
    printerHost    = "192.168.1.42"
} | ConvertTo-Json

Invoke-RestMethod "http://u1-box-a3f2.local/api/settings" `
    -Method Post -ContentType "application/json" -Body $body
```

`defaultChannel` is **0-based**: 0,1,2,3 are slots 1,2,3,4 in the UI.

If you'd rather use real curl, it ships with Windows as `curl.exe` — the `.exe`
matters, and in `cmd.exe` (not PowerShell) the quoting is:

```cmd
curl.exe -X POST http://u1-box-a3f2.local/api/settings -H "Content-Type: application/json" -d "{\"boxName\":\"Drybox 1\",\"defaultChannel\":0,\"printerHost\":\"192.168.1.42\"}"
```

Renaming also changes the mDNS name, so this box is now `u1-drybox-1.local`.
Open `http://u1-drybox-1.local/` and check the header reads `Drybox 1 → slot 1`.

**If `.local` addresses don't resolve**, test with `ping u1-drybox-1.local`.
Windows 11 usually handles mDNS natively; when it doesn't, either install Apple's
[Bonjour Print Services](https://support.apple.com/kb/DL999) or just use the IP
address the serial monitor printed.

---

## 9. The other seven

Flash all of them first — each answers to its own MAC-derived name, so they don't
collide — then configure them in one go.

Repeat flashing doesn't need a full rebuild. Point `--upload-port` at each new
COM port:

```powershell
pio run -t upload --upload-port COM8
pio run -t upload --upload-port COM9
```

Windows assigns a new COM number per board, so re-run `pio device list` between
boards, or watch Device Manager.

Then configure all eight at once. Edit the names and hostnames to match what the
serial monitor showed you:

```powershell
$boxes = @(
    @{ host="u1-box-a3f2"; name="Drybox 1"; slot=0; printer="192.168.1.42" }
    @{ host="u1-box-b17c"; name="Drybox 2"; slot=1; printer="192.168.1.42" }
    @{ host="u1-box-4d91"; name="Drybox 3"; slot=2; printer="192.168.1.42" }
    @{ host="u1-box-0d51"; name="Drybox 4"; slot=3; printer="192.168.1.42" }
    @{ host="u1-box-77ae"; name="Drybox 5"; slot=0; printer="192.168.1.43" }
    @{ host="u1-box-91b2"; name="Drybox 6"; slot=1; printer="192.168.1.43" }
    @{ host="u1-box-3c8f"; name="Drybox 7"; slot=2; printer="192.168.1.43" }
    @{ host="u1-box-e604"; name="Drybox 8"; slot=3; printer="192.168.1.43" }
)

foreach ($b in $boxes) {
    $body = @{
        boxName        = $b.name
        defaultChannel = $b.slot
        printerHost    = $b.printer
    } | ConvertTo-Json
    try {
        Invoke-RestMethod "http://$($b.host).local/api/settings" `
            -Method Post -ContentType "application/json" -Body $body | Out-Null
        Write-Host "configured $($b.name)" -ForegroundColor Green
    } catch {
        Write-Host "FAILED $($b.name) — $($_.Exception.Message)" -ForegroundColor Red
    }
}
```

Finally, open any box's web UI and press **Refresh** under *Other boxes*. All
eight should appear. That's your proof the fleet is up.

---

## 10. Updating later — no USB

Once a box is running this firmware you never need the cable again.

```powershell
pio run -e seeed_xiao_esp32c5-ota -t upload --upload-port u1-drybox-3.local
```

All eight:

```powershell
1..8 | ForEach-Object {
    Write-Host "== Drybox $_" -ForegroundColor Cyan
    pio run -e seeed_xiao_esp32c5-ota -t upload --upload-port "u1-drybox-$_.local"
}
```

Update one box first and watch it for a minute before looping the rest — OTA
protects you from a *failed* upload, but not from firmware that installs fine and
then misbehaves.

You can also drag `.pio\build\seeed_xiao_esp32c5\firmware.bin` onto the
**Firmware** card in any box's web UI. That takes `firmware.bin`, not
`firmware.factory.bin`.

A failed or interrupted OTA leaves the box running the firmware it already had,
and settings survive either way. See
[FLASHING.md](FLASHING.md#updating-over-the-air) for the details and how to set an
OTA password — it's open on your LAN by default.

---

## Windows-specific troubleshooting

**`pio` is not recognized**
PATH. Reopen the terminal, or use `py -m platformio` instead — same thing.

**Compiler errors mentioning paths, or `No such file or directory` on files that
plainly exist**
The 260-character path limit. Move the project to `C:\u1-spool-bridge`.

**Builds take several minutes every time**
Microsoft Defender scanning thousands of small build files. Add exclusions for
`C:\u1-spool-bridge` and `%USERPROFILE%\.platformio` under Windows Security →
Virus & threat protection → Manage settings → Exclusions.

**`Invoke-WebRequest: A parameter cannot be found that matches parameter name 'X'`**
You used `curl` in PowerShell and got the alias. Use `Invoke-RestMethod`, or spell
it `curl.exe`.

**COM port number changes between boards**
Normal — Windows assigns one per device. `pio device list` between boards.

**`Access is denied` on a COM port**
A serial monitor or IDE still has it open. Close it.

**The board shows in Device Manager as "Unknown device" or with a warning
triangle**
Almost always the cable. If a known-good cable doesn't fix it, the board may be
stuck — hold **B**, tap **R**, release **B**.

**OneDrive keeps touching the project folder**
Move it outside your OneDrive tree. Sync during a build causes random failures.

---

## Sources

- [PlatformIO Core installation](https://docs.platformio.org/en/latest/core/installation/index.html)
- [Seeed XIAO ESP32-C5 getting started](https://wiki.seeedstudio.com/xiao_esp32c5_getting_started/)
- [about_Aliases — PowerShell's `curl` alias](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_aliases)
