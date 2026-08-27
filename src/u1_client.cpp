#include "u1_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "settings.h"

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

  JsonArrayConst info = doc["result"]["status"]["filament_detect"]["info"];
  if (info.isNull()) {
    // Presence still worked, so don't fail the whole call — stock firmware
    // simply won't have this object.
    errorOut = "filament_detect not exposed (stock firmware?)";
    return i > 0;
  }

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

void u1BuildPayload(const SpoolData &d, uint8_t channel, String &out) {
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
    info["CARD_TYPE"] = d.cardType;
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

  String payload;
  u1BuildPayload(d, channel, payload);

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(6000);
  String url = baseUrl() + "/printer/filament_detect/set";
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

  // The endpoint answers {"state":"success"} — treat anything else as a
  // soft failure so the UI can surface it.
  if (r.body.indexOf("success") < 0 && r.body.indexOf("\"result\"") < 0) {
    r.error = "unexpected response: " + r.body;
    return r;
  }

  r.ok = true;
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
