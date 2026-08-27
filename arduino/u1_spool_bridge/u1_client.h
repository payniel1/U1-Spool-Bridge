// ---------------------------------------------------------------------------
// u1_client.h — talks to the Snapmaker U1's Moonraker instance.
//
// The endpoint comes from the paxx12 Extended Firmware, which patches Klipper's
// `filament_detect` object to expose a writable `set`:
//
//   POST /printer/filament_detect/set
//   {"channel": 0, "info": {"VENDOR": "...", "MAIN_TYPE": "PLA", ...}}
//
// Stock Snapmaker firmware does NOT expose this — see the README.
// ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>

#include "spool_data.h"

struct SendResult {
  bool   ok = false;
  int    httpCode = 0;
  String body;
  String error;
};

// Push one spool into a slot. `channel` is 0..3 (slot 1..4 on the machine).
SendResult u1Send(const SpoolData &d, uint8_t channel);

// Cheap reachability probe — GET /printer/info.
bool u1Ping(String &hostnameOut, String &errorOut);

// Per-slot filament presence, straight from Klipper:
//   GET /printer/objects/query?print_task_config -> filament_exist[]
// This is the same field the U1's own filament UI uses to decide whether a
// slot is occupied, so a false->true transition is a load actually happening.
bool u1FetchChannels(bool present[4], String &errorOut);

// What the printer itself believes is in each slot. This is the readback of the
// same object we write to, so it is ground truth rather than our memory of what
// we sent — and it survives the spool leaving the reader, which is exactly the
// moment you want to look at it.
struct U1Slot {
  bool     present   = false;   // print_task_config.filament_exist[i]
  bool     known     = false;   // the printer had filament info for this slot
  char     vendor[24]   = {0};
  char     mainType[16] = {0};
  char     subType[24]  = {0};
  uint32_t rgb       = 0;
  uint8_t  alpha     = 255;
  uint16_t hotendMin = 0, hotendMax = 0, bedTemp = 0;
  char     uidHex[24]   = {0};  // empty unless CARD_UID was sent
  char     cardType[12] = {0};
};

// One query for both objects: presence and full per-slot filament info.
bool u1FetchSlots(U1Slot slots[4], String &errorOut);

// Serialise a SpoolData into the `info` object the printer expects. Exposed so
// the web UI can show the exact payload before it goes out.
void u1BuildPayload(const SpoolData &d, uint8_t channel, String &out);
