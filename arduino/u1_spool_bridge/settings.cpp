#include "settings.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <ctype.h>

#include "config.h"
#include "send_gate.h"

Settings g_settings;

static const char *NS = "u1bridge";
static Preferences prefs;

char g_groupName[GROUP_NAME_MAX] = {0};

void groupNameSet(const char *v) {
  snprintf(g_groupName, sizeof(g_groupName), "%s", v ? v : "");
}

// Its own key, not part of the blob. See the layout guard in settings.h.
static void groupLoad() {
  Preferences p;
  if (!p.begin(NS, true)) return;
  String v = p.getString("group", "");
  p.end();
  groupNameSet(v.c_str());
}
static void groupSave() {
  Preferences p;
  if (!p.begin(NS, false)) return;
  p.putString("group", g_groupName);
  p.end();
}

void Settings::loadDefaults() {
  memset(this, 0, sizeof(Settings));
  version = SETTINGS_VERSION;

  // Name every board after its own MAC so eight fresh units don't all answer
  // to the same mDNS name — you can reach and rename them all at once.
  uint64_t mac = ESP.getEfuseMac();
  snprintf(boxName, sizeof(boxName), "Box %02X%02X",
           (unsigned)((mac >> 32) & 0xFF), (unsigned)((mac >> 40) & 0xFF));
  sendOnBoot = true;

  snprintf(hostname, sizeof(hostname), "u1-spool-bridge");
  wifiBand           = BAND_AUTO;
  wifiTxPower        = 0;    // 0 = leave the radio at its default (max)
  printerPort        = 80;   // Moonraker is proxied on :80 on the U1; 7125 also works
  for (uint8_t i = 0; i < MAX_READERS; i++) readerChannel[i] = i & 3;
  forceGenericVendor = false;
  sendCardUid        = true;
  scanIntervalMs     = 400;

  triggerMode  = TRIG_ON_INSERT;  // one reader per box is the common case
  dwellMs      = 700;
  absenceMs    = 3000;
  cooldownS    = 30;
  scanValidS   = 300;
  armTimeoutS  = 120;
  statePollMs  = 1000;

  spoolmanPort        = 7912;  // Spoolman's default
  spoolmanSetLocation = true;
  spoolmanNoteLoads   = true;

  // Compile-time fleet defaults, so eight freshly flashed boards join the
  // network and find the printer without eight rounds of AP setup.
  snprintf(wifiSsid, sizeof(wifiSsid), "%s", DEFAULT_WIFI_SSID);
  snprintf(wifiPass, sizeof(wifiPass), "%s", DEFAULT_WIFI_PASS);
  snprintf(printerHost, sizeof(printerHost), "%s", DEFAULT_PRINTER_HOST);
  snprintf(spoolmanHost, sizeof(spoolmanHost), "%s", DEFAULT_SPOOLMAN_HOST);
  spoolmanEnabled = spoolmanHost[0] != '\0';
  // The whole point of the location is to be the GROUP heading in Spoolman, so
  // every box feeding one printer files under the same string. Adding {slot}
  // here would split that back into one location per box.
  //
  // {group} falls back to the box name, so an unconfigured box still gets
  // something of its own rather than an empty location.
  snprintf(locationFmt, sizeof(locationFmt), "{group}");
  otaEnabled = true;
  snprintf(ntpServer, sizeof(ntpServer), "pool.ntp.org");
  snprintf(timezone, sizeof(timezone), "UTC0");
  // Well-known MIFARE Classic keys worth trying on non-Bambu tags.
  snprintf(extraKeys[0], 13, "FFFFFFFFFFFF");
  snprintf(extraKeys[1], 13, "A0A1A2A3A4A5");
  snprintf(extraKeys[2], 13, "D3F7D3F7D3F7");
}

void settingsDeriveHostname() {
  char  *o = g_settings.hostname;
  size_t cap = sizeof(g_settings.hostname);
  size_t n = 0;
  for (const char *p = "u1-"; *p && n + 1 < cap; p++) o[n++] = *p;

  bool lastDash = true;
  for (const char *p = g_settings.boxName; *p && n + 1 < cap; p++) {
    if (isalnum((unsigned char)*p)) {
      o[n++] = (char)tolower((unsigned char)*p);
      lastDash = false;
    } else if (!lastDash) {
      o[n++] = '-';
      lastDash = true;
    }
  }
  while (n > 3 && o[n - 1] == '-') n--;
  if (n <= 3) {  // boxName was empty or all punctuation
    snprintf(o, cap, "u1-spool-bridge");
    return;
  }
  o[n] = '\0';
}

bool settingsLoad() {
  g_settings.loadDefaults();
  groupLoad();

  if (!prefs.begin(NS, true)) return false;
  const size_t n = prefs.getBytesLength("blob");
  if (n == 0 || n > sizeof(Settings)) {
    prefs.end();
    return false;   // nothing stored, or written by something newer than us
  }

  // Overlay the stored bytes onto a defaulted struct rather than demanding an
  // exact size match. A blob written by an older build is SHORTER, and every
  // field it does contain is still at the offset it was written at (the layout
  // guard in settings.h is what keeps that true). The tail it cannot reach
  // simply keeps its default.
  //
  // The previous version required n == sizeof(Settings) and fell back to
  // defaults otherwise, which meant that appending a single field to this
  // struct would have made every box in a deployed fleet forget its Wi-Fi
  // credentials on the next update and come back up as an access point.
  Settings tmp;
  tmp.loadDefaults();
  if (prefs.getBytes("blob", &tmp, sizeof(Settings)) != n) {
    prefs.end();
    return false;
  }
  prefs.end();

  const uint16_t stored = tmp.version;
  if (stored < SETTINGS_MIN_COMPATIBLE || stored > SETTINGS_VERSION) {
    return false;   // a layout we have no business reinterpreting
  }

  tmp.version = SETTINGS_VERSION;
  g_settings  = tmp;
  if (n != sizeof(Settings) || stored != SETTINGS_VERSION) {
    settingsSave();   // rewrite in the current layout so this happens once
  }
  return true;
}

bool settingsSave() {
  groupSave();
  if (!prefs.begin(NS, false)) return false;
  size_t w = prefs.putBytes("blob", &g_settings, sizeof(Settings));
  prefs.end();
  return w == sizeof(Settings);
}

void settingsToJson(String &out) {
  JsonDocument doc;
  doc["wifiSsid"]           = g_settings.wifiSsid;
  doc["wifiPassSet"]        = g_settings.wifiPass[0] != '\0';  // never echo the password
  doc["hostname"]           = g_settings.hostname;
  doc["wifiBand"]           = g_settings.wifiBand;
  doc["wifiTxPower"]        = g_settings.wifiTxPower;
  doc["dualBand"]           = (bool)HAS_DUAL_BAND;
  doc["printerHost"]        = g_settings.printerHost;
  doc["printerPort"]        = g_settings.printerPort;
  doc["apiKeySet"]          = g_settings.apiKey[0] != '\0';
  doc["readerCount"]        = READER_COUNT;
  JsonArray rc = doc["readerChannel"].to<JsonArray>();
  for (uint8_t i = 0; i < MAX_READERS; i++) rc.add(g_settings.readerChannel[i]);
  doc["boxName"]            = g_settings.boxName;
  doc["groupName"]          = g_groupName;
  doc["sendOnBoot"]         = g_settings.sendOnBoot;
  doc["triggerMode"]        = g_settings.triggerMode;
  doc["dwellMs"]            = g_settings.dwellMs;
  doc["absenceMs"]          = g_settings.absenceMs;
  doc["cooldownS"]          = g_settings.cooldownS;
  doc["scanValidS"]         = g_settings.scanValidS;
  doc["armTimeoutS"]        = g_settings.armTimeoutS;
  doc["statePollMs"]        = g_settings.statePollMs;
  doc["forceGenericVendor"] = g_settings.forceGenericVendor;
  doc["sendCardUid"]        = g_settings.sendCardUid;
  doc["scanIntervalMs"]     = g_settings.scanIntervalMs;
  doc["spoolmanEnabled"]     = g_settings.spoolmanEnabled;
  doc["spoolmanHost"]        = g_settings.spoolmanHost;
  doc["spoolmanPort"]        = g_settings.spoolmanPort;
  doc["spoolmanSetLocation"] = g_settings.spoolmanSetLocation;
  doc["spoolmanNoteLoads"]   = g_settings.spoolmanNoteLoads;
  doc["locationFmt"]         = g_settings.locationFmt;
  doc["otaEnabled"]          = g_settings.otaEnabled;
  doc["otaPasswordSet"]      = g_settings.otaPassword[0] != '\0';
  doc["ntpServer"]           = g_settings.ntpServer;
  doc["timezone"]            = g_settings.timezone;
  JsonArray keys = doc["extraKeys"].to<JsonArray>();
  for (int i = 0; i < MAX_EXTRA_KEYS; i++) {
    if (g_settings.extraKeys[i][0]) keys.add(g_settings.extraKeys[i]);
  }
  serializeJson(doc, out);
}

static void copyIfPresent(JsonVariant v, char *dst, size_t dstLen) {
  if (v.isNull()) return;
  const char *s = v.as<const char *>();
  if (!s) return;
  snprintf(dst, dstLen, "%s", s);
}

// A host field gets pasted into, not typed into. People arrive with
// "http://192.168.1.20:7912/" from their browser bar, and storing that raw
// builds "http://http://192.168.1.20:7912/:7912" — which fails with an error
// that names a URL nobody reads closely enough to spot the doubling. Take the
// hostname out of whatever was pasted, and lift a port out of it if one is
// there rather than throwing it away.
static void tidyHost(char *host, size_t cap, uint16_t *port) {
  String h(host);
  h.trim();
  if (h.startsWith("http://"))  h = h.substring(7);
  else if (h.startsWith("https://")) h = h.substring(8);

  int slash = h.indexOf('/');
  if (slash >= 0) h = h.substring(0, slash);      // drop any path

  int colon = h.lastIndexOf(':');
  if (colon > 0) {
    long p = h.substring(colon + 1).toInt();
    if (p > 0 && p <= 65535 && port) *port = (uint16_t)p;
    h = h.substring(0, colon);
  }
  h.trim();
  snprintf(host, cap, "%s", h.c_str());
}

bool settingsFromJson(const String &json, String &err) {
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, json);
  if (e) { err = e.c_str(); return false; }

  copyIfPresent(doc["boxName"],     g_settings.boxName,     sizeof(g_settings.boxName));
  copyIfPresent(doc["groupName"],   g_groupName,            sizeof(g_groupName));
  copyIfPresent(doc["wifiSsid"],    g_settings.wifiSsid,    sizeof(g_settings.wifiSsid));
  copyIfPresent(doc["hostname"],    g_settings.hostname,    sizeof(g_settings.hostname));
  copyIfPresent(doc["printerHost"],  g_settings.printerHost,  sizeof(g_settings.printerHost));
  copyIfPresent(doc["spoolmanHost"], g_settings.spoolmanHost, sizeof(g_settings.spoolmanHost));
  copyIfPresent(doc["locationFmt"],  g_settings.locationFmt,  sizeof(g_settings.locationFmt));
  copyIfPresent(doc["ntpServer"],    g_settings.ntpServer,    sizeof(g_settings.ntpServer));
  copyIfPresent(doc["timezone"],     g_settings.timezone,     sizeof(g_settings.timezone));

  // Blank password / api key means "leave unchanged".
  const char *pw = doc["wifiPass"] | "";
  if (pw[0]) snprintf(g_settings.wifiPass, sizeof(g_settings.wifiPass), "%s", pw);
  const char *ak = doc["apiKey"] | "";
  if (ak[0]) snprintf(g_settings.apiKey, sizeof(g_settings.apiKey), "%s", ak);
  // "-" clears the OTA password; blank leaves it alone.
  const char *op = doc["otaPassword"] | "";
  if (op[0] == '-' && op[1] == '\0') g_settings.otaPassword[0] = '\0';
  else if (op[0]) snprintf(g_settings.otaPassword, sizeof(g_settings.otaPassword), "%s", op);

  if (!doc["wifiBand"].isNull())
    g_settings.wifiBand = (uint8_t)constrain(doc["wifiBand"].as<int>(), 0, 2);
  // 0 means "don't touch it". Anything else is clamped to the range the radio
  // will actually accept; 8 dBm is about as low as is useful indoors.
  if (!doc["wifiTxPower"].isNull()) {
    int t = doc["wifiTxPower"].as<int>();
    g_settings.wifiTxPower = t <= 0 ? 0 : (uint8_t)constrain(t, 8, 20);
  }
  if (!doc["printerPort"].isNull())
    g_settings.printerPort = (uint16_t)doc["printerPort"].as<int>();
  // Accept either the array or, for a single-reader node, the plain scalar the
  // provisioning one-liners in the README use.
  if (doc["readerChannel"].is<JsonArray>()) {
    uint8_t i = 0;
    for (JsonVariant v : doc["readerChannel"].as<JsonArray>()) {
      if (i >= MAX_READERS) break;
      g_settings.readerChannel[i++] = (uint8_t)constrain(v.as<int>(), 0, 3);
    }
  } else if (!doc["defaultChannel"].isNull()) {
    g_settings.readerChannel[0] = (uint8_t)constrain(doc["defaultChannel"].as<int>(), 0, 3);
  }
  if (!doc["otaEnabled"].isNull())
    g_settings.otaEnabled = doc["otaEnabled"].as<bool>();
  if (!doc["sendOnBoot"].isNull())
    g_settings.sendOnBoot = doc["sendOnBoot"].as<bool>();
  if (!doc["triggerMode"].isNull())
    g_settings.triggerMode = (uint8_t)constrain(doc["triggerMode"].as<int>(), 0, 4);
  if (!doc["dwellMs"].isNull())
    g_settings.dwellMs = (uint16_t)constrain(doc["dwellMs"].as<int>(), 0, 10000);
  if (!doc["absenceMs"].isNull())
    g_settings.absenceMs = (uint16_t)constrain(doc["absenceMs"].as<int>(), 200, 60000);
  if (!doc["cooldownS"].isNull())
    g_settings.cooldownS = (uint16_t)constrain(doc["cooldownS"].as<int>(), 0, 3600);
  if (!doc["scanValidS"].isNull())
    g_settings.scanValidS = (uint16_t)constrain(doc["scanValidS"].as<int>(), 5, 3600);
  if (!doc["armTimeoutS"].isNull())
    g_settings.armTimeoutS = (uint16_t)constrain(doc["armTimeoutS"].as<int>(), 5, 3600);
  if (!doc["statePollMs"].isNull())
    g_settings.statePollMs = (uint16_t)constrain(doc["statePollMs"].as<int>(), 250, 10000);
  if (!doc["forceGenericVendor"].isNull())
    g_settings.forceGenericVendor = doc["forceGenericVendor"].as<bool>();
  if (!doc["sendCardUid"].isNull())
    g_settings.sendCardUid = doc["sendCardUid"].as<bool>();
  if (!doc["scanIntervalMs"].isNull())
    g_settings.scanIntervalMs = (uint16_t)constrain(doc["scanIntervalMs"].as<int>(), 100, 5000);

  if (!doc["spoolmanEnabled"].isNull())
    g_settings.spoolmanEnabled = doc["spoolmanEnabled"].as<bool>();
  if (!doc["spoolmanPort"].isNull())
    g_settings.spoolmanPort = (uint16_t)doc["spoolmanPort"].as<int>();
  if (!doc["spoolmanSetLocation"].isNull())
    g_settings.spoolmanSetLocation = doc["spoolmanSetLocation"].as<bool>();
  if (!doc["spoolmanNoteLoads"].isNull())
    g_settings.spoolmanNoteLoads = doc["spoolmanNoteLoads"].as<bool>();

  // Both hosts, after their ports have been read, so a port pasted into the host
  // field wins over a stale value in the port field rather than being discarded.
  tidyHost(g_settings.printerHost,  sizeof(g_settings.printerHost),  &g_settings.printerPort);
  tidyHost(g_settings.spoolmanHost, sizeof(g_settings.spoolmanHost), &g_settings.spoolmanPort);

  if (doc["extraKeys"].is<JsonArray>()) {
    memset(g_settings.extraKeys, 0, sizeof(g_settings.extraKeys));
    int i = 0;
    for (JsonVariant v : doc["extraKeys"].as<JsonArray>()) {
      if (i >= MAX_EXTRA_KEYS) break;
      const char *s = v.as<const char *>();
      if (s && strlen(s) == 12) snprintf(g_settings.extraKeys[i++], 13, "%s", s);
    }
  }

  settingsDeriveHostname();
  if (!settingsSave()) { err = "NVS write failed"; return false; }
  return true;
}
