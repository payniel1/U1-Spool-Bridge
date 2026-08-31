#include "u1_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <string.h>

#include "settings.h"
#include "u1_detect.h"
#include "u1_reply.h"

// What the wire has actually told us. U1_BACKEND_AUTO means "nobody has said
// yet"; u1BackendEffective() turns that into a working assumption.
static uint8_t s_detected     = U1_BACKEND_AUTO;
static bool    s_detectedHard = false;   // came from a send, not from a shape
static bool    s_presenceOnly = false;

uint8_t u1BackendEffective() {
  if (g_settings.printerBackend != U1_BACKEND_AUTO) return g_settings.printerBackend;
  if (s_detected != U1_BACKEND_AUTO) return s_detected;
  return U1_BACKEND_EXTENDED;   // see the note in u1_client.h
}

bool u1BackendKnown() {
  return g_settings.printerBackend != U1_BACKEND_AUTO || s_detected != U1_BACKEND_AUTO;
}

void u1BackendForget() {
  s_detected     = U1_BACKEND_AUTO;
  s_detectedHard = false;
  s_presenceOnly = false;
}

// True once a SEND has told us, rather than the status shape merely suggesting.
bool u1BackendConfirmed() { return s_detectedHard; }

bool u1SlotsPresenceOnly() { return s_presenceOnly; }

const char *u1BackendName(uint8_t backend) {
  switch (backend) {
    case U1_BACKEND_EXTENDED: return "Extended Firmware";
    case U1_BACKEND_STOCK:    return "stock + Bespok3d";
    default:                  return "detecting";
  }
}

// Only latch while the user has left it on AUTO — an explicit choice should not
// be quietly overridden by a probe.
//
// `hard` separates two very different kinds of evidence. A refused send is a
// TEST of the behaviour that actually differs. The status-shape probe is an
// inference, and it runs every 15 seconds. Without this, the probe walked over
// the send's answer on the next poll — so a box that had correctly worked out
// it was talking to Bespok3d flipped back to Extended a few seconds later, and
// then paid two round trips on every send thereafter, for ever.
static void backendObserved(uint8_t what, bool hard) {
  if (g_settings.printerBackend != U1_BACKEND_AUTO) return;
  if (s_detectedHard && !hard) return;
  s_detected     = what;
  s_detectedHard = s_detectedHard || hard;
}

static String baseUrl() {
  String u = "http://";
  u += g_settings.printerHost;
  if (g_settings.printerPort != 80) {
    u += ":";
    u += String(g_settings.printerPort);
  }
  return u;
}

bool u1FetchSlots(U1Slot slots[4], String &errorOut) {
  for (int i = 0; i < 4; i++) slots[i] = U1Slot();

  if (WiFi.status() != WL_CONNECTED) { errorOut = "no WiFi"; return false; }
  if (g_settings.printerHost[0] == '\0') { errorOut = "no printer host set"; return false; }

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(5000);
  // Both objects in one round trip — presence and the filament info itself.
  if (!http.begin(client, baseUrl() +
                              "/printer/objects/query?print_task_config&filament_detect")) {
    errorOut = "begin failed";
    return false;
  }
  if (g_settings.apiKey[0]) http.addHeader("X-Api-Key", g_settings.apiKey);
  int    code = http.GET();
  String body = http.getString();
  http.end();

  if (code != 200) { errorOut = "HTTP " + String(code); return false; }

  // Filtered parse: the full object carries a lot of metadata we don't show,
  // and a 4-channel blob would otherwise be the biggest allocation we make.
  JsonDocument filter;
  filter["result"]["status"]["print_task_config"]["filament_exist"] = true;
  JsonObject fi = filter["result"]["status"]["filament_detect"]["info"]
                      .to<JsonArray>()
                      .add<JsonObject>();
  for (const char *k : {"VENDOR", "MAIN_TYPE", "SUB_TYPE", "RGB_1", "ALPHA",
                        "HOTEND_MIN_TEMP", "HOTEND_MAX_TEMP", "BED_TEMP",
                        "CARD_UID", "CARD_TYPE", "OFFICIAL"}) {
    fi[k] = true;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
    errorOut = "unparseable status";
    return false;
  }

  JsonArrayConst pres =
      doc["result"]["status"]["print_task_config"]["filament_exist"];
  int i = 0;
  for (JsonVariantConst v : pres) {
    if (i >= 4) break;
    slots[i++].present = v.as<bool>();
  }

  // Which backend, from the SHAPE of the struct rather than from whether the
  // object exists — both firmwares have it. See u1_detect.h.
  switch (u1ProbeBackend(body.c_str())) {
    case U1_PROBE_EXTENDED: backendObserved(U1_BACKEND_EXTENDED, false); break;
    case U1_PROBE_STOCK:    backendObserved(U1_BACKEND_STOCK,    false); break;
    default: break;   // nothing to go on; leave whatever we had
  }

  JsonArrayConst info = doc["result"]["status"]["filament_detect"]["info"];
  if (info.isNull()) {
    // Genuinely absent. Neither backend does this, but presence still worked,
    // so report what we have rather than failing the whole call.
    s_presenceOnly = (i > 0);
    errorOut = "filament_detect not queryable — slot presence only";
    return i > 0;
  }

  s_presenceOnly = false;

  i = 0;
  for (JsonObjectConst o : info) {
    if (i >= 4) break;
    U1Slot &s = slots[i++];
    snprintf(s.vendor,   sizeof(s.vendor),   "%s", (const char *)(o["VENDOR"]    | ""));
    snprintf(s.mainType, sizeof(s.mainType), "%s", (const char *)(o["MAIN_TYPE"] | ""));
    snprintf(s.subType,  sizeof(s.subType),  "%s", (const char *)(o["SUB_TYPE"]  | ""));
    snprintf(s.cardType, sizeof(s.cardType), "%s", (const char *)(o["CARD_TYPE"] | ""));
    s.rgb       = (uint32_t)(o["RGB_1"] | 0) & 0xFFFFFF;
    s.alpha     = (uint8_t)(o["ALPHA"] | 255);
    s.hotendMin = (uint16_t)(o["HOTEND_MIN_TEMP"] | 0);
    s.hotendMax = (uint16_t)(o["HOTEND_MAX_TEMP"] | 0);
    s.bedTemp   = (uint16_t)(o["BED_TEMP"] | 0);

    // CARD_UID comes back the way it went out: an array of byte integers.
    JsonArrayConst uid = o["CARD_UID"];
    if (!uid.isNull()) {
      uint8_t raw[10];
      uint8_t n = 0;
      for (JsonVariantConst b : uid) {
        if (n >= sizeof(raw)) break;
        raw[n++] = (uint8_t)b.as<unsigned>();
      }
      if (n) uidToHex(raw, n, s.uidHex, sizeof(s.uidHex));
    }

    s.known = s.mainType[0] != '\0' || s.vendor[0] != '\0';
  }
  return true;
}

void u1BuildPayload(const SpoolData &d, uint8_t channel, String &out,
                    uint8_t backend) {
  if (backend == U1_BACKEND_AUTO) backend = u1BackendEffective();
  JsonDocument doc;
  doc["channel"] = channel;
  JsonObject info = doc["info"].to<JsonObject>();

  info["VENDOR"]    = d.vendor;
  info["MAIN_TYPE"] = d.mainType;
  info["SUB_TYPE"]  = d.subType;

  info["RGB_1"] = (uint32_t)(d.rgb & 0xFFFFFF);
  info["RGB_2"] = (uint32_t)(d.rgb2 & 0xFFFFFF);
  info["RGB_3"] = 0;
  info["RGB_4"] = 0;
  info["RGB_5"] = 0;
  info["ALPHA"] = d.alpha;

  info["HOTEND_MIN_TEMP"] = d.hotendMin;
  info["HOTEND_MAX_TEMP"] = d.hotendMax;
  info["BED_TEMP"]        = d.bedTemp;

  if (g_settings.sendCardUid && d.uidLen > 0) {
    JsonArray uid = info["CARD_UID"].to<JsonArray>();
    for (uint8_t i = 0; i < d.uidLen; i++) uid.add(d.uid[i]);
    // CARD_TYPE is an Extended Firmware field. The Bespok3d handler validates
    // the whole `info` object against a fixed list and raises
    //     "unsupported fields: CARD_TYPE"
    // which fails the entire request — so on stock we simply leave it out.
    // CARD_UID itself is on their accepted list and stays.
    if (backend != U1_BACKEND_STOCK) info["CARD_TYPE"] = d.cardType;
  }

  if (d.sku) info["SKU"] = d.sku;

  serializeJson(doc, out);
}

SendResult u1Send(const SpoolData &d, uint8_t channel) {
  SendResult r;

  if (WiFi.status() != WL_CONNECTED) {
    r.error = "not connected to WiFi";
    return r;
  }
  if (g_settings.printerHost[0] == '\0') {
    r.error = "printer host not configured";
    return r;
  }
  if (channel > 3) {
    r.error = "channel must be 0-3";
    return r;
  }

  const String url     = baseUrl() + "/printer/filament_detect/set";
  uint8_t      backend = u1BackendEffective();

  // Two passes at most. The second only happens when the first was refused for
  // carrying a field this backend does not know, which is exactly the
  // Extended-vs-stock mismatch — so we drop those fields and go again.
  for (uint8_t attempt = 0; attempt < 2; attempt++) {
    String payload;
    u1BuildPayload(d, channel, payload, backend);

    WiFiClient client;
    HTTPClient http;
    http.setTimeout(6000);
    if (!http.begin(client, url)) {
      r.error = "could not open " + url;
      return r;
    }
    http.addHeader("Content-Type", "application/json");
    if (g_settings.apiKey[0]) http.addHeader("X-Api-Key", g_settings.apiKey);

    r.httpCode = http.POST(payload);
    r.body     = http.getString();
    http.end();

    if (r.httpCode <= 0) {
      r.error = "HTTP error " + String(r.httpCode) + " (" +
                HTTPClient::errorToString(r.httpCode) + ")";
      return r;
    }
    if (r.httpCode < 200 || r.httpCode >= 300) {
      r.error = "printer returned HTTP " + String(r.httpCode);
      return r;
    }

    char    msg[160];
    U1Reply reply = u1ClassifyReply(r.body.c_str(), msg, sizeof(msg));

    if (reply == U1_REPLY_OK) {
      // A backend that accepted an Extended-only field is an Extended backend.
      // Worth latching: it means the readback is worth attempting.
      if (backend == U1_BACKEND_EXTENDED && g_settings.sendCardUid && d.uidLen > 0) {
        backendObserved(U1_BACKEND_EXTENDED, true);
      }
      r.ok = true;
      return r;
    }

    if (reply == U1_REPLY_BAD_FIELD && backend != U1_BACKEND_STOCK) {
      if (g_settings.printerBackend != U1_BACKEND_AUTO) {
        // The user pinned this. Say so plainly rather than quietly working
        // around a setting they chose on purpose.
        r.error = "printer refused a field this firmware sent (" + String(msg) +
                  "). Printer backend is pinned to \"" +
                  u1BackendName(g_settings.printerBackend) +
                  "\" — set it to Auto, or to stock + Bespok3d.";
        return r;
      }
      backendObserved(U1_BACKEND_STOCK, true);
      backend = U1_BACKEND_STOCK;
      continue;                       // one retry, without the extra fields
    }

    // Reaching here on BAD_FIELD means the retry was ALSO refused for a field
    // — so it is not the Extended/stock mismatch and there is nothing left to
    // strip. Say what actually happened rather than "unexpected response".
    if (reply == U1_REPLY_BAD_FIELD || reply == U1_REPLY_ERROR) {
      r.error = "printer refused it: " + String(msg);
    } else {
      r.error = "unexpected response: " + r.body;
    }
    return r;
  }

  // Unreachable: every path above returns, and the only `continue` is on the
  // first pass. Here so a future third case cannot fall out silently ok.
  if (r.error.length() == 0) r.error = "send gave up after two attempts";
  return r;
}

bool u1FetchChannels(bool present[4], String &errorOut) {
  if (WiFi.status() != WL_CONNECTED) { errorOut = "no WiFi"; return false; }
  if (g_settings.printerHost[0] == '\0') { errorOut = "no printer host set"; return false; }

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(4000);
  if (!http.begin(client, baseUrl() + "/printer/objects/query?print_task_config")) {
    errorOut = "begin failed";
    return false;
  }
  if (g_settings.apiKey[0]) http.addHeader("X-Api-Key", g_settings.apiKey);
  int    code = http.GET();
  String body = http.getString();
  http.end();

  if (code != 200) { errorOut = "HTTP " + String(code); return false; }

  // Only the array we need, so a big status blob stays cheap.
  JsonDocument filter;
  filter["result"]["status"]["print_task_config"]["filament_exist"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
    errorOut = "unparseable status";
    return false;
  }

  JsonArrayConst arr =
      doc["result"]["status"]["print_task_config"]["filament_exist"];
  if (arr.isNull()) {
    errorOut = "print_task_config.filament_exist missing";
    return false;
  }

  for (int i = 0; i < 4; i++) present[i] = false;
  int i = 0;
  for (JsonVariantConst v : arr) {
    if (i >= 4) break;
    present[i++] = v.as<bool>();
  }
  return true;
}

bool u1Ping(String &hostnameOut, String &errorOut) {
  if (WiFi.status() != WL_CONNECTED) { errorOut = "no WiFi"; return false; }
  if (g_settings.printerHost[0] == '\0') { errorOut = "no printer host set"; return false; }

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(4000);
  if (!http.begin(client, baseUrl() + "/printer/info")) {
    errorOut = "begin failed";
    return false;
  }
  if (g_settings.apiKey[0]) http.addHeader("X-Api-Key", g_settings.apiKey);
  int code = http.GET();
  String body = http.getString();
  http.end();

  if (code != 200) {
    errorOut = "HTTP " + String(code);
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body) == DeserializationError::Ok) {
    const char *h = doc["result"]["hostname"] | "";
    hostnameOut = h[0] ? h : "printer";
  } else {
    hostnameOut = "printer";
  }
  return true;
}
