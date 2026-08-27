// ---------------------------------------------------------------------------
// fleet_ota.h — push the firmware image this box just received to every other
// box on the network, from the box rather than from the browser.
//
// The browser uploads once, to here. This box writes the image to its inactive
// OTA slot as usual, then reads it straight back out of that slot and POSTs it
// to each peer's /api/ota in turn, confirms each one came back on the new
// version, and only then reboots into the image itself. So the box driving the
// update is still the last to restart, and the phone uploads 1.5 MB once
// instead of once per box.
// ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>

// Set by the browser before the upload: where to push, the OTA password to use,
// and the version string the image carries (so each peer can be confirmed).
void fleetPlanSet(const String &peersCsv, const String &pw, const String &wantFw);
void fleetPlanClear();
bool fleetPlanPending();
uint8_t fleetPlanCount();

// Called by the upload handler once an image has landed and verified, so the
// push knows where it is and how long it is.
void fleetNoteImage(const void *partition, size_t length);

// Blocking, and slow — seconds per peer. Runs from the main loop, not the web
// server task. Reports progress over the websocket as it goes.
void fleetPushRun();
