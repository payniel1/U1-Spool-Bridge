// ---------------------------------------------------------------------------
// u1_detect.h — which project is serving filament_detect/set on this printer?
//
// The first attempt at this asked "is `filament_detect` queryable?" on the
// theory that only the Extended Firmware exposed it. That was wrong, and wrong
// in the confident direction: `filament_detect` is a STOCK Snapmaker U1 Klipper
// object — it is what the printer's own spool-holder readers write to and what
// the screen displays. paxx12 does not add the object, it adds a writable `set`
// on it. Bespok3d's rfid_ntag.py registers the same endpoint as a plain Klipper
// extra. So both printers answer that query, and the detector cheerfully
// reported "Extended Firmware" on a stock machine.
//
// Comparing two real printers side by side gives a discriminator that actually
// holds. paxx12 extends FILAMENT_INFO_STRUCT with two keys that stock does not
// have:
//
//   stock + Bespok3d : ... "OFFICIAL": true, "CARD_UID": 0
//   paxx12           : ... "OFFICIAL": true, "CARD_UID": [4,195,67,...],
//                          "CARD_TYPE": "NTAG21x", "CARD_EVENT_TIME": 152840.04
//
// and it is STRUCTURAL, not data-dependent: on the stock machine every slot
// lacks them, empty ones included, and on the paxx12 machine every slot has
// them. (Note also that stock reports CARD_UID as the integer 0 rather than a
// byte array — so no tag UID comes back from a stock printer, which is why the
// THIS BOX badge cannot work there.)
//
// This is still an inference about a shape, not a test of behaviour. A refused
// send is the real evidence, so u1_client.cpp lets that outrank this — see
// backendObserved().
//
// Free of Arduino types so the fixtures captured from both machines can be
// replayed on a host.
// ---------------------------------------------------------------------------
#pragma once

#include <stdint.h>

enum U1Probe : uint8_t {
  U1_PROBE_UNKNOWN = 0,   // no filament_detect.info to look at
  U1_PROBE_EXTENDED,      // the paxx12-only keys are present
  U1_PROBE_STOCK,         // info is there and those keys are not
};

// `statusBody` is the whole body of
//   GET /printer/objects/query?print_task_config&filament_detect
U1Probe u1ProbeBackend(const char *statusBody);
