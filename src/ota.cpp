#include "ota.h"

#include <ArduinoOTA.h>
#include <Update.h>

#include "config.h"
#include "ota_stall.h"
#include "settings.h"
#include "web_ui.h"

volatile bool g_otaRebootPending = false;

// Emitted into .rodata so a browser can find it by scanning the raw .bin. It
// is `used` because nothing dereferences the array itself — fwFingerprint()
// hands out the pointer — and without that the linker is entitled to bin it.
extern "C" const char kFwFingerprint[] __attribute__((used)) = FW_FINGERPRINT;

const char *fwFingerprint() { return kFwFingerprint; }

static bool     s_busy = false;
static int      s_pct = -1;
static uint32_t s_lastActivity = 0;

bool otaBusy() { return s_busy; }
int  otaProgressPct() { return s_pct; }
void otaNoteActivity() { s_lastActivity = millis(); }

void otaSetBusy(bool busy) {
  s_busy = busy;
  if (busy) s_lastActivity = millis();   // start the clock, don't inherit it
  else      s_pct = -1;
}
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
  // Ahead of the otaEnabled check on purpose: a box wedged by a dead upload
  // has to be able to free itself even if OTA was switched off in the
  // meantime, or the setting becomes a way to make the box permanently deaf.
  if (otaStalled(s_busy, millis(), s_lastActivity, OTA_STALL_MS)) {
    // Only the browser/fleet upload path can get stuck like this. ArduinoOTA
    // has its own timeout and clears s_busy through onError, so by the time we
    // are here there is no espota transfer to interrupt.
    Update.abort();
    otaSetBusy(false);
    webOtaEvent("error", -1,
                "upload stopped part-way — abandoned it, the box is back to normal");
    Serial.println("OTA: upload stalled, aborted; box is live again");
  }

  if (!g_settings.otaEnabled) return;

  // Note that espota needs no keep-alive here: ArduinoOTA::handle() blocks
  // inside _runUpdate() for the whole transfer, so otaLoop() does not run
  // again until it is over. Refreshing s_lastActivity around this call would
  // reset the timer on every single pass of the main loop and quietly turn the
  // watchdog above into a no-op.
  ArduinoOTA.handle();
}
