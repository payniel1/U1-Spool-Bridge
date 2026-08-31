// ---------------------------------------------------------------------------
// u1_reply.h — reading the printer's answer to a filament_detect/set.
//
// Two different projects serve that endpoint and they refuse things
// differently, so the answer has to be classified rather than glanced at. Both
// answer HTTP 200 whatever happens and put the verdict in the body:
//
//   {"result":{"state":"success"}}
//   {"result":{"state":"error","message":"unsupported fields: CARD_TYPE"}}
//
// That second one is the Bespok3d validator refusing the WHOLE request because
// of one key it does not know, and it is the signal that the box is talking to
// stock firmware rather than the Extended Firmware. Getting it wrong in the
// lenient direction is the dangerous case: a substring check for "result"
// passes on the error body too, and the box would report every rejected send
// as a success.
//
// Free of Arduino types on purpose, so `pio test -e native` can put real
// response bodies through the shipped code path.
// ---------------------------------------------------------------------------
#pragma once

#include <stddef.h>
#include <stdint.h>

enum U1Reply : uint8_t {
  U1_REPLY_OK = 0,
  U1_REPLY_BAD_FIELD,   // refused for carrying a field it does not know
  U1_REPLY_ERROR,       // refused for some other reason
  U1_REPLY_UNKNOWN,     // not a shape we recognise
};

// `msgOut` receives a short reason, NUL-terminated, possibly empty. Safe to
// pass a null body; that classifies as U1_REPLY_UNKNOWN.
U1Reply u1ClassifyReply(const char *body, char *msgOut, size_t msgCap);
