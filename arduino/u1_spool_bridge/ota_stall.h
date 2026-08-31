// ---------------------------------------------------------------------------
// ota_stall.h — has a browser upload gone quiet?
//
// An OTA that is REFUSED cleans up after itself: the handler reports the
// reason and clears the busy flag. An OTA whose connection simply dies does
// not. The final chunk never arrives, so the code that would clear the flag is
// never reached, and `otaBusy()` stays true forever. That flag gates the whole
// main loop:
//
//     if (otaBusy()) { delay(1); return; }
//
// so the box stops reading tags, stops talking to the printer, stops taking
// part in fleet updates, and refuses the retry as well — Update is still
// begun. Only a power cycle clears it, which is fine on a bench and useless
// across eight dryboxes on two floors.
//
// Split out as a pure predicate because the arithmetic is the part that can be
// wrong: millis() wraps every ~49.7 days, so `now < lastActivity` is a
// perfectly ordinary state a few weeks into an uptime, not an impossible one.
//
// The tempting wrong version is a guard that treats it as impossible:
//
//     if (now < lastActivity) return false;   // "clock went backwards"
//     return now - lastActivity >= timeoutMs;
//
// which never fires again for the ~49 days after a wrap — so a box that has
// been up that long is exactly the one that can no longer rescue itself. The
// test suite mutation-checks that specific mistake, and the boundary, and the
// busy guard.
//
// (Writing the subtraction signed is NOT the hazard, despite looking like it:
// the bit pattern is identical and it happens to behave. Do not rely on that —
// signed overflow is undefined and the compiler may act on it — but do not
// expect the tests to catch it either. They do not.)
// ---------------------------------------------------------------------------
#pragma once

#include <stdint.h>

// How long an upload may go without a single byte before it is abandoned.
//
// Too short kills a live-but-slow transfer, which is much worse than waiting:
// chunks arrive as fast as TCP delivers them, so a real gap this long means
// the far end is gone. Too long leaves the box deaf. 45 s is far beyond any
// plausible LAN stall and still a fraction of "until someone walks over".
#define OTA_STALL_MS 45000u

static inline bool otaStalled(bool busy, uint32_t now, uint32_t lastActivity,
                              uint32_t timeoutMs) {
  if (!busy) return false;
  // Deliberately unsigned: the wrap is the point. now=10, last=0xFFFFFFF0 is
  // 26 ms of elapsed time, not a negative one.
  return (uint32_t)(now - lastActivity) >= timeoutMs;
}
