u1-spool-bridge — Arduino IDE sketch
====================================

Full guide: docs/ARDUINO-IDE.md in the project root.

Three things you must do, in this order:

1. INSTALL THE LIBRARIES  (Tools > Manage Libraries, install by exact name)
       Adafruit PN532
       Adafruit BusIO
       ArduinoJson              (by Benoit Blanchon, version 7.x)
       Async TCP                (by ESP32Async)
       ESP Async WebServer      (by ESP32Async)

   Careful with the last two — several similarly-named forks exist. The ones
   you want are published by "ESP32Async".

2. SET THE PARTITION SCHEME
       Tools > Partition Scheme > "Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)"

   THIS IS NOT OPTIONAL. The IDE defaults this board to a 4 MB scheme with a
   1.25 MB app slot. This firmware is about 1.44 MB, so the default fails with
   "Sketch too big". Minimal SPIFFS gives 1.9 MB and keeps OTA working.

   Also check Tools > USB CDC On Boot is "Enabled" (it should be by default).
   The XIAO has no USB-to-UART chip, so with CDC disabled you get no serial
   output at all.

3. EDIT config.h
   Near the top there's a block marked "ARDUINO IDE USERS". Uncomment the
   DEFAULT_WIFI_SSID / DEFAULT_WIFI_PASS lines and fill in your network.
   Otherwise the board comes up as its own access point and you configure it
   from a phone.

Pin defaults are already the XIAO ESP32-C5's pads:
    PN532 SDA pad -> D4      PN532 RSTO -> D2
    PN532 SCL pad -> D5      PN532 IRQ  -> unused
    PN532 VCC     -> 3V3     PN532 GND  -> GND
Set BOTH of the PN532's DIP switches to OFF. That is HSU, and it is the only
transport this firmware speaks.

  The pads keep their I2C names because that is what the module prints on
  them. The Elechouse V3 and its clones put HSU on the same header pins --
  I2C labels on the front of the board, HSU labels on the back -- with the
  SDA pad carrying the module's TX and the SCL pad its RX. The crossover
  happens in the firmware's pin assignment, not in your wiring, so nothing
  needs rerouting. RSTO must stay connected: the driver pulses it on every
  init and it is the only recovery lever there is.

  Nothing to uncomment, no transport to choose. If your wiring differs, the
  overrides are PIN_PN532_RX / PIN_PN532_TX / PIN_PN532_RST in config.h.

  Also fit 100uF + 0.1uF across the PN532's VCC/GND. See docs/capacitors.html.

u1_spool_bridge.ino is intentionally empty — setup() and loop() are in
main.cpp. The IDE only needs a .ino named after the folder.

NOTE: this folder is a copy of the project's src/ and include/ directories.
Editing files here does not change the PlatformIO project, and vice versa.
