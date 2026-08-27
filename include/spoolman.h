// ---------------------------------------------------------------------------
// spoolman.h — talks to a Spoolman inventory server.
//
// The tag on the spool is only an identifier; Spoolman is the source of truth
// for what's actually on it. We look a spool up by its tag UID, which lives in
// the `card_uids` extra field — the same convention the U1 Extended Firmware's
// SpoolLink uses, so the two agree on what a tag means.
//
// Spoolman stores every extra-field value as a JSON-encoded string regardless
// of the field's declared type, so `card_uids` on the wire looks like
//     "extra": {"card_uids": "\"AABBCCDD,11223344\""}
// and this module does that encoding for you.
//
// API reference: https://donkie.github.io/Spoolman/
// ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>

#include "spool_data.h"

#define SPOOLMAN_UID_FIELD "card_uids"

struct SpoolmanSpool {
  bool     found = false;
  uint32_t id = 0;
  char     filamentName[48] = {0};
  char     vendor[32] = {0};
  char     material[16] = {0};
  char     variant[24] = {0};  // extra.variant, if SpoolLink set one
  char     colorHex[10] = {0};
  uint16_t extruderTemp = 0;
  uint16_t bedTemp = 0;
  float    remainingWeight = -1;  // grams, -1 = unknown
  float    initialWeight = -1;
  float    diameter = 0;
  char     location[40] = {0};
  String   comment;  // existing comment, so we can append rather than clobber
};

bool spoolmanConfigured();

// GET /api/v1/info — cheap reachability probe.
bool spoolmanPing(String &versionOut, String &errOut);

// Create the `card_uids` extra field if it isn't there yet. Safe to re-run.
bool spoolmanEnsureFields(String &errOut);

// Scan the inventory for a spool whose card_uids contains `uidHex`.
// Returns the spool id, or 0 if nothing matched. `errOut` is only set on a
// real failure — "not found" is not an error.
uint32_t spoolmanFindByUid(const char *uidHex, String &errOut);

// GET /api/v1/spool/{id}
bool spoolmanFetch(uint32_t id, SpoolmanSpool &out, String &errOut);

// Overlay Spoolman's data onto a decoded tag. Spoolman wins on everything it
// knows about; anything it leaves blank keeps the tag's value.
void spoolmanApply(const SpoolmanSpool &s, SpoolData &d);

// Rewrite the location of an already-loaded spool, without touching its
// comment. Used when the group name or the location format changes.
bool spoolmanRefileLocation(uint32_t spoolId, uint8_t channel, String &errOut);

// Attach this tag UID to a spool, and detach it from any other spool that
// claims it — otherwise the next lookup is ambiguous.
bool spoolmanLinkUid(uint32_t spoolId, const char *uidHex, String &errOut);

// Record that the spool went into a slot: sets `location` and appends one line
// to the spool's comment.
bool spoolmanNoteLoad(uint32_t spoolId, uint8_t channel, String &errOut);

// A trimmed spool list for the web UI's picker. Writes a JSON array of
// {id, label, material, color, remaining} into `jsonOut`.
bool spoolmanListForPicker(String &jsonOut, String &errOut);
