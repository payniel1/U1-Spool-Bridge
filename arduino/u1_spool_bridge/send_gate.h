// ---------------------------------------------------------------------------
// send_gate.h — decides whether a scanned tag should actually be pushed to the
// printer, and to which slot.
//
// Without this the bridge fires on every read, so a spool that merely passes
// the antenna reprograms a slot. The gate holds the last scan as "pending" and
// only releases it when something says a load is really happening.
//
// Pure logic — time is passed in rather than read — so every mode and edge case
// is unit-tested on the host.
// ---------------------------------------------------------------------------
#pragma once

#include <stdint.h>
#include <string.h>

enum TriggerMode : uint8_t {
  TRIG_ON_LOAD   = 0,  // wait for the printer to report a slot filling up
  TRIG_ALWAYS    = 1,  // fire on every settled scan
  TRIG_ARMED     = 2,  // fire only after you arm a slot in the UI
  TRIG_MANUAL    = 3,  // never fire on its own; the Send button only
  TRIG_ON_INSERT = 4   // reader is bolted to one drybox: a spool appearing in
                       // it goes to that box's slot, no printer round-trip
};

struct GateConfig {
  uint8_t  mode         = TRIG_ON_LOAD;
  uint32_t cooldownMs   = 30000;   // same tag -> same slot suppression window
  uint32_t scanValidMs  = 300000;  // how long a scan stays eligible for a load
  uint32_t armTimeoutMs = 120000;  // an armed slot gives up after this
};

struct GateState {
  bool     havePending = false;
  uint32_t pendingAt = 0;
  uint8_t  pendingUid[10] = {0};
  uint8_t  pendingUidLen = 0;

  bool     armed = false;
  uint8_t  armedChannel = 0;
  uint32_t armedAt = 0;

  bool     haveLastSent = false;
  uint8_t  lastSentUid[10] = {0};
  uint8_t  lastSentUidLen = 0;
  uint8_t  lastSentChannel = 0;
  uint32_t lastSentAt = 0;
};

struct GateDecision {
  bool        send = false;
  uint8_t     channel = 0;
  const char *reason = "";  // why, for the activity log
};

// A tag has settled on the reader. Always records it as pending; returns a
// send decision if the current mode wants to act immediately.
GateDecision gateOnScan(const GateConfig &cfg, GateState &st, const uint8_t *uid,
                        uint8_t uidLen, uint8_t defaultChannel, uint32_t now);

// The printer just reported this channel going from empty to occupied.
GateDecision gateOnChannelLoaded(const GateConfig &cfg, GateState &st,
                                 uint8_t channel, uint32_t now);

// Arm a slot (TRIG_ARMED). If a fresh scan is already pending, this releases it.
GateDecision gateArm(const GateConfig &cfg, GateState &st, uint8_t channel,
                     uint32_t now);

// Call after a successful send so the cooldown starts.
void gateNoteSent(GateState &st, const uint8_t *uid, uint8_t uidLen,
                  uint8_t channel, uint32_t now);

// Drop a stale pending scan / expired arm. Safe to call every loop.
void gateExpire(const GateConfig &cfg, GateState &st, uint32_t now);

// Is this tag inside its cooldown for this channel?
bool gateInCooldown(const GateConfig &cfg, const GateState &st, const uint8_t *uid,
                    uint8_t uidLen, uint8_t channel, uint32_t now);

const char *triggerModeName(uint8_t mode);
