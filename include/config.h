#pragma once

#if __has_include("soc/soc_caps.h")
#include "soc/soc_caps.h"
#endif

// ---------------------------------------------------------------------------
// The reader. One PN532 per board, on HSU (UART). That is the only transport
// this firmware has, deliberately.
//
// I2C was the original, and it is what the ESP_ERR_INVALID_STATE fault was:
// one corrupted transaction latches the IDF i2c_master driver, every later
// call fails identically, and the reader is gone until the box reboots. A UART
// has no such state — a glitched frame costs one read. SPI avoids the latch
// too but wants four signal wires on different pads and clocks at 1 MHz over
// the same unshielded dupont, which is a step backwards. Both are gone; what
// is left is the one that works.
//
// Neither transport removed the need for the decoupling capacitors.
//
// WIRING: unchanged from an I2C build, and that is not a coincidence. The
// Elechouse V3 and its clones put HSU on the SAME header pins as I2C — I2C
// labels on the front of the board, HSU labels on the back. The SDA pad is the
// module's TX, the SCL pad is its RX, so the wire already running to SDA
// carries transmit and we make that GPIO our RX. The crossover happens in the
// pin assignment below, not in the loom.
//
// Both DIP switches OFF. RSTO must stay connected — the driver pulses it on
// every init, and it is the only recovery lever there is. IRQ goes unused;
// leaving it connected is harmless.
// ---------------------------------------------------------------------------

// MAX_READERS is a STORAGE constant, not a capability. Settings::readerChannel
// is an array of this size sitting in the middle of a struct that is persisted
// to NVS as raw bytes, so changing it shifts every field after it and every
// deployed box reads its neighbours' bytes as its own — see the layout guard
// in settings.h. It stays at 2 even though only one reader is ever built.
#define MAX_READERS  2
#define READER_COUNT 1

// Seeed XIAO ESP32-C5 pads:  D4 = GPIO23 (SDA pad)   D5 = GPIO24 (SCL pad)
//                            D2 = GPIO25 (RSTO)
#ifndef PIN_PN532_RX
#define PIN_PN532_RX 23   // SDA pad is the module's TX -> our RX
#endif
#ifndef PIN_PN532_TX
#define PIN_PN532_TX 24   // SCL pad is the module's RX -> our TX
#endif
#ifndef PIN_PN532_RST
#define PIN_PN532_RST 25
#endif

// The XIAO's single yellow LED is wired active-low. Detected from the board
// variant so an Arduino IDE build gets it without any flags.
#if defined(ARDUINO_XIAO_ESP32C5) && !defined(STATUS_LED_ACTIVE_LOW)
#define STATUS_LED_ACTIVE_LOW
#endif

// True on the ESP32-C5 and other dual-band parts. Gates the 5 GHz controls in
// the web UI — the firmware is otherwise identical on a 2.4 GHz-only chip.
#if defined(SOC_WIFI_SUPPORT_5G) && SOC_WIFI_SUPPORT_5G
#define HAS_DUAL_BAND 1
#else
#define HAS_DUAL_BAND 0
#endif

// Wi-Fi band preference (Settings::wifiBand)
#define BAND_AUTO 0
#define BAND_2G   1
#define BAND_5G   2

// ===========================================================================
// ARDUINO IDE USERS: put your network here, then save.
// Uncomment and fill in. Leave them commented if you'd rather configure each
// board through its own access point on first boot.
//
//   #define DEFAULT_WIFI_SSID     "YourNetwork"
//   #define DEFAULT_WIFI_PASS     "YourPassword"
//   #define DEFAULT_SPOOLMAN_HOST "192.168.1.20"
//
// The reader runs on HSU; set the module's DIP switches to OFF/OFF and there is
// nothing else to choose. Pin overrides, if your wiring differs, are
// PIN_PN532_RX / PIN_PN532_TX / PIN_PN532_RST above.
//
// (PlatformIO users set these in platformio.ini instead — the -D flags there
//  win over anything below, so leave this block alone.)
// ===========================================================================

// Fleet provisioning. Set these in platformio.ini and every board you flash
// comes up already on the network, so the only per-box step is naming it:
//   -DDEFAULT_WIFI_SSID='"HomeNet"' -DDEFAULT_WIFI_PASS='"hunter2"'
#ifndef DEFAULT_WIFI_SSID
#define DEFAULT_WIFI_SSID ""
#endif
#ifndef DEFAULT_WIFI_PASS
#define DEFAULT_WIFI_PASS ""
#endif
#ifndef DEFAULT_PRINTER_HOST
#define DEFAULT_PRINTER_HOST ""
#endif
#ifndef DEFAULT_SPOOLMAN_HOST
#define DEFAULT_SPOOLMAN_HOST ""
#endif

#define AP_SSID     "U1-SpoolBridge"
#define AP_PASSWORD "spoolbridge"   // >= 8 chars, change if you like
#define FW_VERSION  "1.16.0"

// ---------------------------------------------------------------------------
// Build fingerprint.
//
// "Update all boxes" hands the same .bin to every box on the network, which
// makes a wrong file eight times as expensive as it used to be. So the browser
// checks the file before it sends a single byte — and to do that it needs to
// know what the file IS, from the bytes alone.
//
// The ESP32 image header answers only part of that. It carries a magic byte
// and a chip id (verified: 0x17 for the C5, 0x0D for the C6), so a C6 image
// aimed at a C5 fleet is caught. But the app descriptor's version field is
// filled in by arduino-lib-builder with its own git hash — on these builds it
// reads "ee57070", not FW_VERSION — so it cannot tell you which firmware this
// is, whose it is, or which transport it was built for.
//
// Hence a marker of our own, emitted into .rodata where a browser can scan for
// it. Grammar is deliberately dull so the parser can be too:
//
//   U1SB-FINGERPRINT-v1|fw=1.12.0|tgt=esp32c5|bus=uart|rc=1|end
//
// It is referenced by /api/brief, so the linker cannot drop it and the string
// a box reports about itself is the same string baked into its image.
// ---------------------------------------------------------------------------
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"   // defines CONFIG_IDF_TARGET; may not be in scope yet
#endif

// Kept in the fingerprint even though it can only say one thing now: the fleet
// updater compares it against each box, and a box still running an older I2C or
// SPI build must be told the image it is being offered is a different
// transport rather than silently flashed with it.
#define FW_BUS_STR "uart"

#ifdef CONFIG_IDF_TARGET
#define FW_TARGET_STR CONFIG_IDF_TARGET
#else
#define FW_TARGET_STR "unknown"
#endif

#define FW_RC_STR2(x) #x
#define FW_RC_STR(x)  FW_RC_STR2(x)

#define FW_FINGERPRINT_PREFIX "U1SB-FINGERPRINT-v1|"
#define FW_FINGERPRINT                                                        \
  FW_FINGERPRINT_PREFIX "fw=" FW_VERSION "|tgt=" FW_TARGET_STR                \
                        "|bus=" FW_BUS_STR "|rc=" FW_RC_STR(READER_COUNT) "|end"
