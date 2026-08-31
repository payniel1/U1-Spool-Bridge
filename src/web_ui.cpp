#include "web_ui.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <WiFi.h>

#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "config.h"
#include "fleet_ota.h"
#include "decoders.h"
#include "ota.h"
#include "settings.h"
#include "tag_reader.h"
#include "u1_client.h"
#include "web_page.h"

static AsyncWebServer server(80);
// Outcome of the most recent browser firmware upload. Written only by the
// upload handler, read only by the completion handler that answers it.
static volatile bool s_otaLanded = false;
static String        s_otaError;
static bool                   s_otaFleet = false;   // push this on to the peers
static size_t                 s_otaBytes = 0;       // image length, for the push
static const esp_partition_t *s_otaPart  = nullptr; // where it landed

static AsyncWebSocket ws("/ws");

PendingWork g_work;
Lane        g_lanes[MAX_READERS];
uint8_t     g_activeLane = 0;
bool        g_printerOk = false;
bool        g_spoolmanOk = false;

// ---------------------------------------------------------------------------
// serialisation helpers
// ---------------------------------------------------------------------------

static void spoolToJson(const SpoolData &d, JsonObject o) {
  char uidHex[24];
  uidToHex(d.uid, d.uidLen, uidHex, sizeof(uidHex));

  o["source"]       = tagSourceName(d.source);
  o["vendor"]       = d.vendor;
  o["mainType"]     = d.mainType;
  o["subType"]      = d.subType;
  o["detailedType"] = d.detailedType;
  o["rgb"]          = d.rgb;
  o["rgb2"]         = d.rgb2;
  o["alpha"]        = d.alpha;
  o["hotendMin"]    = d.hotendMin;
  o["hotendMax"]    = d.hotendMax;
  o["bedTemp"]      = d.bedTemp;
  o["dryTemp"]      = d.dryTemp;
  o["weightG"]      = d.weightG;
  o["diameterUm"]   = d.diameterUm;
  o["lengthM"]      = d.lengthM;
  o["sku"]          = d.sku;
  o["skuStr"]       = d.skuStr;
  o["tray"]         = d.tray;
  o["prodDate"]     = d.prodDate;
  o["uid"]          = uidHex;
  o["cardType"]     = d.cardType;
  o["spoolmanId"]   = d.spoolmanId;
  o["remainingG"]   = d.remainingG;
}

static void spoolFromJson(JsonObjectConst o, SpoolData &d) {
  d.clear();
  d.valid  = true;
  d.source = SRC_MANUAL;
  snprintf(d.vendor,   sizeof(d.vendor),   "%s", (const char *)(o["vendor"]   | "Generic"));
  snprintf(d.mainType, sizeof(d.mainType), "%s", (const char *)(o["mainType"] | "PLA"));
  snprintf(d.subType,  sizeof(d.subType),  "%s", (const char *)(o["subType"]  | "Basic"));
  d.rgb       = (uint32_t)(o["rgb"] | 0);
  d.rgb2      = (uint32_t)(o["rgb2"] | 0);
  d.alpha     = (uint8_t)(o["alpha"] | 255);
  d.hotendMin = (uint16_t)(o["hotendMin"] | 0);
  d.hotendMax = (uint16_t)(o["hotendMax"] | 0);
  d.bedTemp   = (uint16_t)(o["bedTemp"] | 0);
  d.weightG   = (uint16_t)(o["weightG"] | 0);
  d.sku        = (uint32_t)(o["sku"] | 0);
  d.spoolmanId = (uint32_t)(o["spoolmanId"] | 0);
  d.remainingG = (uint16_t)(o["remainingG"] | 0);
  snprintf(d.cardType, sizeof(d.cardType), "%s", (const char *)(o["cardType"] | ""));

  // UID arrives as a hex string from the browser.
  const char *uidHex = o["uid"] | "";
  size_t      n = strlen(uidHex) / 2;
  if (n > sizeof(d.uid)) n = sizeof(d.uid);
  for (size_t i = 0; i < n; i++) {
    unsigned v;
    char pair[3] = {uidHex[i * 2], uidHex[i * 2 + 1], 0};
    if (sscanf(pair, "%02x", &v) != 1) { n = i; break; }
    d.uid[i] = (uint8_t)v;
  }
  d.uidLen = (uint8_t)n;

  normalizeForU1(d);
}

// ---------------------------------------------------------------------------
// broadcasts
// ---------------------------------------------------------------------------

void webBroadcastTag(uint8_t reader, const SpoolData &d, const String &note) {
  JsonDocument doc;
  doc["ev"]     = "tag";
  doc["reader"] = reader;
  doc["slot"]   = g_settings.readerChannel[reader] + 1;
  doc["note"]   = note;
  spoolToJson(d, doc["spool"].to<JsonObject>());
  String s;
  serializeJson(doc, s);
  ws.textAll(s);
}

const char *wifiBandName() {
  if (WiFi.status() != WL_CONNECTED) return "";
#if HAS_DUAL_BAND
  return WiFi.getBand() == WIFI_BAND_5G ? "5 GHz" : "2.4 GHz";
#else
  return "2.4 GHz";
#endif
}

void webBroadcastStatus() {
  JsonDocument doc;
  doc["ev"]           = "status";
  doc["version"]      = FW_VERSION;
  doc["box"]          = g_settings.boxName;
  doc["host"]         = g_settings.hostname;
  doc["readerCount"]  = READER_COUNT;
  doc["activeReader"] = g_activeLane;
  JsonArray lanes = doc["lanes"].to<JsonArray>();
  for (uint8_t i = 0; i < READER_COUNT; i++) {
    JsonObject l = lanes.add<JsonObject>();
    l["slot"]    = g_settings.readerChannel[i] + 1;
    l["ready"]   = g_readers[i].ready();
    l["bus"]     = g_readers[i].busName();
    l["present"] = g_lanes[i].spool.valid;
    l["pending"] = g_lanes[i].gate.havePending;
    l["armed"]   = g_lanes[i].gate.armed;
    // Reader resets since boot. Non-zero means this box's wiring is marginal and
    // wants looking at, even though it's currently working.
    l["resets"]  = g_readers[i].recoveries();
    if (!g_readers[i].ready()) l["err"] = g_readers[i].lastError();
    if (g_lanes[i].spool.valid) {
      l["label"] = String(g_lanes[i].spool.vendor) + " " + g_lanes[i].spool.mainType;
      l["rgb"]   = g_lanes[i].spool.rgb;
    }
  }
  doc["chip"]         = ESP.getChipModel();
  doc["dualBand"]     = (bool)HAS_DUAL_BAND;
  doc["band"]         = wifiBandName();
  doc["bandWanted"]   = g_settings.wifiBand;
  doc["bandFellBack"] = g_bandFellBack;
  doc["rssi"]         = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  doc["reader"]       = g_readers[0].ready();
  doc["resets"]       = g_readers[0].recoveries();
  doc["pn532"]        = g_readers[0].firmwareVersion()
                            ? String((g_readers[0].firmwareVersion() >> 16) & 0xFF) + "." +
                                  String((g_readers[0].firmwareVersion() >> 8) & 0xFF)
                            : "";
  doc["wifi"]         = WiFi.status() == WL_CONNECTED;
  doc["ip"]           = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString()
                                                      : WiFi.softAPIP().toString();
  doc["printer"]      = g_printerOk;
  doc["printerKnown"] = g_settings.printerHost[0] != '\0';
  doc["printerHost"]  = g_settings.printerHost;
  doc["spoolman"]     = g_spoolmanOk;
  doc["spoolmanOn"]   = g_settings.spoolmanEnabled && g_settings.spoolmanHost[0] != '\0';
  doc["uptime"]       = millis() / 1000;

  // Gating state — what the UI needs to explain why nothing was sent.
  doc["triggerMode"] = g_settings.triggerMode;
  doc["triggerName"] = triggerModeName(g_settings.triggerMode);
  const GateState &ag = g_lanes[g_activeLane].gate;
  doc["armed"]        = ag.armed;
  doc["armedChannel"] = ag.armedChannel;
  doc["pending"]      = ag.havePending;
  doc["pendingAgeS"]  = ag.havePending ? (millis() - ag.pendingAt) / 1000 : 0;
  doc["chanKnown"]    = g_chanKnown;

  // What the printer says is in each slot. Survives the spool leaving the
  // reader, which is when you actually want to check it.
  doc["slotsKnown"] = g_slotsKnown;
  if (g_slotsErr.length()) doc["slotsErr"] = g_slotsErr;

  // Which side is answering /printer/filament_detect/set, and whether anything
  // has actually told us or we are still running on the assumption.
  doc["backend"]      = u1BackendName(u1BackendEffective());
  doc["backendKnown"]     = u1BackendKnown();
  doc["backendConfirmed"] = u1BackendConfirmed();
  doc["backendPinned"]    = g_settings.printerBackend != U1_BACKEND_AUTO;
  doc["presenceOnly"] = u1SlotsPresenceOnly();
  JsonArray sl = doc["slots"].to<JsonArray>();
  for (int i = 0; i < 4; i++) {
    JsonObject o = sl.add<JsonObject>();
    o["n"]       = i + 1;
    o["present"] = g_slots[i].present;
    o["known"]   = g_slots[i].known;
    if (!g_slots[i].known) continue;
    o["vendor"]   = g_slots[i].vendor;
    o["mainType"] = g_slots[i].mainType;
    o["subType"]  = g_slots[i].subType;
    o["rgb"]      = g_slots[i].rgb;
    o["hotendMin"]= g_slots[i].hotendMin;
    o["hotendMax"]= g_slots[i].hotendMax;
    o["bedTemp"]  = g_slots[i].bedTemp;
    if (g_slots[i].uidHex[0])   o["uid"] = g_slots[i].uidHex;
    if (g_slots[i].cardType[0]) o["cardType"] = g_slots[i].cardType;
  }
  JsonArray ch = doc["chan"].to<JsonArray>();
  for (int i = 0; i < 4; i++) ch.add(g_chanPresent[i]);
  String s;
  serializeJson(doc, s);
  ws.textAll(s);
}

void webOtaEvent(const char *state, int pct, const char *msg) {
  JsonDocument doc;
  doc["ev"]    = "ota";
  doc["state"] = state;
  doc["pct"]   = pct;
  doc["msg"]   = msg ? msg : "";
  String s;
  serializeJson(doc, s);
  ws.textAll(s);
  Serial.printf("[ota] %s %d%% %s\n", state, pct, msg ? msg : "");
}

void webBriefJson(String &out) {
  JsonDocument doc;
  doc["box"]       = g_settings.boxName;
  doc["host"]      = g_settings.hostname;
  doc["printer"]   = g_settings.printerHost;
  doc["printerOk"] = g_printerOk;
  doc["version"]   = FW_VERSION;
  doc["uptime"]    = millis() / 1000;

  // For "Update all boxes". The IP means the browser posts to 192.168.x.y
  // rather than <host>.local, which matters because the .local name is
  // resolved by THIS box's mDNS query, not by the phone holding the page —
  // Android in particular often cannot resolve it at all. The fingerprint
  // lets the browser compare this box against the dropped image in the same
  // terms the image describes itself in.
  doc["ip"]          = WiFi.localIP().toString();
  doc["fingerprint"] = fwFingerprint();
  doc["bus"]         = FW_BUS_STR;
  doc["target"]      = FW_TARGET_STR;
  doc["otaEnabled"]  = g_settings.otaEnabled;
  doc["otaLocked"]   = g_settings.otaPassword[0] != 0;
  doc["rc"]          = READER_COUNT;
  doc["pins"]        = FW_PINS_STR;   // same chip, different board -> different triple
  doc["group"]       = g_groupName;   // "" means: group me by my printer
  doc["slot"]      = g_settings.readerChannel[0] + 1;  // headline slot

  JsonArray lanes = doc["lanes"].to<JsonArray>();
  bool anyPresent = false;
  for (uint8_t i = 0; i < READER_COUNT; i++) {
    const SpoolData &s = g_lanes[i].spool;
    JsonObject       l = lanes.add<JsonObject>();
    l["slot"]   = g_settings.readerChannel[i] + 1;
    l["reader"] = g_readers[i].ready();
    l["resets"] = g_readers[i].recoveries();

    bool present = s.valid && s.uidLen > 0;
    l["present"] = present;
    if (!present) continue;
    anyPresent = true;

    char uidHex[24];
    uidToHex(s.uid, s.uidLen, uidHex, sizeof(uidHex));
    l["spool"]      = String(s.vendor) + " " + s.mainType + " " + s.subType;
    l["rgb"]        = s.rgb;
    l["uid"]        = uidHex;
    l["spoolmanId"] = s.spoolmanId;
    l["remainingG"] = s.remainingG;
  }

  // Flattened view of lane 0, so a fleet tile can stay simple.
  doc["present"] = anyPresent;
  if (READER_COUNT == 1 && lanes[0]["present"].as<bool>()) {
    doc["spool"]      = lanes[0]["spool"];
    doc["rgb"]        = lanes[0]["rgb"];
    doc["remainingG"] = lanes[0]["remainingG"];
  }
  serializeJson(doc, out);
}

void webBroadcastFleet(const String &jsonArray, const String &err) {
  String s = "{\"ev\":\"fleet\",\"error\":\"" + err + "\",\"peers\":" +
             (jsonArray.length() ? jsonArray : "[]") + "}";
  ws.textAll(s);
}

void webBroadcastSpoolList(const String &jsonArray, const String &err) {
  // Hand-built so the (potentially large) array isn't copied into a second
  // JsonDocument just to be re-serialised.
  String s = "{\"ev\":\"spools\",\"error\":\"" + err + "\",\"spools\":" +
             (jsonArray.length() ? jsonArray : "[]") + "}";
  ws.textAll(s);
}

void webFleetPush(const String &peer, const char *state, const String &msg) {
  JsonDocument doc;
  doc["ev"]    = "fleetpush";
  doc["peer"]  = peer;
  doc["state"] = state;
  doc["msg"]   = msg;
  String out;
  serializeJson(doc, out);
  ws.textAll(out);
}

// The last dump this box produced. A dump can run for minutes, and the
// websocket does not always survive that — it used to mean the result was
// gone and the whole slow read had to be repeated, which is exactly the
// wrong thing to lose. Kept in RAM (a few kB) until the next dump or reboot.
static String s_lastDump;

void webDumpResult(const String &text) {
  s_lastDump = text;
  JsonDocument doc;
  doc["ev"]   = "dump";
  doc["text"] = text;
  String out;
  serializeJson(doc, out);
  ws.textAll(out);
}

// Progress, so the connection has something to carry while the read grinds
// on and the page can show where it is up to.
void webDumpProgress(uint8_t done, uint8_t total) {
  JsonDocument doc;
  doc["ev"]    = "dumpprog";
  doc["done"]  = done;
  doc["total"] = total;
  String out;
  serializeJson(doc, out);
  ws.textAll(out);
  ws.cleanupClients();   // the only thing keeping the socket tidy in here
}

void webLog(const String &msg, const char *level) {
  JsonDocument doc;
  doc["ev"]    = "log";
  doc["level"] = level;
  doc["msg"]   = msg;
  String s;
  serializeJson(doc, s);
  ws.textAll(s);
  Serial.printf("[log] %s\n", msg.c_str());
}

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------

// Accumulate a chunked request body into a String hung off the request.
static bool collectBody(AsyncWebServerRequest *req, uint8_t *data, size_t len,
                        size_t index, size_t total, String &out) {
  if (index == 0) {
    req->_tempObject = new String();
    ((String *)req->_tempObject)->reserve(total + 1);
  }
  String *buf = (String *)req->_tempObject;
  if (!buf) return false;
  for (size_t i = 0; i < len; i++) buf->concat((char)data[i]);
  if (index + len < total) return false;
  out = *buf;
  delete buf;
  req->_tempObject = nullptr;
  return true;
}

static void onWsEvent(AsyncWebSocket *, AsyncWebSocketClient *client,
                      AwsEventType type, void *, uint8_t *, size_t) {
  if (type == WS_EVT_CONNECT) {
    webBroadcastStatus();
    for (uint8_t i = 0; i < READER_COUNT; i++) {
      if (g_lanes[i].spool.valid) webBroadcastTag(i, g_lanes[i].spool, "");
    }
  }
}

void webBegin() {
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    // Serve straight out of flash rather than copying 14 kB into a String.
    req->send(req->beginResponse(200, "text/html", (const uint8_t *)INDEX_HTML,
                                 strlen_P(INDEX_HTML)));
  });

  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *req) {
    String out;
    settingsToJson(out);
    req->send(200, "application/json", out);
  });

  server.on(
      "/api/settings", HTTP_POST,
      [](AsyncWebServerRequest *) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index,
         size_t total) {
        String body;
        if (!collectBody(req, data, len, index, total, body)) return;

        String  oldSsid = g_settings.wifiSsid;
        uint8_t oldBand = g_settings.wifiBand;
        String  err;
        bool    ok = settingsFromJson(body, err);

        // The band has to be chosen before association, so changing it (or the
        // network) means a reconnect.
        bool reboot = ok && g_settings.wifiSsid[0] &&
                      (oldSsid != String(g_settings.wifiSsid) ||
                       oldBand != g_settings.wifiBand);

        // TX power, unlike the band, takes effect on a live association — so it
        // can be tried without a reboot. Handy when you're bisecting a box whose
        // reader keeps dropping out.
        if (ok && !reboot && WiFi.status() == WL_CONNECTED) {
          // 0 means "put it back where it was": the part's default is 20 dBm,
          // so restoring 19.5 would quietly leave a limit in place.
          WiFi.setTxPower(g_settings.wifiTxPower
                              ? (wifi_power_t)(g_settings.wifiTxPower * 4)
                              : WIFI_POWER_20dBm);
        }

        JsonDocument res;
        res["ok"]     = ok;
        res["error"]  = err;
        res["reboot"] = reboot;
        String out;
        serializeJson(res, out);
        req->send(ok ? 200 : 400, "application/json", out);
        if (reboot) g_work.reboot = true;
      });

  server.on(
      "/api/send", HTTP_POST,
      [](AsyncWebServerRequest *) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index,
         size_t total) {
        String body;
        if (!collectBody(req, data, len, index, total, body)) return;

        JsonDocument doc;
        if (deserializeJson(doc, body)) {
          req->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
          return;
        }
        uint8_t ch = (uint8_t)constrain((int)(doc["channel"] | 0), 0, 3);
        uint8_t rd = (uint8_t)constrain((int)(doc["reader"] | (int)g_activeLane),
                                        0, READER_COUNT - 1);
        SpoolData d;
        if (doc["spool"].is<JsonObject>()) {
          spoolFromJson(doc["spool"].as<JsonObjectConst>(), d);
        } else {
          d = g_lanes[rd].spool;
        }
        if (!d.valid) {
          req->send(400, "application/json",
                    "{\"ok\":false,\"error\":\"nothing to send\"}");
          return;
        }
        // Hand the blocking HTTP call to loop().
        g_work.spool   = d;
        g_work.channel = ch;
        g_work.reader  = rd;
        g_work.send    = true;
        req->send(200, "application/json", "{\"ok\":true,\"queued\":true}");
      });

  server.on("/api/reader", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (req->hasParam("i")) {
      int i = req->getParam("i")->value().toInt();
      g_activeLane = (uint8_t)constrain(i, 0, READER_COUNT - 1);
    }
    webBroadcastStatus();
    req->send(200, "application/json", "{\"ok\":true}");
  });

  // Read every sector of whatever is on the reader, with every key we know.
  // Collect the last dump, whether or not the socket survived producing it.
  server.on("/api/dump", HTTP_GET, [](AsyncWebServerRequest *req) {
    AsyncWebServerResponse *res = req->beginResponse(
        200, "text/plain; charset=utf-8",
        s_lastDump.length() ? s_lastDump
                            : String("No dump has been taken since this box booted.\n"));
    res->addHeader("Access-Control-Allow-Origin", "*");
    req->send(res);
  });

  server.on("/api/dump", HTTP_POST, [](AsyncWebServerRequest *req) {
    g_work.dump = true;
    req->send(200, "application/json", "{\"ok\":true,\"queued\":true}");
  });

  server.on("/api/rescan", HTTP_POST, [](AsyncWebServerRequest *req) {
    for (uint8_t i = 0; i < READER_COUNT; i++) g_readers[i].forgetLastTag();
    req->send(200, "application/json", "{\"ok\":true}");
  });

  // Reboot into a five-minute run with the radio off, to find out whether WiFi
  // is what's knocking the reader over. Watch it on the serial console — the
  // board is deliberately unreachable over the network while it runs, and comes
  // back on its own afterwards.
  server.on("/api/slots", HTTP_POST, [](AsyncWebServerRequest *req) {
    g_work.slots = true;   // loop() does the fetch; the answer arrives over the ws
    req->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/radiotest", HTTP_POST, [](AsyncWebServerRequest *req) {
    requestRadioTest();
    req->send(200, "application/json", "{\"ok\":true}");
    g_work.reboot = true;
  });

  // --- firmware upload from the browser ---------------------------------
  // The image is written on the web server's task as the multipart body
  // arrives. Nothing is committed until the final chunk verifies, so an
  // interrupted upload leaves the running firmware untouched.
  //
  // The HTTP response is authoritative about what happened. That used to be
  // untrue in a way that did not matter much and now would: the completion
  // handler judged success by Update.hasError(), which is false when Update
  // was never begun at all. So a refusal — OTA switched off, wrong password —
  // answered 200 {"ok":true} and set the reboot flag, and the box restarted
  // insisting it had updated. On one box you would notice the version had not
  // moved. Across eight, driven by "Update all boxes", you would get eight
  // reboots, eight green ticks and no new firmware anywhere.
  //
  // s_otaLanded is therefore set in exactly one place: after Update.end(true)
  // returns true. Everything else is a failure with a reason attached, and
  // that reason goes in the body, because a browser updating a PEER cannot
  // see that box's websocket — this response is the only thing it hears.
  server.on(
      "/api/ota", HTTP_POST,
      [](AsyncWebServerRequest *req) {
        AsyncWebServerResponse *res;
        if (s_otaLanded) {
          res = req->beginResponse(200, "application/json",
                                   String("{\"ok\":true,\"rebooting\":true,\"version\":\"") +
                                       FW_VERSION + "\"}");
        } else {
          String why = s_otaError.length() ? s_otaError : String("no image received");
          why.replace("\"", "'");
          res = req->beginResponse(500, "application/json",
                                   String("{\"ok\":false,\"error\":\"") + why + "\"}");
        }
        res->addHeader("Connection", "close");
        res->addHeader("Access-Control-Allow-Origin", "*");
        req->send(res);
        // With a fleet plan loaded the reboot waits: this box has to stay up to
        // hand the image to the others, and it reads that image out of the slot
        // it just wrote. It reboots at the end of the push, still last.
        if (s_otaLanded && !(s_otaFleet && fleetPlanPending())) g_otaRebootPending = true;
      },
      [](AsyncWebServerRequest *req, String filename, size_t index, uint8_t *data,
         size_t len, bool final) {
        if (index == 0) {
          s_otaLanded = false;
          s_otaError  = "";
          if (!g_settings.otaEnabled) {
            s_otaError = "OTA is disabled in settings";
            webOtaEvent("error", -1, s_otaError.c_str());
            return;
          }
          if (g_settings.otaPassword[0]) {
            String pw = req->hasParam("pw") ? req->getParam("pw")->value() : String();
            if (pw != g_settings.otaPassword) {
              s_otaError = "wrong OTA password";
              webOtaEvent("error", -1, s_otaError.c_str());
              return;
            }
          }
          otaSetBusy(true);
          s_otaFleet = req->hasParam("fleet");
          s_otaBytes = 0;
          // Captured BEFORE begin(): afterwards the boot partition has been
          // switched, and esp_ota_get_next_update_partition would name the
          // other slot — the one we did not write.
          s_otaPart = esp_ota_get_next_update_partition(NULL);
          webOtaEvent("start", 0, filename.c_str());
          if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            otaSetBusy(false);
            s_otaError = Update.errorString();
            webOtaEvent("error", -1, s_otaError.c_str());
            return;
          }
        }
        if (!otaBusy()) return;  // never got started

        if (Update.write(data, len) != len) {
          s_otaError = Update.errorString();
          webOtaEvent("error", -1, s_otaError.c_str());
          Update.abort();
          otaSetBusy(false);
          return;
        }

        s_otaBytes += len;
        otaNoteActivity();   // the transfer is alive; hold off the stall watchdog

        size_t total = req->contentLength();
        if (total) {
          int pct = (int)(((index + len) * 100ULL) / total);
          if (pct != otaProgressPct()) {
            otaSetProgress(pct);
            webOtaEvent("progress", pct, "");
          }
        }

        if (final) {
          if (Update.end(true)) {
            s_otaLanded = true;
            if (s_otaFleet && s_otaPart) fleetNoteImage(s_otaPart, s_otaBytes);
            webOtaEvent("done", 100,
                        s_otaFleet ? "sending it on to the other boxes" : "rebooting");
          } else {
            s_otaError = Update.errorString();
            webOtaEvent("error", -1, s_otaError.c_str());
          }
          otaSetBusy(false);
        }
      });

  // Rename a group: set it here and push the same name to every box in it.
  server.on(
      "/api/group", HTTP_POST,
      [](AsyncWebServerRequest *) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index,
         size_t total) {
        String body;
        if (!collectBody(req, data, len, index, total, body)) return;
        JsonDocument doc;
        if (deserializeJson(doc, body)) {
          req->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
          return;
        }
        const String name = doc["group"] | "";
        if (name.length() >= GROUP_NAME_MAX) {
          req->send(400, "application/json",
                    "{\"ok\":false,\"error\":\"that name is too long\"}");
          return;
        }
        String peers;
        for (JsonVariant v : doc["peers"].as<JsonArray>()) {
          if (peers.length()) peers += ",";
          peers += v.as<String>();
        }
        // Ours first, so the page it was typed on is right even if a peer is
        // unreachable.
        if (doc["includeSelf"] | true) {
          groupNameSet(name.c_str());
          settingsSave();
          g_work.smRefile = true;   // our own location just changed too
        }
        JsonDocument out;
        out["groupName"] = name;
        String payload;
        serializeJson(out, payload);
        fleetSetApply(payload, peers, true);
        g_work.groupApply = peers.length() > 0;
        req->send(200, "application/json", "{\"ok\":true}");
      });

  // Push one Spoolman location format to the whole fleet, so every box files
  // its spools under the same scheme. With {group} in it, Spoolman's location
  // grouping ends up matching the groups in the web UI.
  server.on(
      "/api/fleetfmt", HTTP_POST,
      [](AsyncWebServerRequest *) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index,
         size_t total) {
        String body;
        if (!collectBody(req, data, len, index, total, body)) return;
        JsonDocument doc;
        if (deserializeJson(doc, body)) {
          req->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
          return;
        }
        const String fmt = doc["locationFmt"] | "";
        if (fmt.length() >= (int)sizeof(g_settings.locationFmt)) {
          req->send(400, "application/json",
                    "{\"ok\":false,\"error\":\"that format is too long\"}");
          return;
        }
        String peers;
        for (JsonVariant v : doc["peers"].as<JsonArray>()) {
          if (peers.length()) peers += ",";
          peers += v.as<String>();
        }
        if (doc["includeSelf"] | true) {
          snprintf(g_settings.locationFmt, sizeof(g_settings.locationFmt), "%s",
                   fmt.c_str());
          settingsSave();
          g_work.smRefile = true;
        }
        JsonDocument out;
        out["locationFmt"] = fmt;
        String payload;
        serializeJson(out, payload);
        fleetSetApply(payload, peers, true);
        g_work.groupApply = peers.length() > 0;
        req->send(200, "application/json", "{\"ok\":true}");
      });

  // Asked of us by whichever box pushed the change.
  server.on("/api/spoolman/refile", HTTP_POST, [](AsyncWebServerRequest *req) {
    g_work.smRefile = true;
    req->send(200, "application/json", "{\"ok\":true,\"queued\":true}");
  });

  // The browser lodges the plan before it uploads: who to push to, the OTA
  // password to use, and the version the image carries so each peer can be
  // confirmed afterwards. Held in RAM only, and cleared once the push is done.
  server.on(
      "/api/fleetplan", HTTP_POST,
      [](AsyncWebServerRequest *) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index,
         size_t total) {
        String body;
        if (!collectBody(req, data, len, index, total, body)) return;
        JsonDocument doc;
        if (deserializeJson(doc, body)) {
          req->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
          return;
        }
        String peers;
        for (JsonVariant v : doc["peers"].as<JsonArray>()) {
          if (peers.length()) peers += ",";
          peers += v.as<String>();
        }
        fleetPlanSet(peers, doc["pw"] | "", doc["fw"] | "");
        req->send(200, "application/json",
                  String("{\"ok\":true,\"peers\":") + fleetPlanCount() + "}");
      });

  // Preflight for the above. Attaching an upload-progress listener to an XHR
  // makes the request non-simple, so the browser asks permission first before
  // posting a firmware image to a box it is not being served from.
  server.on("/api/ota", HTTP_OPTIONS, [](AsyncWebServerRequest *req) {
    AsyncWebServerResponse *res = req->beginResponse(204);
    res->addHeader("Access-Control-Allow-Origin", "*");
    res->addHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    res->addHeader("Access-Control-Allow-Headers", "Content-Type");
    res->addHeader("Access-Control-Max-Age", "600");
    req->send(res);
  });

  // Compact status, for the other boxes' fleet views.
  server.on("/api/brief", HTTP_GET, [](AsyncWebServerRequest *req) {
    String out;
    webBriefJson(out);
    // Readable cross-origin, because after pushing an image to a peer the
    // browser confirms the update by asking that peer what it is now running.
    // The HTTP response to the upload cannot answer that: the box reboots as
    // soon as the image verifies, usually before the reply is flushed.
    AsyncWebServerResponse *res = req->beginResponse(200, "application/json", out);
    res->addHeader("Access-Control-Allow-Origin", "*");
    req->send(res);
  });

  server.on("/api/fleet", HTTP_GET, [](AsyncWebServerRequest *req) {
    g_work.fleet = true;
    req->send(200, "application/json", "{\"ok\":true,\"queued\":true}");
  });

  // Arm a slot: the next spool to settle on the reader goes there.
  server.on("/api/arm", HTTP_GET, [](AsyncWebServerRequest *req) {
    int ch = req->hasParam("channel") ? req->getParam("channel")->value().toInt() : 0;
    g_work.armChannel = (uint8_t)constrain(ch, 0, 3);
    g_work.arm        = true;
    req->send(200, "application/json", "{\"ok\":true,\"queued\":true}");
  });

  server.on("/api/ping", HTTP_GET, [](AsyncWebServerRequest *req) {
    g_work.ping = true;
    g_work.smPing = g_settings.spoolmanEnabled;
    req->send(200, "application/json", "{\"ok\":true,\"queued\":true}");
  });

  // --- Spoolman ---------------------------------------------------------

  server.on("/api/spoolman/spools", HTTP_GET, [](AsyncWebServerRequest *req) {
    g_work.smList = true;
    req->send(200, "application/json", "{\"ok\":true,\"queued\":true}");
  });

  server.on(
      "/api/spoolman/link", HTTP_POST,
      [](AsyncWebServerRequest *) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index,
         size_t total) {
        String body;
        if (!collectBody(req, data, len, index, total, body)) return;
        JsonDocument doc;
        if (deserializeJson(doc, body)) {
          req->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
          return;
        }
        uint32_t id = (uint32_t)(doc["spoolId"] | 0);
        if (!id || activeSpool().uidLen == 0) {
          req->send(400, "application/json",
                    "{\"ok\":false,\"error\":\"need a spool id and a tag on the reader\"}");
          return;
        }
        g_work.smLinkId = id;
        g_work.smLink   = true;
        req->send(200, "application/json", "{\"ok\":true,\"queued\":true}");
      });

  // The OpenSpool JSON for the current spool — handy if you also want to burn
  // an NTAG215 with a phone.
  server.on("/api/tagjson", HTTP_GET, [](AsyncWebServerRequest *req) {
    char buf[400];
    size_t n = openspool_build_json(activeSpool(), buf, sizeof(buf));
    req->send(200, "application/json", n ? buf : "{}");
  });

  server.onNotFound([](AsyncWebServerRequest *req) {
    // Anything unknown goes to the UI, which makes the AP-mode captive portal
    // behave sensibly on most phones.
    req->redirect("/");
  });

  server.begin();
}

void webLoop() { ws.cleanupClients(); }
