#include "send_gate.h"

const char *triggerModeName(uint8_t mode) {
  switch (mode) {
    case TRIG_ON_LOAD:   return "on load";
    case TRIG_ALWAYS:    return "every scan";
    case TRIG_ARMED:     return "armed slot";
    case TRIG_MANUAL:    return "manual";
    case TRIG_ON_INSERT: return "on insert";
    default:             return "?";
  }
}

// millis() wraps after ~49 days. Unsigned subtraction handles that correctly as
// long as we never compare timestamps directly.
static inline uint32_t elapsed(uint32_t now, uint32_t then) { return now - then; }

static bool sameUid(const uint8_t *a, uint8_t aLen, const uint8_t *b, uint8_t bLen) {
  return aLen && aLen == bLen && memcmp(a, b, aLen) == 0;
}

bool gateInCooldown(const GateConfig &cfg, const GateState &st, const uint8_t *uid,
                    uint8_t uidLen, uint8_t channel, uint32_t now) {
  if (!st.haveLastSent || cfg.cooldownMs == 0) return false;
  if (st.lastSentChannel != channel) return false;
  if (!sameUid(uid, uidLen, st.lastSentUid, st.lastSentUidLen)) return false;
  return elapsed(now, st.lastSentAt) < cfg.cooldownMs;
}

void gateExpire(const GateConfig &cfg, GateState &st, uint32_t now) {
  if (st.havePending && cfg.scanValidMs &&
      elapsed(now, st.pendingAt) > cfg.scanValidMs) {
    st.havePending = false;
  }
  if (st.armed && cfg.armTimeoutMs && elapsed(now, st.armedAt) > cfg.armTimeoutMs) {
    st.armed = false;
  }
}

GateDecision gateOnScan(const GateConfig &cfg, GateState &st, const uint8_t *uid,
                        uint8_t uidLen, uint8_t defaultChannel, uint32_t now) {
  GateDecision d;

  // Whatever the mode, the scan becomes the pending spool — that is what a
  // later load event, arm, or Send press will use.
  st.havePending = true;
  st.pendingAt   = now;
  st.pendingUidLen = uidLen > sizeof(st.pendingUid) ? sizeof(st.pendingUid) : uidLen;
  memcpy(st.pendingUid, uid, st.pendingUidLen);

  if (cfg.mode == TRIG_ALWAYS || cfg.mode == TRIG_ON_INSERT) {
    if (gateInCooldown(cfg, st, uid, uidLen, defaultChannel, now)) {
      d.reason = "still in cooldown";
      return d;
    }
    d.send = true;
    d.channel = defaultChannel;
    // On a box-mounted reader the tag only reappears when a spool is genuinely
    // swapped in — the reader's absence debounce has already ruled out a
    // momentary dropout by the time we get here.
    d.reason = (cfg.mode == TRIG_ON_INSERT) ? "spool inserted in this box"
                                            : "scan (every-scan mode)";
    return d;
  }

  if (cfg.mode == TRIG_ARMED && st.armed) {
    st.armed = false;
    if (gateInCooldown(cfg, st, uid, uidLen, st.armedChannel, now)) {
      d.reason = "still in cooldown";
      return d;
    }
    d.send = true;
    d.channel = st.armedChannel;
    d.reason = "scan while armed";
    return d;
  }

  d.reason = (cfg.mode == TRIG_ON_LOAD) ? "held — waiting for a slot to load"
                                        : "held";
  return d;
}

GateDecision gateOnChannelLoaded(const GateConfig &cfg, GateState &st,
                                 uint8_t channel, uint32_t now) {
  GateDecision d;
  d.channel = channel;

  if (cfg.mode != TRIG_ON_LOAD) {
    d.reason = "load ignored — not in on-load mode";
    return d;
  }
  if (!st.havePending) {
    d.reason = "slot loaded but nothing has been scanned";
    return d;
  }
  if (cfg.scanValidMs && elapsed(now, st.pendingAt) > cfg.scanValidMs) {
    st.havePending = false;
    d.reason = "slot loaded but the last scan is too old";
    return d;
  }
  if (gateInCooldown(cfg, st, st.pendingUid, st.pendingUidLen, channel, now)) {
    d.reason = "still in cooldown";
    return d;
  }

  d.send   = true;
  d.reason = "slot loaded";
  return d;
}

GateDecision gateArm(const GateConfig &cfg, GateState &st, uint8_t channel,
                     uint32_t now) {
  GateDecision d;
  d.channel = channel;

  // A spool already sitting on the reader shouldn't need re-presenting.
  bool pendingFresh = st.havePending &&
                      (!cfg.scanValidMs || elapsed(now, st.pendingAt) <= cfg.scanValidMs);
  if (pendingFresh && !gateInCooldown(cfg, st, st.pendingUid, st.pendingUidLen,
                                      channel, now)) {
    st.armed = false;
    d.send   = true;
    d.reason = "armed with a spool already on the reader";
    return d;
  }

  st.armed        = true;
  st.armedChannel = channel;
  st.armedAt      = now;
  d.reason        = "armed — present a spool";
  return d;
}

void gateNoteSent(GateState &st, const uint8_t *uid, uint8_t uidLen,
                  uint8_t channel, uint32_t now) {
  st.haveLastSent    = true;
  st.lastSentUidLen  = uidLen > sizeof(st.lastSentUid) ? sizeof(st.lastSentUid) : uidLen;
  memcpy(st.lastSentUid, uid, st.lastSentUidLen);
  st.lastSentChannel = channel;
  st.lastSentAt      = now;
}
