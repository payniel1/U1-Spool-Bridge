#include "fleet_ota.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <esp_partition.h>

#include "fleet_wire.h"
#include "web_ui.h"

static String s_peers, s_pw, s_wantFw;
static const esp_partition_t *s_part = nullptr;
static size_t                 s_len  = 0;

void fleetPlanSet(const String &peersCsv, const String &pw, const String &wantFw) {
  s_peers  = peersCsv;
  s_pw     = pw;
  s_wantFw = wantFw;
}
void fleetPlanClear() { s_peers = ""; s_pw = ""; s_wantFw = ""; s_part = nullptr; s_len = 0; }
bool fleetPlanPending() { return s_peers.length() > 0 && s_part != nullptr && s_len > 0; }

uint8_t fleetPlanCount() {
  if (!s_peers.length()) return 0;
  uint8_t n = 1;
  for (unsigned i = 0; i < s_peers.length(); i++)
    if (s_peers[i] == ',') n++;
  return n;
}

void fleetNoteImage(const void *partition, size_t length) {
  s_part = (const esp_partition_t *)partition;
  s_len  = length;
}

// ---------------------------------------------------------------------------
// The body, streamed straight out of flash. Nothing buffers 1.5 MB anywhere —
// HTTPClient pulls a few kB at a time and each pull is served from the OTA
// partition on demand.
// ---------------------------------------------------------------------------
static bool readFromPartition(void *ctx, size_t offset, void *dst, size_t len) {
  const esp_partition_t *p = (const esp_partition_t *)ctx;
  return esp_partition_read(p, offset, dst, len) == ESP_OK;
}

class ImageBodyStream : public Stream {
 public:
  ImageBodyStream(const std::string &head, const std::string &tail,
                  const esp_partition_t *part, size_t imageLen)
      : _head(head), _tail(tail), _part(part), _img(imageLen) {}

  size_t total() const { return fleetBodyLength(_head.size(), _img, _tail.size()); }

  int    available() override { return (int)(total() - _pos); }
  int    peek() override { return -1; }
  void   flush() override {}
  size_t write(uint8_t) override { return 0; }
  int    read() override {
    uint8_t b;
    return readBytes((char *)&b, 1) == 1 ? b : -1;
  }

  size_t readBytes(char *dst, size_t n) override {
    const size_t got =
        fleetBodyRead(_pos, n, dst, _head.data(), _head.size(), _img, _tail.data(),
                      _tail.size(), readFromPartition, (void *)_part);
    _pos += got;
    return got;
  }

 private:
  std::string            _head, _tail;
  const esp_partition_t *_part;
  size_t                 _img;
  size_t                 _pos = 0;
};

// ---------------------------------------------------------------------------

static bool pushToPeer(const String &peer, String &errOut) {
  const std::string boundary = fleetBoundary((uint32_t)millis());
  const std::string head     = fleetMultipartHead(boundary, "firmware.bin");
  const std::string tail     = fleetMultipartTail(boundary);

  ImageBodyStream body(head, tail, s_part, s_len);

  WiFiClient  client;
  HTTPClient  http;
  String      url = "http://" + peer + "/api/ota";
  if (s_pw.length()) url += "?pw=" + s_pw;

  http.setTimeout(20000);
  http.setConnectTimeout(6000);
  if (!http.begin(client, url)) { errOut = "could not open " + url; return false; }
  http.addHeader("Content-Type", String("multipart/form-data; boundary=") + boundary.c_str());

  const int code = http.sendRequest("POST", (Stream *)&body, body.total());
  String    reply;
  if (code > 0) reply = http.getString();
  http.end();

  if (code <= 0) { errOut = "upload failed (" + String(code) + ")"; return false; }
  if (code != 200) {
    // The peer says why in the body from 1.12.0 on; older ones just say 500.
    errOut = reply.length() ? reply : ("HTTP " + String(code));
    if (errOut.length() > 90) errOut = errOut.substring(0, 90);
    return false;
  }
  return true;
}

// A peer reboots as soon as the image verifies, so the reply to the upload is
// not proof of anything. Ask it what it is running until it says the new one.
static bool confirmPeer(const String &peer, uint32_t budgetMs, String &errOut) {
  const uint32_t t0 = millis();
  bool           answered = false;
  String         lastSeen;
  while (millis() - t0 < budgetMs) {
    delay(2000);
    WiFiClient c;
    HTTPClient h;
    h.setTimeout(2500);
    h.setConnectTimeout(2500);
    if (h.begin(c, "http://" + peer + "/api/brief") && h.GET() == 200) {
      String body = h.getString();
      h.end();
      answered  = true;
      int v = body.indexOf("\"version\":\"");
      if (v >= 0) {
        int e = body.indexOf('"', v + 11);
        lastSeen = body.substring(v + 11, e);
        if (lastSeen == s_wantFw) return true;
      }
    } else {
      h.end();
    }
  }
  errOut = answered ? ("still running " + (lastSeen.length() ? lastSeen : String("the old build")))
                    : "no answer after the upload";
  return false;
}

void fleetPushRun() {
  if (!fleetPlanPending()) { fleetPlanClear(); return; }

  String rest = s_peers;
  int    n = 0, ok = 0;
  while (rest.length()) {
    const int c    = rest.indexOf(',');
    String    peer = (c < 0) ? rest : rest.substring(0, c);
    rest           = (c < 0) ? String("") : rest.substring(c + 1);
    peer.trim();
    if (!peer.length()) continue;
    n++;

    String err;
    webFleetPush(peer, "sending", "");
    if (!pushToPeer(peer, err)) {
      webFleetPush(peer, "fail", err);
      continue;
    }
    webFleetPush(peer, "waiting", "");
    if (!confirmPeer(peer, 60000, err)) {
      webFleetPush(peer, "fail", err);
      continue;
    }
    ok++;
    webFleetPush(peer, "ok", "now on " + s_wantFw);
  }

  webFleetPush("", "done", String(ok) + "/" + String(n));
  fleetPlanClear();
}

// ---------------------------------------------------------------------------
// Pushing a setting to the rest of the fleet.
//
// Renaming a group, or changing the Spoolman location format, has to reach the
// other boxes or you would type the same thing into eight Settings pages. Done
// from the box for the same reason the firmware push is: a browser posting to a
// box it was not served from is cross-origin, and /api/settings sends no CORS
// headers on any firmware. A box talking to a box has no such problem.
//
// The POST carries ONLY the keys being changed. Every field in settingsFromJson
// is guarded by isNull(), so absent keys are left exactly as they were — this
// cannot be collateral damage for a peer's Wi-Fi, printer or Spoolman config.
// ---------------------------------------------------------------------------
static String s_faPayload, s_faPeers;
static bool   s_faRefile = false;

void fleetSetApply(const String &payloadJson, const String &peersCsv, bool refile) {
  s_faPayload = payloadJson;
  s_faPeers   = peersCsv;
  s_faRefile  = refile;
}

static bool postJson(const String &url, const String &payload, int &codeOut) {
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(5000);
  http.setConnectTimeout(3000);
  if (!http.begin(client, url)) return false;
  http.addHeader("Content-Type", "application/json");
  codeOut = http.POST(payload);
  http.end();
  return codeOut > 0;
}

void fleetSetRun() {
  String rest = s_faPeers;
  int    n = 0, ok = 0;

  while (rest.length()) {
    const int c    = rest.indexOf(',');
    String    peer = (c < 0) ? rest : rest.substring(0, c);
    rest           = (c < 0) ? String("") : rest.substring(c + 1);
    peer.trim();
    if (!peer.length()) continue;
    n++;

    int code = 0;
    if (!postJson("http://" + peer + "/api/settings", s_faPayload, code) || code != 200) {
      webFleetPush(peer, "groupfail",
                   code > 0 ? ("HTTP " + String(code)) : String("no answer"));
      continue;
    }
    ok++;
    // Then ask it to re-file whatever it currently has loaded, so Spoolman
    // catches up instead of holding the location from before the change. A box
    // too old to know this endpoint answers its catch-all redirect; harmless.
    if (s_faRefile) {
      int rc = 0;
      postJson("http://" + peer + "/api/spoolman/refile", "{}", rc);
    }
    webFleetPush(peer, "groupok", "");
  }

  webFleetPush("", "groupdone", String(ok) + "/" + String(n));
  s_faPayload = "";
  s_faPeers   = "";
  s_faRefile  = false;
}
