#include "ota.h"

#include <ArduinoOTA.h>

#include "config.h"
#include "settings.h"
#include "web_ui.h"

volatile bool g_otaRebootPending = false;

// Emitted into .rodata so a browser can find it by scanning the raw .bin. It
// is `used` because nothing dereferences the array itself — fwFingerprint()
// hands out the pointer — and without that the linker is entitled to bin it.
extern "C" const char kFwFingerprint[] __attribute__((used)) = FW_FINGERPRINT;

const char *fwFingerprint() { return kFwFingerprint; }

static bool s_busy = false;
static int  s_pct = -1;

bool otaBusy() { return s_busy; }
int  otaProgressPct() { return s_pct; }
void otaSetBusy(bool busy) { s_busy = busy; if (!busy) s_pct = -1; }
void otaSetProgress(int pct) { s_pct = pct; }

void otaBegin() {
  if (!g_settings.otaEnabled) {
    Serial.println("OTA disabled in settings.");
    return;
  }

  ArduinoOTA.setHostname(g_settings.hostname);
  if (g_settings.otaPassword[0]) ArduinoOTA.setPassword(g_settings.otaPassword);

  ArduinoOTA.onStart([]() {
    s_busy = true;
    s_pct  = 0;
    // Nothing else should be touching the I2C bus or the network while an
    // image is being written.
    webOtaEvent("start", 0, ArduinoOTA.getCommand() == U_FLASH ? "firmware" : "filesystem");
  });

  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    int pct = total ? (int)((done * 100ULL) / total) : 0;
    if (pct != s_pct) {
      s_pct = pct;
      webOtaEvent("progress", pct, "");
    }
  });

  ArduinoOTA.onEnd([]() {
    s_pct = 100;
    webOtaEvent("done", 100, "rebooting");
    // ArduinoOTA reboots for us once this returns.
  });

  ArduinoOTA.onError([](ota_error_t err) {
    s_busy = false;
    s_pct  = -1;
    const char *msg = "unknown error";
    switch (err) {
      case OTA_AUTH_ERROR:    msg = "wrong OTA password"; break;
      case OTA_BEGIN_ERROR:   msg = "could not start (image too large?)"; break;
      case OTA_CONNECT_ERROR: msg = "connection lost"; break;
      case OTA_RECEIVE_ERROR: msg = "receive failed"; break;
      case OTA_END_ERROR:     msg = "image failed verification"; break;
    }
    webOtaEvent("error", -1, msg);
  });

  ArduinoOTA.begin();
  Serial.printf("OTA ready on %s.local%s\n", g_settings.hostname,
                g_settings.otaPassword[0] ? " (password set)" : " (no password)");
}

void otaLoop() {
  if (!g_settings.otaEnabled) return;
  ArduinoOTA.handle();
}
