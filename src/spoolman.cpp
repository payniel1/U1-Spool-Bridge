#include "spoolman.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <time.h>

#include "settings.h"
#include "spoolman_fields.h"

// The string handling lives in spoolman_fields.cpp (no Arduino types, so it is
// unit-tested on the host); these are just Arduino String adapters.
static inline std::string toStd(const String &s) { return std::string(s.c_str()); }
static inline String      toArd(const std::string &s) { return String(s.c_str()); }

#define MAX_UID_OWNERS 8

static String baseUrl() {
  String u = "http://";
  u += g_settings.spoolmanHost;
  u += ":";
  u += String(g_settings.spoolmanPort);
  return u;
}

bool spoolmanConfigured() {
  return g_settings.spoolmanEnabled && g_settings.spoolmanHost[0] != '\0';
}

// ---------------------------------------------------------------------------
// tiny HTTP helpers
// ---------------------------------------------------------------------------

static bool preflight(String &err) {
  if (WiFi.status() != WL_CONNECTED) { err = "no WiFi"; return false; }
  if (!spoolmanConfigured()) {
    err = g_settings.spoolmanHost[0]
              ? "Spoolman host is set but \"Use Spoolman\" is unticked"
              : "no Spoolman host set";
    return false;
  }
  return true;
}

static int httpDo(const char *method, const String &path, const String &payload,
                  String &body, String &err) {
  if (!preflight(err)) return -1;

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(8000);
  http.setReuse(false);
  if (!http.begin(client, baseUrl() + path)) {
    err = "could not open " + baseUrl() + path;
    return -1;
  }
  if (payload.length()) http.addHeader("Content-Type", "application/json");

  int code = payload.length() ? http.sendRequest(method, payload)
                              : http.sendRequest(method);
  body = http.getString();
  http.end();

  if (code <= 0) {
    err = String("HTTP error ") + HTTPClient::errorToString(code);
  } else if (code < 200 || code >= 300) {
    err = "Spoolman returned HTTP " + String(code) + ": " + body.substring(0, 120);
  }
  return code;
}

static String jsonUnquote(const char *encoded) {
  if (!encoded || !*encoded) return String();
  return toArd(smJsonUnquote(std::string(encoded)));
}

static String jsonQuote(const String &plain) { return toArd(smJsonQuote(toStd(plain))); }

static bool uidListContains(const String &list, const char *uid) {
  return smUidListContains(toStd(list), std::string(uid));
}

static String uidListRemove(const String &list, const char *uid) {
  return toArd(smUidListRemove(toStd(list), std::string(uid)));
}

static String uidListAdd(const String &list, const char *uid) {
  return toArd(smUidListAdd(toStd(list), std::string(uid)));
}

// ---------------------------------------------------------------------------
// info / bootstrap
// ---------------------------------------------------------------------------

bool spoolmanPing(String &versionOut, String &errOut) {
  String body;
  if (httpDo("GET", "/api/v1/info", "", body, errOut) != 200) return false;
  JsonDocument doc;
  if (deserializeJson(doc, body) == DeserializationError::Ok) {
    versionOut = (const char *)(doc["version"] | "unknown");
  } else {
    versionOut = "unknown";
  }
  return true;
}

bool spoolmanEnsureFields(String &errOut) {
  String body;
  if (httpDo("GET", "/api/v1/field/spool", "", body, errOut) != 200) return false;

  JsonDocument doc;
  if (deserializeJson(doc, body)) { errOut = "bad field list"; return false; }
  for (JsonVariant v : doc.as<JsonArray>()) {
    if (strcmp(v["key"] | "", SPOOLMAN_UID_FIELD) == 0) return true;  // already there
  }

  // Match SpoolLink's field so both tools read and write the same thing.
  JsonDocument req;
  req["name"]       = "Card UIDs";
  req["field_type"] = "text";
  req["order"]      = 0;
  String payload;
  serializeJson(req, payload);

  int code = httpDo("POST", "/api/v1/field/spool/" SPOOLMAN_UID_FIELD, payload, body, errOut);
  return code >= 200 && code < 300;
}

// ---------------------------------------------------------------------------
// lookup
// ---------------------------------------------------------------------------

struct UidOwner {
  uint32_t id;
  String   list;
};

// One pass over the inventory, pulling out only the spools that claim `uidHex`.
// The response is parsed through an ArduinoJson filter so a 300-spool
// inventory costs a few kB instead of a few hundred.
static bool scanUidOwners(const char *uidHex, UidOwner *owners, size_t maxOwners,
                          size_t &count, String &errOut) {
  count = 0;
  if (!preflight(errOut)) return false;

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(10000);
  http.setReuse(false);
  if (!http.begin(client, baseUrl() + "/api/v1/spool?allow_archived=false")) {
    errOut = "could not open Spoolman";
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    errOut = "spool list returned HTTP " + String(code);
    http.end();
    return false;
  }

  JsonDocument filter;
  JsonObject   f = filter.add<JsonObject>();
  f["id"] = true;
  f["extra"][SPOOLMAN_UID_FIELD] = true;

  JsonDocument doc;
  DeserializationError e =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (e) {
    errOut = String("could not parse spool list: ") + e.c_str();
    return false;
  }

  for (JsonVariant v : doc.as<JsonArray>()) {
    const char *raw = v["extra"][SPOOLMAN_UID_FIELD] | "";
    if (!raw[0]) continue;
    String list = jsonUnquote(raw);
    if (!uidListContains(list, uidHex)) continue;
    if (count < maxOwners) {
      owners[count].id   = (uint32_t)(v["id"] | 0);
      owners[count].list = list;
      count++;
    }
  }
  return true;
}

uint32_t spoolmanFindByUid(const char *uidHex, String &errOut) {
  UidOwner owners[MAX_UID_OWNERS];
  size_t   n = 0;
  if (!scanUidOwners(uidHex, owners, MAX_UID_OWNERS, n, errOut)) return 0;
  return n ? owners[0].id : 0;
}

bool spoolmanFetch(uint32_t id, SpoolmanSpool &out, String &errOut) {
  String body;
  if (httpDo("GET", "/api/v1/spool/" + String(id), "", body, errOut) != 200) return false;

  JsonDocument doc;
  if (deserializeJson(doc, body)) { errOut = "bad spool JSON"; return false; }

  out.found = true;
  out.id    = (uint32_t)(doc["id"] | 0);

  JsonObject fil = doc["filament"];
  snprintf(out.filamentName, sizeof(out.filamentName), "%s",
           (const char *)(fil["name"] | ""));
  snprintf(out.vendor, sizeof(out.vendor), "%s",
           (const char *)(fil["vendor"]["name"] | ""));
  snprintf(out.material, sizeof(out.material), "%s",
           (const char *)(fil["material"] | ""));
  snprintf(out.colorHex, sizeof(out.colorHex), "%s",
           (const char *)(fil["color_hex"] | ""));
  out.extruderTemp = (uint16_t)(fil["settings_extruder_temp"] | 0);
  out.bedTemp      = (uint16_t)(fil["settings_bed_temp"] | 0);
  out.diameter     = fil["diameter"] | 0.0f;

  out.remainingWeight = doc["remaining_weight"].isNull() ? -1.0f
                                                         : doc["remaining_weight"].as<float>();
  out.initialWeight   = doc["initial_weight"].isNull() ? -1.0f
                                                       : doc["initial_weight"].as<float>();
  snprintf(out.location, sizeof(out.location), "%s",
           (const char *)(doc["location"] | ""));
  out.comment = (const char *)(doc["comment"] | "");

  const char *variant = doc["extra"]["variant"] | "";
  if (variant[0]) {
    String v = jsonUnquote(variant);
    snprintf(out.variant, sizeof(out.variant), "%s", v.c_str());
  }
  return true;
}

void spoolmanApply(const SpoolmanSpool &s, SpoolData &d) {
  if (!s.found) return;

  // A blank NTAG carries nothing, but if Spoolman knows the UID we have a
  // complete spool anyway — that's the whole point of registering tags.
  if (!d.valid || d.source == SRC_UNKNOWN) {
    d.valid  = true;
    d.source = SRC_SPOOLMAN;
  }

  d.spoolmanId = s.id;
  if (s.remainingWeight >= 0) d.remainingG = (uint16_t)(s.remainingWeight + 0.5f);

  // Spoolman is the curated record — it wins wherever it has an opinion.
  if (s.vendor[0])   snprintf(d.vendor, sizeof(d.vendor), "%s", s.vendor);
  if (s.material[0]) {
    normalizeMainType(s.material, d.mainType, sizeof(d.mainType));
    snprintf(d.detailedType, sizeof(d.detailedType), "%s",
             s.filamentName[0] ? s.filamentName : s.material);
  }
  if (s.variant[0]) {
    snprintf(d.subType, sizeof(d.subType), "%s",
             mapSubTypeForU1(s.variant, d.mainType));
  } else if (s.filamentName[0]) {
    snprintf(d.subType, sizeof(d.subType), "%s",
             mapSubTypeForU1(s.filamentName, d.mainType));
  }

  if (s.colorHex[0]) {
    uint32_t rgb = 0;
    uint8_t  a   = d.alpha;
    if (parseHexColor(s.colorHex, &rgb, &a)) { d.rgb = rgb; d.alpha = a; }
  }

  // Spoolman stores one extruder temperature, the U1 wants a window. Centre a
  // modest range on it and keep the tag's window if Spoolman has nothing.
  if (s.extruderTemp) {
    d.hotendMin = s.extruderTemp > 10 ? s.extruderTemp - 10 : s.extruderTemp;
    d.hotendMax = s.extruderTemp + 10;
  }
  if (s.bedTemp) d.bedTemp = s.bedTemp;
  if (s.diameter > 0.5f && s.diameter < 5.0f) {
    d.diameterUm = (uint16_t)(s.diameter * 1000.0f + 0.5f);
  }
  if (s.remainingWeight > 0) d.weightG = (uint16_t)(s.remainingWeight + 0.5f);
}

// ---------------------------------------------------------------------------
// writes
// ---------------------------------------------------------------------------

static bool patchSpool(uint32_t id, const String &payload, String &errOut) {
  String body;
  int    code = httpDo("PATCH", "/api/v1/spool/" + String(id), payload, body, errOut);
  return code >= 200 && code < 300;
}

bool spoolmanLinkUid(uint32_t spoolId, const char *uidHex, String &errOut) {
  UidOwner owners[MAX_UID_OWNERS];
  size_t   n = 0;
  if (!scanUidOwners(uidHex, owners, MAX_UID_OWNERS, n, errOut)) return false;

  // Detach the UID from anything else holding it, or the next lookup is a
  // coin flip between two spools.
  for (size_t i = 0; i < n; i++) {
    if (owners[i].id == spoolId) continue;
    String stripped = uidListRemove(owners[i].list, uidHex);
    JsonDocument patch;
    patch["extra"][SPOOLMAN_UID_FIELD] = jsonQuote(stripped);
    String payload;
    serializeJson(patch, payload);
    String ignored;
    patchSpool(owners[i].id, payload, ignored);  // best effort
  }

  // Read the target's current list so we append rather than overwrite.
  String body;
  String current;
  if (httpDo("GET", "/api/v1/spool/" + String(spoolId), "", body, errOut) != 200) {
    return false;
  }
  {
    JsonDocument doc;
    if (deserializeJson(doc, body)) { errOut = "bad spool JSON"; return false; }
    current = jsonUnquote(doc["extra"][SPOOLMAN_UID_FIELD] | "");
  }

  JsonDocument patch;
  patch["extra"][SPOOLMAN_UID_FIELD] = jsonQuote(uidListAdd(current, uidHex));
  String payload;
  serializeJson(patch, payload);
  return patchSpool(spoolId, payload, errOut);
}

static String formatLocation(uint8_t channel) {
  return toArd(smFormatLocation(std::string(g_settings.locationFmt), channel + 1,
                                std::string(g_groupName),
                                std::string(g_settings.boxName)));
}

static String timestampPrefix() {
  time_t    now = time(nullptr);
  struct tm tm;
  if (now > 1700000000 && localtime_r(&now, &tm)) {  // NTP has landed
    char buf[24];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return String(buf) + " ";
  }
  return String();  // no clock yet — skip the date rather than print a fake one
}

bool spoolmanNoteLoad(uint32_t spoolId, uint8_t channel, String &errOut) {
  if (!spoolId) return true;

  SpoolmanSpool s;
  if (!spoolmanFetch(spoolId, s, errOut)) return false;

  String line = timestampPrefix() + "loaded into " + formatLocation(channel);
  // Spoolman caps comments at 1024 characters.
  String comment = toArd(smAppendComment(toStd(s.comment), toStd(line), 1000));

  JsonDocument patch;
  if (g_settings.spoolmanSetLocation) patch["location"] = formatLocation(channel);
  if (g_settings.spoolmanNoteLoads)   patch["comment"]  = comment;
  String payload;
  serializeJson(patch, payload);
  return patchSpool(spoolId, payload, errOut);
}

// Re-file a spool that is already loaded, after the location format or the
// group name changed under it. Location only: spoolmanNoteLoad also appends a
// line to the spool's comment, and renaming a group three times should not
// leave three "loaded into" entries describing a load that happened once.
bool spoolmanRefileLocation(uint32_t spoolId, uint8_t channel, String &errOut) {
  if (!spoolId || !g_settings.spoolmanSetLocation) return true;
  JsonDocument patch;
  patch["location"] = formatLocation(channel);
  String payload;
  serializeJson(patch, payload);
  return patchSpool(spoolId, payload, errOut);
}

// ---------------------------------------------------------------------------
// picker list
// ---------------------------------------------------------------------------

bool spoolmanListForPicker(String &jsonOut, String &errOut) {
  if (!preflight(errOut)) return false;

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(10000);
  http.setReuse(false);
  if (!http.begin(client, baseUrl() +
                              "/api/v1/spool?allow_archived=false&sort=filament.name:asc")) {
    errOut = "could not open Spoolman";
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    errOut = "spool list returned HTTP " + String(code);
    http.end();
    return false;
  }

  JsonDocument filter;
  JsonObject   f = filter.add<JsonObject>();
  f["id"] = true;
  f["remaining_weight"] = true;
  f["location"] = true;
  f["filament"]["name"] = true;
  f["filament"]["material"] = true;
  f["filament"]["color_hex"] = true;
  f["filament"]["vendor"]["name"] = true;
  f["extra"][SPOOLMAN_UID_FIELD] = true;

  JsonDocument doc;
  DeserializationError e =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (e) { errOut = String("could not parse spool list: ") + e.c_str(); return false; }

  JsonDocument out;
  JsonArray    arr = out.to<JsonArray>();
  for (JsonVariant v : doc.as<JsonArray>()) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = v["id"] | 0;
    String label = String((const char *)(v["filament"]["vendor"]["name"] | "")) + " " +
                   String((const char *)(v["filament"]["name"] | ""));
    label.trim();
    if (!label.length()) label = "spool #" + String((int)(v["id"] | 0));
    o["label"]     = label;
    o["material"]  = (const char *)(v["filament"]["material"] | "");
    o["color"]     = (const char *)(v["filament"]["color_hex"] | "");
    o["location"]  = (const char *)(v["location"] | "");
    o["remaining"] = v["remaining_weight"].isNull() ? -1 : v["remaining_weight"].as<int>();
    o["tagged"]    = jsonUnquote(v["extra"][SPOOLMAN_UID_FIELD] | "").length() > 0;
  }
  serializeJson(out, jsonOut);
  return true;
}
