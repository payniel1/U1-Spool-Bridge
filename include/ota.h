// ---------------------------------------------------------------------------
// ota.h — over-the-air firmware updates.
//
// Two ways in, because they suit different moments:
//
//   * ArduinoOTA (espota) — `pio run -t upload --upload-port u1-drybox-3.local`.
//     Scriptable, so eight boxes are a for-loop.
//   * POST /api/ota — drag a .bin onto the web UI. No toolchain needed.
//
// Both write to the inactive OTA slot and only switch the boot partition once
// the whole image has landed and verified, so a failed or interrupted update
// leaves the box running exactly what it was running before.
// ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>

// Call after WiFi and mDNS are up.
void otaBegin();

// Pump ArduinoOTA. Cheap; call every loop.
void otaLoop();

// True while an update is being written — the main loop stops polling the
// reader and talking to the printer for the duration.
bool otaBusy();

// Progress, for the UI. 0..100, or -1 when idle.
int otaProgressPct();

// Used by the browser-upload handler in web_ui.cpp, which writes the image on
// the web server's task rather than through ArduinoOTA.
void otaSetBusy(bool busy);
void otaSetProgress(int pct);

// Set by the web upload handler when an image has landed successfully; the
// main loop reboots on the next pass so the HTTP response can be delivered
// first.
extern volatile bool g_otaRebootPending;

// The build fingerprint baked into this image: project, version, chip target,
// transport and reader count. /api/brief reports it, and the browser scans an
// uploaded .bin for the same string, so "what this box runs" and "what this
// file would install" are compared in identical terms.
const char *fwFingerprint();
