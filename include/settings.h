// ---------------------------------------------------------------------------
// settings.h — everything the user can configure, persisted in NVS.
// ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>
#include <stddef.h>

#include "config.h"

#define MAX_EXTRA_KEYS 6

// Settings.printerBackend — who is serving /printer/filament_detect/set.
#define U1_BACKEND_AUTO     0   // probe, and fix itself if a send is rejected
#define U1_BACKEND_EXTENDED 1   // paxx12 Extended Firmware
#define U1_BACKEND_STOCK    2   // stock firmware + the Bespok3d plugin

// Bumped whenever the struct layout changes; a mismatch falls back to defaults
// rather than reading garbage out of NVS.
#define SETTINGS_VERSION 8

// The oldest blob layout settingsLoad() will still overlay onto defaults.
// Raise it only if a change makes an old blob genuinely unreadable rather than
// merely short.
#define SETTINGS_MIN_COMPATIBLE 7

struct Settings {
  uint16_t version;

  // Who this node is. One board per drybox, so every unit needs its own name
  // and its own (printer, slot) binding.
  char boxName[24];    // "Drybox 3" — also drives the mDNS hostname
  bool sendOnBoot;     // re-assert the resident spool after a power cycle

  // Network
  char wifiSsid[33];
  char wifiPass[65];
  char hostname[24];
  uint8_t wifiBand;  // BAND_AUTO / BAND_2G / BAND_5G — ignored on 2.4 GHz-only chips
  // Radio transmit power, dBm. 0 = leave the default (max, ~20 dBm).
  // Turning this down cuts the peak current of a TX burst, which is the cheap
  // software fix for a PN532 sharing a rail that sags. Only worth it on a box
  // with signal to spare — check the RSSI pill before you drop it.
  uint8_t wifiTxPower;

  // Printer (Moonraker on the U1). Which side is serving
  // /printer/filament_detect/set is `printerBackend`, appended at the end of
  // this struct — see the note there.
  char     printerHost[64];
  uint16_t printerPort;
  char     apiKey[48];

  // Spoolman inventory server
  bool     spoolmanEnabled;
  char     spoolmanHost[64];
  uint16_t spoolmanPort;
  bool     spoolmanSetLocation;  // PATCH the spool's location when it's sent
  bool     spoolmanNoteLoads;    // append a line to the spool's comment
  char     locationFmt[32];      // "{slot}" is replaced by 1..4

  // Over-the-air updates
  bool otaEnabled;
  char otaPassword[33];   // empty = no password (LAN-only device)

  // NTP, so the comment trail carries real timestamps
  char ntpServer[40];
  char timezone[40];  // POSIX TZ string, e.g. "CET-1CEST,M3.5.0,M10.5.0/3"

  // Behaviour
  // Which printer slot each reader feeds. 0..3 = slot 1..4 in the UI.
  uint8_t readerChannel[MAX_READERS];
  bool    forceGenericVendor;  // report VENDOR="Generic" for slicer compat
  bool    sendCardUid;         // include CARD_UID so the U1 tracks tag presence
  uint16_t scanIntervalMs;

  // When a scan is allowed to reach the printer — see send_gate.h
  uint8_t  triggerMode;     // TRIG_ON_LOAD / TRIG_ALWAYS / TRIG_ARMED / TRIG_MANUAL
  uint16_t dwellMs;         // how long a tag must sit still to count as a read
  uint16_t absenceMs;       // how long it must be gone to count as removed
  uint16_t cooldownS;       // same tag -> same slot suppression
  uint16_t scanValidS;      // how long a scan stays eligible for a load event
  uint16_t armTimeoutS;     // an armed slot gives up after this
  uint16_t statePollMs;     // how often to ask the printer about slot occupancy

  // Extra MIFARE Classic Key A values to try (hex, 12 chars), for QIDI /
  // Creality / third-party tags whose keys are not derivable.
  char extraKeys[MAX_EXTRA_KEYS][13];

  // ---- append new fields BELOW this line, never above it ------------------
  // (see the layout guard under the struct)

  // Which implementation is answering POST /printer/filament_detect/set.
  //
  //   paxx12 Extended Firmware  — accepts CARD_TYPE alongside CARD_UID.
  //   Bespok3d RFID Spool Reader — a Klipper extra on STOCK firmware that
  //     registers the same endpoint, but rejects the whole request with
  //     "unsupported fields" if it sees a key it does not know, and CARD_TYPE
  //     is one of those. It also exposes no queryable filament_detect object,
  //     so the readback goes dark and only slot presence survives.
  //
  // U1_BACKEND_AUTO detects it and self-corrects on a rejected send; the two
  // explicit values are there for when you would rather not let it guess.
  uint8_t printerBackend;

  void loadDefaults();
};

extern Settings g_settings;

// ---------------------------------------------------------------------------
// Layout guard.
//
// Settings is persisted to NVS as a raw struct blob, so its byte layout is a
// storage format, not an implementation detail. settingsLoad() overlays a
// stored blob onto defaults; that is only safe while existing fields stay
// exactly where they were and new ones are APPENDED. Insert a field in the
// middle and every box reads its neighbours' bytes as its own — which for
// wifiSsid/wifiPass means a fleet that quietly drops off the network during an
// update and has to be reconfigured one box at a time over its AP.
//
// So the offsets are pinned here. If you add a field, put it at the end and
// leave these alone; if one of these fires, that is the bug, not the assert.
// ---------------------------------------------------------------------------
static_assert(offsetof(Settings, version)     ==   0, "Settings layout changed");
static_assert(offsetof(Settings, boxName)     ==   2, "Settings layout changed");
static_assert(offsetof(Settings, wifiSsid)    ==  27, "Settings layout changed");
static_assert(offsetof(Settings, wifiPass)    ==  60, "Settings layout changed");
static_assert(offsetof(Settings, hostname)    == 125, "Settings layout changed");
static_assert(offsetof(Settings, printerHost) == 151, "Settings layout changed");
static_assert(offsetof(Settings, apiKey)      == 218, "Settings layout changed");
static_assert(offsetof(Settings, spoolmanHost)== 267, "Settings layout changed");
static_assert(offsetof(Settings, otaPassword) == 369, "Settings layout changed");
static_assert(offsetof(Settings, ntpServer)   == 402, "Settings layout changed");
static_assert(offsetof(Settings, timezone)    == 442, "Settings layout changed");
static_assert(offsetof(Settings, extraKeys)   == 502, "Settings layout changed");
static_assert(sizeof(Settings) == 582, "Settings grew — see the layout guard above");

// The group a box belongs to in the fleet view. Kept OUT of the struct above
// and stored under its own NVS key, precisely so adding it cannot change
// sizeof(Settings) and trip the mechanism the guard describes.
#define GROUP_NAME_MAX 24
extern char g_groupName[GROUP_NAME_MAX];
void        groupNameSet(const char *s);

bool settingsLoad();
bool settingsSave();
// Turn boxName into a unique mDNS hostname ("Drybox 3" -> "u1-drybox-3"), so
// eight nodes on one network don't all answer to the same name.
void settingsDeriveHostname();
void settingsToJson(String &out);
bool settingsFromJson(const String &json, String &err);
