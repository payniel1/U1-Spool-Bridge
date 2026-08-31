#include "u1_detect.h"

#include <ArduinoJson.h>

U1Probe u1ProbeBackend(const char *statusBody) {
  if (!statusBody || !*statusBody) return U1_PROBE_UNKNOWN;

  // Its own tiny filter rather than piggy-backing on the caller's parse: the
  // question this answers is about which KEYS exist, and a filter that does
  // not name a key removes exactly the evidence we are looking for. Keeping it
  // self-contained means the fixtures replayed in the tests go through the
  // same path the firmware does.
  JsonDocument filter;
  JsonObject slot = filter["result"]["status"]["filament_detect"]["info"]
                        .to<JsonArray>()
                        .add<JsonObject>();
  slot["CARD_TYPE"]       = true;
  slot["CARD_EVENT_TIME"] = true;

  JsonDocument doc;
  if (deserializeJson(doc, statusBody, DeserializationOption::Filter(filter)))
    return U1_PROBE_UNKNOWN;

  JsonArrayConst info = doc["result"]["status"]["filament_detect"]["info"];
  if (info.isNull() || info.size() == 0) return U1_PROBE_UNKNOWN;

  for (JsonObjectConst o : info) {
    if (!o["CARD_TYPE"].isNull() || !o["CARD_EVENT_TIME"].isNull())
      return U1_PROBE_EXTENDED;
  }
  return U1_PROBE_STOCK;
}
