// ---------------------------------------------------------------------------
// u1_client.h — talks to the Snapmaker U1's Moonraker instance.
//
// Everything here hangs off one endpoint:
//
//   POST /printer/filament_detect/set
//   {"channel": 0, "info": {"VENDOR": "...", "MAIN_TYPE": "PLA", ...}}
//
// Stock Snapmaker firmware does not serve it. TWO different projects add it,
// and they are not quite the same endpoint:
//
//   paxx12 Extended Firmware — patches Klipper's `filament_detect` object to
//     expose a writable `set`, and adds CARD_TYPE next to CARD_UID. It also
//     exposes `filament_detect` to /printer/objects/query, which is what makes
//     the readback ("what the printer believes is in each slot") possible.
//
//   Bespok3d "RFID Spool Reader" — a Klipper extra installed onto STOCK
//     firmware, no flashing. Its rfid_ntag.py does
//         webhooks.register_endpoint("filament_detect/set", ...)
//     so the same URL, the same nested `info` object, and the same
//     {"state":"success"} answer. Two differences bite:
//       * it validates the field list and answers
//             {"state":"error","message":"unsupported fields: CARD_TYPE"}
//         rejecting the WHOLE request rather than ignoring the stray key;
//       * it defines no get_status, so `filament_detect` is not queryable and
//         only `print_task_config.filament_exist` comes back.
//
// So the payload and the readback both depend on which one is out there.
// u1BackendEffective() answers that, and on U1_BACKEND_AUTO it is allowed to
// change its mind — see u1Send().
// ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>

#include "spool_data.h"

// What we currently believe is serving the endpoint. Never returns
// U1_BACKEND_AUTO: with nothing to go on it assumes EXTENDED, because being
// wrong that way is loud and self-correcting (the send is rejected and we
// latch STOCK), whereas the other way round would quietly drop CARD_TYPE
// forever with nothing to show for it.
uint8_t     u1BackendEffective();
const char *u1BackendName(uint8_t backend);

// True once something has actually told us, rather than us assuming.
bool u1BackendKnown();

// Drop the latch — call when the printer host or the backend setting changes,
// so a box moved to a different printer does not carry an old answer over.
void u1BackendForget();

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

// True when the printer answered but had no queryable `filament_detect` — i.e.
// presence is all we are going to get. The UI uses this to explain an empty
// "Loaded in the printer" card instead of leaving it looking broken.
bool u1SlotsPresenceOnly();

// One query for both objects: presence and full per-slot filament info.
bool u1FetchSlots(U1Slot slots[4], String &errorOut);

// Serialise a SpoolData into the `info` object the printer expects. Exposed so
// the web UI can show the exact payload before it goes out. `backend` may be
// U1_BACKEND_AUTO, in which case u1BackendEffective() decides.
void u1BuildPayload(const SpoolData &d, uint8_t channel, String &out,
                    uint8_t backend = 0 /* U1_BACKEND_AUTO */);
