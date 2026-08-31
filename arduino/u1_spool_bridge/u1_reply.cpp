#include "u1_reply.h"

#include <ArduinoJson.h>
#include <string.h>

static void put(char *dst, size_t cap, const char *src) {
  if (!dst || !cap) return;
  if (!src) src = "";
  size_t n = strlen(src);
  if (n >= cap) n = cap - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

U1Reply u1ClassifyReply(const char *body, char *msgOut, size_t msgCap) {
  put(msgOut, msgCap, "");
  if (!body || !*body) return U1_REPLY_UNKNOWN;

  // Checked before parsing, so a truncated or oversized body still gets the
  // one verdict that changes what the caller does next.
  if (strstr(body, "unsupported fields")) {
    put(msgOut, msgCap, body);
    return U1_REPLY_BAD_FIELD;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body) == DeserializationError::Ok) {
    JsonVariantConst root = doc.as<JsonVariantConst>();
    // Moonraker wraps a webhook's answer in {"result": ...}; a direct call is
    // bare. Accept either.
    JsonVariantConst r  = root["result"].isNull() ? root : root["result"];
    const char      *st = r["state"] | "";

    if (!strcmp(st, "success")) return U1_REPLY_OK;
    if (!strcmp(st, "error")) {
      const char *m = r["message"] | "refused";
      put(msgOut, msgCap, m);
      // The Extended Firmware phrases its field complaints differently, so
      // look at the message too rather than only the top-level substring.
      if (strstr(m, "unsupported field") || strstr(m, "unknown field"))
        return U1_REPLY_BAD_FIELD;
      return U1_REPLY_ERROR;
    }
    // A bare {"result": ...} with no state is how some builds acknowledge.
    if (!root["result"].isNull() && st[0] == '\0') return U1_REPLY_OK;
  }

  put(msgOut, msgCap, body);
  return U1_REPLY_UNKNOWN;
}
