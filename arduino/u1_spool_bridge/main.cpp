// ---------------------------------------------------------------------------
// u1-spool-bridge
//
// ESP32-C6 + PN532: read the RFID tag off a filament spool (Bambu Lab, QIDI or
// any OpenSpool NTAG), then push the material into a Snapmaker U1 slot over
// WiFi. Everything is driven from a small web UI served by the board.
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_random.h>

#include "config.h"
#include "fleet_ota.h"
#include "ota.h"
#include "send_gate.h"
#include "settings.h"
#include "spool_data.h"
#include "spoolman.h"
#include "tag_reader.h"
#include "u1_client.h"
#include "web_ui.h"

static uint32_t lastScan = 0;
static uint32_t scanDelay = 400;
static uint32_t lastStatus = 0;
static uint32_t lastPing = 0;
static uint32_t lastStatePoll = 0;
static uint32_t statePollBackoff = 0;
// When a reader is down we retry it on a slow cadence rather than hammering a
// bus that isn't answering. Also lets a reader that was rewired live come back
// without a reboot.
static uint32_t lastReaderRetry[MAX_READERS] = {0};
// Consecutive failed recoveries. Enough of them means the problem is below the
// firmware and only a full reboot is left to try.
static uint16_t consecFail[MAX_READERS] = {0};
// When this reader first failed to come back. The reboot fallback measures from
// here, not from uptime — otherwise re-seating a connector on a box that has
// been up for a week would reboot it immediately.
static uint32_t firstFailAt[MAX_READERS] = {0};
// Tags seen but not decoded, in a row. A spool at the very edge of the field
// gives up its UID readily and then hasn't the signal for the second of MIFARE
// authentication a decode needs — so it appears, fails, drops out, and does it
// all again. Worth naming, because the symptom on its own reads as a fault.
static uint8_t  undecoded[MAX_READERS] = {0};
// True when a band-restricted join failed and we associated on the other band.
bool      g_bandFellBack = false;
// The printer's own view of all four slots. Refreshed slowly in the background,
// and immediately after anything that could have changed it — so the panel still
// shows what is loaded long after the spool has left the reader, which is the
// whole point of it.
U1Slot    g_slots[4];
bool      g_slotsKnown = false;
String    g_slotsErr;
static uint32_t lastSlotFetch = 0;
static bool     slotsDirty    = true;   // fetch once as soon as we are up
bool      g_chanPresent[4] = {false, false, false, false};
bool      g_chanKnown = false;

GateConfig gateCfg() {
  GateConfig c;
  c.mode         = g_settings.triggerMode;
  c.cooldownMs   = (uint32_t)g_settings.cooldownS * 1000UL;
  c.scanValidMs  = (uint32_t)g_settings.scanValidS * 1000UL;
  c.armTimeoutMs = (uint32_t)g_settings.armTimeoutS * 1000UL;
  return c;
}

static void queueSend(uint8_t reader, const SpoolData &d, uint8_t channel,
                      const char *why) {
  g_work.spool   = d;
  g_work.channel = channel;
  g_work.reader  = reader;
  g_work.send    = true;
  webLog(String(READER_COUNT > 1 ? "reader " + String(reader + 1) + ": " : "") +
         "sending to slot " + String(channel + 1) + " — " + why);
}

static void led(uint8_t r, uint8_t g, uint8_t b) {
#if defined(RGB_BUILTIN)
  rgbLedWrite(RGB_BUILTIN, r, g, b);
#elif defined(LED_BUILTIN)
  // Boards like the XIAO have one plain LED, so it can't carry a colour code.
  // Use it for the thing you actually want to see across a room: lit means
  // something needs attention (no reader, no WiFi, a failed send).
  bool trouble = (r > g && r > b);
#ifdef STATUS_LED_ACTIVE_LOW
  digitalWrite(LED_BUILTIN, trouble ? LOW : HIGH);
#else
  digitalWrite(LED_BUILTIN, trouble ? HIGH : LOW);
#endif
#else
  (void)r; (void)g; (void)b;
#endif
}

// Ask the radio for a band before associating. No-op on a 2.4 GHz-only part.
//
// setBandMode() refuses with "You need to start WiFi first" until the radio's
// STA_START event has been processed on the event task. That is asynchronous,
// and markedly slower on a cold power-on than on a soft reset — so a
// power-cycled board would lose the race, the band mode would never be applied,
// the radio would stay on AUTO, and a box set to 5 GHz-only came back on
// 2.4 GHz. Retry until the radio is ready rather than assuming it already is.
static bool applyBandMode(uint8_t band) {
#if HAS_DUAL_BAND
  wifi_band_mode_t bm = WIFI_BAND_MODE_AUTO;
  if (band == BAND_2G) bm = WIFI_BAND_MODE_2G_ONLY;
  else if (band == BAND_5G) bm = WIFI_BAND_MODE_5G_ONLY;

  for (uint8_t i = 0; i < 60; i++) {          // up to ~600 ms
    if (WiFi.setBandMode(bm)) return true;
    delay(10);
  }
  Serial.println("Radio would not take the band mode — it stays on auto.");
  return false;
#else
  (void)band;
  return true;
#endif
}

// setTxPower() is refused unless the radio has actually started, and the start
// is signalled asynchronously — so this retries briefly rather than assuming.
// A silent failure here would be the worst kind: the log would claim the limit
// was applied while the radio carried on at full power.
static bool s_txPowerOverride = false;   // set after a join fails at reduced power

static void applyTxPower() {
  if (!g_settings.wifiTxPower || s_txPowerOverride) return;
  wifi_power_t want = (wifi_power_t)(g_settings.wifiTxPower * 4);
  for (uint8_t i = 0; i < 60; i++) {          // up to ~600 ms, same race
    if (WiFi.setTxPower(want)) {
      Serial.printf("TX power limited to %u dBm\n",
                    (unsigned)g_settings.wifiTxPower);
      return;
    }
    delay(10);
  }
  Serial.println("Radio refused the TX power limit — running at full power.");
}

static bool joinNetwork(uint8_t band, uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  applyBandMode(band);
  // Set the power limit *before* associating, not after. Association is the
  // hungriest phase of the whole boot — a weak-signal join retries hard for
  // seconds — and that is exactly when a shared 3V3 rail sags far enough to
  // upset the PN532 sitting next to it.
  applyTxPower();
  WiFi.begin(g_settings.wifiSsid, g_settings.wifiPass);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

static void startNetwork() {
  WiFi.persistent(false);
  WiFi.setHostname(g_settings.hostname);

  if (g_settings.wifiSsid[0]) {
    Serial.printf("Joining %s", g_settings.wifiSsid);
    bool ok = joinNetwork(g_settings.wifiBand, 15000);

    // Don't let a band preference lock you out: if a band-restricted join
    // fails, try again across both bands before giving up.
    if (!ok && g_settings.wifiBand != BAND_AUTO) {
      Serial.printf("No luck on the requested band — retrying on both");
      WiFi.disconnect(true);
      delay(200);
      ok = joinNetwork(BAND_AUTO, 12000);
      if (ok) g_bandFellBack = true;
    }

    // Nor should a TX power limit. The AP has to hear *us*, and a low setting
    // that looked fine on the bench can fail to associate — the symptom is an
    // AUTH_EXPIRE and a long string of retries. Being unreachable is a worse
    // failure than a browned-out reader, so the limit yields.
    if (!ok && g_settings.wifiTxPower) {
      Serial.println("Join failed at reduced TX power — retrying at full power.");
      s_txPowerOverride = true;
      WiFi.disconnect(true);
      delay(200);
      ok = joinNetwork(BAND_AUTO, 12000);
      if (ok) {
        Serial.printf(
            "Associated only at full power. %u dBm is too low for this AP — "
            "raise it in Settings (13 dBm is a good starting point).\n",
            (unsigned)g_settings.wifiTxPower);
      }
    }
  }

  if (WiFi.status() == WL_CONNECTED && g_bandFellBack) {
    // Worth stating plainly. The pill in the header shows the band you actually
    // got, but nobody reads a pill they are not suspicious of yet.
    Serial.printf(
        "NOTE: %s was requested but only a dual-band join succeeded — this box "
        "is on %s. The AP probably has no %s radio for this SSID.\n",
        g_settings.wifiBand == BAND_5G ? "5 GHz only" : "2.4 GHz only",
        wifiBandName(), g_settings.wifiBand == BAND_5G ? "5 GHz" : "2.4 GHz");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Connected on %s (RSSI %d dBm): http://%s/  (http://%s.local/)\n",
                  wifiBandName(), WiFi.RSSI(), WiFi.localIP().toString().c_str(),
                  g_settings.hostname);
    led(0, 12, 0);
  } else {
    // No credentials, or the network is gone: come up as an access point so
    // the UI is always reachable. The setup AP stays on 2.4 GHz — a 5 GHz-only
    // AP is invisible to plenty of phones, and this is the network you need to
    // reach in order to fix things.
    WiFi.mode(WIFI_AP);
    applyBandMode(BAND_2G);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    // An unprovisioned box sits in AP mode next to the same PN532 on the same
    // rail, so the limit matters here too — arguably more, since nobody is
    // watching this one.
    applyTxPower();
    Serial.printf("AP mode: SSID \"%s\", http://%s/\n", AP_SSID,
                  WiFi.softAPIP().toString().c_str());
    led(12, 6, 0);
  }

  if (MDNS.begin(g_settings.hostname)) {
    MDNS.addService("http", "tcp", 80);
    // Own service type so the boxes can find each other without a hub.
    MDNS.addService("u1spool", "tcp", 80);
    // Explicit String overload: boxName/printerHost are char arrays, which
    // makes the const char* and String overloads ambiguous.
    MDNS.addServiceTxt(String("u1spool"), String("tcp"), String("box"),
                       String(g_settings.boxName));
    MDNS.addServiceTxt(String("u1spool"), String("tcp"), String("slot"),
                       String(g_settings.readerChannel[0] + 1));
    MDNS.addServiceTxt(String("u1spool"), String("tcp"), String("printer"),
                       String(g_settings.printerHost));
  }

  if (WiFi.status() == WL_CONNECTED) {
    // Real timestamps for the Spoolman comment trail. Non-blocking; if it
    // never lands we just omit the date.
    configTzTime(g_settings.timezone, g_settings.ntpServer);

    if (spoolmanConfigured()) {
      String err;
      if (!spoolmanEnsureFields(err)) {
        Serial.printf("Spoolman: could not set up the %s field: %s\n",
                      SPOOLMAN_UID_FIELD, err.c_str());
      }
    }
  }
}

// Ask Spoolman what this tag actually is. Runs for unreadable tags too — a
// blank NTAG registered against a spool is the cheapest setup there is.
static void enrichFromSpoolman(SpoolData &d) {
  if (!spoolmanConfigured() || d.uidLen == 0) return;

  char uidHex[24];
  uidToHex(d.uid, d.uidLen, uidHex, sizeof(uidHex));

  String   err;
  uint32_t id = spoolmanFindByUid(uidHex, err);
  if (!id) {
    if (err.length()) {
      g_spoolmanOk = false;
      webLog("Spoolman lookup failed: " + err, "bad");
    } else {
      g_spoolmanOk = true;
      webLog(String("tag ") + uidHex + " isn't linked to a Spoolman spool yet", "warn");
    }
    return;
  }

  SpoolmanSpool s;
  if (!spoolmanFetch(id, s, err)) {
    g_spoolmanOk = false;
    webLog("Spoolman spool #" + String(id) + " unreadable: " + err, "bad");
    return;
  }

  g_spoolmanOk = true;
  spoolmanApply(s, d);
  normalizeForU1(d);
  webLog("Spoolman #" + String(id) + ": " + String(s.vendor) + " " + s.filamentName +
             (s.remainingWeight >= 0 ? " (" + String((int)s.remainingWeight) + " g left)" : ""),
         "ok");
}

// ---------------------------------------------------------------------------
// Radio-off self test.
//
// The whole "is it WiFi browning out the PN532?" question is answerable in one
// run: keep the reader polling and never turn the radio on. If the reader survives
// with the radio dark and drops out without it, the answer is power, and no amount
// of firmware will fix it. Kept in RTC memory so it survives the soft reset
// used to enter it, and cleared on the way in so a single run can't strand a
// box with its radio off.
// ---------------------------------------------------------------------------
// Reboots already spent trying to revive a reader, kept across the restart so
// the budget is real. Without it, "reboot when the reader won't come back" is
// just an infinite loop with a five-minute period.
#define REBOOT_BUDGET_MAGIC 0x52424754UL
#define MAX_READER_REBOOTS  3
RTC_NOINIT_ATTR static uint32_t s_rebootMagic;
RTC_NOINIT_ATTR static uint32_t s_readerReboots;

#define RADIO_TEST_MAGIC 0x52544553UL  // "RTES"
#define RADIO_TEST_MS    (5UL * 60UL * 1000UL)
RTC_NOINIT_ATTR static uint32_t s_radioTestFlag;
static bool     s_radioTest   = false;
static uint32_t s_radioTestT0 = 0;

void requestRadioTest() {
  s_radioTestFlag = RADIO_TEST_MAGIC;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== u1-spool-bridge " FW_VERSION " ===");

  s_radioTest    = (s_radioTestFlag == RADIO_TEST_MAGIC);
  s_radioTestFlag = 0;  // one run only, whatever happens next

  // RTC memory is undefined on a cold boot, so the magic tells us whether
  // s_readerReboots means anything. A power cycle deliberately hands the board
  // a fresh budget — someone has been at the box.
  if (s_rebootMagic != REBOOT_BUDGET_MAGIC) {
    s_rebootMagic   = REBOOT_BUDGET_MAGIC;
    s_readerReboots = 0;
  }

#if !defined(RGB_BUILTIN) && defined(LED_BUILTIN)
  pinMode(LED_BUILTIN, OUTPUT);
#endif
  led(0, 0, 12);

  if (!settingsLoad()) Serial.println("No saved settings — using defaults.");
  settingsDeriveHostname();
  Serial.printf("Box \"%s\" -> slot %d on %s (http://%s.local/)\n",
                g_settings.boxName, g_settings.readerChannel[0] + 1,
                g_settings.printerHost[0] ? g_settings.printerHost : "(unset)",
                g_settings.hostname);

  uint8_t up = readersBegin();
  for (uint8_t i = 0; i < READER_COUNT; i++) {
    if (g_readers[i].ready()) {
      uint32_t v = g_readers[i].firmwareVersion();
      Serial.printf("Reader %d on %s ready (PN532 fw %d.%d) -> slot %d\n", i + 1,
                    g_readers[i].busName(), (int)((v >> 16) & 0xFF),
                    (int)((v >> 8) & 0xFF), g_settings.readerChannel[i] + 1);
    } else {
      Serial.printf("Reader %d init failed: %s\n", i + 1, g_readers[i].lastError());
    }
  }
  if (!up) led(12, 0, 0);

  if (s_radioTest) {
    WiFi.mode(WIFI_OFF);
    s_radioTestT0 = millis();
    Serial.println(
        "\n*** RADIO-OFF SELF TEST ***\n"
        "WiFi is off. The reader will poll on its own for 5 minutes, then the\n"
        "board reboots back to normal by itself.\n"
        "  - reader stays up for 5 minutes -> the radio was the trigger. Fit\n"
        "    the 100uF + 0.1uF across the PN532's VCC/GND, or turn TX power\n"
        "    down.\n"
        "  - it drops out anyway           -> it is the wiring or the module,\n"
        "    not the power. Shorten the two signal wires and check RSTO.\n");
    return;  // no web server, no OTA, nothing that could key the transmitter
  }

  startNetwork();

  // Bringing the radio up is the roughest thing that happens to the 3V3 rail
  // all boot, and the reader is sitting there through all of it with nobody
  // talking to it. If it took a hit we find out here, while nothing else is
  // going on, rather than several seconds into the poll loop with a scan
  // already in flight.
  for (uint8_t i = 0; i < READER_COUNT; i++) {
    if (!g_readers[i].ready()) continue;
    if (g_readers[i].alive()) continue;
    Serial.printf("Reader %d stopped answering while WiFi came up — resetting\n",
                  i + 1);
    if (g_readers[i].recover()) {
      Serial.printf("Reader %d back after reset\n", i + 1);
    } else {
      Serial.printf("Reader %d reset failed: %s\n", i + 1,
                    g_readers[i].lastError());
    }
  }

  webBegin();
  otaBegin();
  Serial.println("Web UI up on port 80.");
}

void loop() {
  if (s_radioTest) {
    if (millis() - s_radioTestT0 >= RADIO_TEST_MS) {
      Serial.printf(
          "\n*** RADIO-OFF SELF TEST DONE — %u reader resets in 5 minutes ***\n"
          "Rebooting back to normal.\n",
          (unsigned)g_readers[0].recoveries());
      delay(200);
      ESP.restart();
    }
    SpoolData d;
    String    note;
    if (g_readers[0].ready()) {
      ScanResult res = g_readers[0].poll(d, note);
      if (res == SCAN_READER_ERROR) {
        Serial.printf("[%lus] reader stopped answering with the radio OFF"
                      " — resetting\n",
                      (unsigned long)((millis() - s_radioTestT0) / 1000));
        if (!g_readers[0].recover()) {
          Serial.printf("[%lus] reset failed: %s\n",
                        (unsigned long)((millis() - s_radioTestT0) / 1000),
                        g_readers[0].lastError());
          delay(2000);
        }
      } else if (res == SCAN_NEW_TAG) {
        Serial.printf("[%lus] read %s %s\n",
                      (unsigned long)((millis() - s_radioTestT0) / 1000),
                      d.vendor, d.mainType);
      }
    } else if (!g_readers[0].recover()) {
      delay(2000);
    }
    delay(400);
    return;
  }

  webLoop();
  otaLoop();

  // While an image is being written, stay out of the way: no reader traffic, no
  // HTTP to the printer, nothing that could stall the flash write.
  if (otaBusy()) {
    delay(1);
    return;
  }

  if (g_otaRebootPending) {
    webLog("firmware updated — rebooting", "ok");
    delay(600);          // let the HTTP response and websocket flush
    ESP.restart();
  }

  uint32_t now = millis();

  // ---- 1. poll the readers ------------------------------------------------
  // Sequentially, never in parallel: two PN532s in one drybox are close enough
  // to jam each other, and only one has its RF field up at a time this way.
  if (now - lastScan >= scanDelay) {
    lastScan = now;
    // Two boards sharing one drybox poll on their own clocks. At a fixed
    // interval their RF fields can drift into phase and stay there, giving one
    // of them a systematic blind spot. A bit of jitter breaks that up.
    scanDelay = g_settings.scanIntervalMs +
                (esp_random() % (g_settings.scanIntervalMs / 4 + 1));
    for (uint8_t r = 0; r < READER_COUNT; r++) {
      // A reader that's down gets another go periodically, so fixing the
      // wiring — or a module that refused to come back — doesn't need a
      // reboot or a trip to the drybox.
      if (!g_readers[r].ready()) {
        // Last resort. If resetting the reader hasn't worked several times over,
        // the fault is below anything this firmware can reach — a rail that
        // won't hold, or a PN532 stuck in its own bad state. A full chip reset
        // tears down the serial port and re-inits the module from cold, which
        // is the one thing left to try. The uptime guard means a
        // board that can never read still stays up long enough to be reflashed
        // over the air rather than boot-looping out of reach.
        // Three conditions, all of them load-bearing:
        //   everWorked()  — a board flashed before its reader was wired must
        //                   not reboot forever; it has nothing to recover.
        //   five minutes down — measured from the first failure, not uptime,
        //                   so re-seating a connector doesn't trip it.
        //   budget        — a reboot that doesn't help must not be repeated
        //                   indefinitely. Three tries, then stay up and say so,
        //                   because a reachable broken box can still be
        //                   reflashed and still serves its web UI.
        if (g_readers[r].everWorked() && consecFail[r] >= 5 &&
            firstFailAt[r] && now - firstFailAt[r] >= 300000) {
          if (s_readerReboots < MAX_READER_REBOOTS) {
            s_readerReboots++;
            Serial.printf(
                "\n*** reader %d unrecoverable after %u resets — reboot %u of %u ***\n",
                r + 1, (unsigned)consecFail[r], (unsigned)s_readerReboots,
                (unsigned)MAX_READER_REBOOTS);
            webLog("reader " + String(r + 1) +
                       " won't come back — rebooting the board",
                   "bad");
            delay(400);
            ESP.restart();
          } else if (consecFail[r] == 5) {
            // Say it once, then stop shouting.
            consecFail[r]++;
            Serial.printf(
                "\n*** reader %d still dead after %u reboots — giving up on "
                "rebooting. Box stays up for OTA. ***\n",
                r + 1, (unsigned)MAX_READER_REBOOTS);
            webLog("reader " + String(r + 1) +
                       " is not coming back — check the wiring at the box",
                   "bad");
          }
        }
        if (now - lastReaderRetry[r] >= 30000) {
          lastReaderRetry[r] = now;
          if (g_readers[r].recover()) {
            consecFail[r]  = 0;
            firstFailAt[r] = 0;
            s_readerReboots = 0;   // it came back; the budget refills
            webLog("reader " + String(r + 1) + " is back (PN532 fw " +
                       String((g_readers[r].firmwareVersion() >> 16) & 0xFF) + "." +
                       String((g_readers[r].firmwareVersion() >> 8) & 0xFF) + ")",
                   "ok");
            webBroadcastStatus();
          } else {
            if (!firstFailAt[r]) firstFailAt[r] = now ? now : 1;
            consecFail[r]++;
          }
        }
        continue;
      }
      Lane      &lane = g_lanes[r];
      SpoolData  d;
      String     note;
      ScanResult res = g_readers[r].poll(d, note);

      if (res == SCAN_NEW_TAG || res == SCAN_UNREADABLE) {
        enrichFromSpoolman(d);
        if (d.valid) note = "";  // Spoolman rescued an otherwise unreadable tag

        lane.spool   = d;
        g_activeLane = r;
        webBroadcastTag(r, d, note);
        led(0, 0, 20);

        if (!d.valid) {
          // Previously this said nothing at all, so the console showed a bare
          // "spool removed" every few seconds with no hint of what had come and
          // gone. Say what was seen.
          char uidHex[24];
          uidToHex(d.uid, d.uidLen, uidHex, sizeof(uidHex));
          webLog(String("tag ") + uidHex + " detected but not decoded" +
                     (note.length() ? " — " + note : ""),
                 "warn");
          if (++undecoded[r] == 3) {
            webLog("three in a row — the tag is probably at the edge of the "
                   "field. Move the spool closer to the antenna, or check it "
                   "isn't a tag type this build can't read.",
                   "warn");
          }
        } else {
          undecoded[r] = 0;
        }

        if (d.valid) {
          GateDecision g = gateOnScan(gateCfg(), lane.gate, d.uid, d.uidLen,
                                      g_settings.readerChannel[r], now);
          if (!lane.firstTagSeen && !g_settings.sendOnBoot) {
            // The spool was already in the box when we powered up; the printer
            // presumably already knows about it.
            webLog("spool was already in the box at boot — not re-sending", "warn");
          } else if (g.send) {
            queueSend(r, d, g.channel, g.reason);
          } else {
            webLog(String(d.vendor) + " " + d.mainType + " — " + g.reason);
          }
        }
        lane.firstTagSeen = true;
        webBroadcastStatus();
      } else if (res == SCAN_REMOVED) {
        lane.spool.clear();
        g_activeLane = r;
        webLog(String(g_settings.boxName) +
               (READER_COUNT > 1 ? " reader " + String(r + 1) : "") +
               ": spool removed");
        webBroadcastTag(r, lane.spool, "");
        webBroadcastStatus();
      } else if (res == SCAN_READER_ERROR) {
        // The module can stop answering on its own — a brownout during a WiFi
        // burst, a truncated frame leaving it out of step, or the PN532 simply
        // latching up. A UART has nothing to unwedge, so this is not the I2C
        // recovery it used to be: it closes the port, pulses RSTO and re-inits
        // from cold, which is the only lever there is. Do that rather than
        // spew errors forever.
        //
        // The banners are deliberately loud and go straight to Serial: the
        // driver's own error spam drowns out anything subtle, and this is the
        // one line that tells you whether recovery is even running.
        {
          uint32_t prev = g_readers[r].lastRecoveryAt();
          Serial.printf("\n*** READER RECOVERY: reader %d %s", r + 1, note.c_str());
          // The interval is the diagnosis. Every few seconds is a firmware or
          // signal problem; every few hours is a rail that needs the capacitors.
          if (prev) Serial.printf(" (%lu s since the last one)",
                                  (unsigned long)((now - prev) / 1000));
          Serial.printf(" ***\n");
        }
        webLog("reader " + String(r + 1) + " " + note + " — resetting it",
               "bad");
        if (g_readers[r].recover()) {
          consecFail[r]  = 0;
          firstFailAt[r] = 0;
          s_readerReboots = 0;
          Serial.printf("*** READER RECOVERY: reader %d OK (reset #%u) ***\n\n",
                        r + 1, (unsigned)g_readers[r].recoveries());
          webLog("reader " + String(r + 1) + " recovered (reset #" +
                     String(g_readers[r].recoveries()) + ")",
                 "ok");
        } else {
          if (!firstFailAt[r]) firstFailAt[r] = now ? now : 1;
          consecFail[r]++;
          Serial.printf("*** READER RECOVERY: reader %d FAILED (%u in a row): %s ***\n\n",
                        r + 1, (unsigned)consecFail[r], g_readers[r].lastError());
          webLog("reader " + String(r + 1) + " reset failed: " +
                     String(g_readers[r].lastError()) + " — retrying in 30 s",
                 "bad");
          lastReaderRetry[r] = now;
        }
        webBroadcastStatus();
      } else if (res == SCAN_NO_TAG && note.length()) {
        webLog(note);
      }
    }
  }

  for (uint8_t r = 0; r < READER_COUNT; r++) gateExpire(gateCfg(), g_lanes[r].gate, now);

  // ---- 1b. watch the printer's slots ---------------------------------------
  // A slot going from empty to occupied is the load actually happening. That,
  // not the mere presence of a tag, is what releases the pending scan.
  if (g_settings.triggerMode == TRIG_ON_LOAD && g_settings.printerHost[0] &&
      now - lastStatePoll >= (statePollBackoff ? statePollBackoff
                                               : g_settings.statePollMs)) {
    lastStatePoll = now;
    bool   fresh[4];
    String err;
    if (u1FetchChannels(fresh, err)) {
      statePollBackoff = 0;
      if (g_chanKnown) {
        for (uint8_t i = 0; i < 4; i++) {
          if (fresh[i] && !g_chanPresent[i]) {
            uint8_t lane = 0;
            for (uint8_t r = 0; r < READER_COUNT; r++) {
              if (g_settings.readerChannel[r] == i) { lane = r; break; }
            }
            GateDecision g =
                gateOnChannelLoaded(gateCfg(), g_lanes[lane].gate, i, now);
            if (g.send) {
              queueSend(lane, g_lanes[lane].spool, i, g.reason);
            } else {
              webLog("slot " + String(i + 1) + " loaded — " + g.reason, "warn");
            }
          }
        }
      }
      // A slot changing occupancy means its filament info has changed too.
      if (memcmp(g_chanPresent, fresh, sizeof(fresh)) != 0) slotsDirty = true;
      memcpy(g_chanPresent, fresh, sizeof(fresh));
      g_chanKnown = true;
    } else {
      // Don't hammer an unreachable printer from the main loop.
      if (!statePollBackoff) webLog("slot watch paused: " + err, "warn");
      statePollBackoff = 15000;
      g_chanKnown = false;
    }
  }

  // ---- 2. blocking work, kept off the AsyncTCP task ------------------------
  if (g_work.send) {
    g_work.send = false;
    SendResult r = u1Send(g_work.spool, g_work.channel);
    g_printerOk  = r.ok;
    if (r.ok) {
      webLog("slot " + String(g_work.channel + 1) + " set to " +
                 String(g_work.spool.vendor) + " " + g_work.spool.mainType + " " +
                 g_work.spool.subType,
             "ok");
      led(0, 20, 0);
      gateNoteSent(g_lanes[g_work.reader].gate, g_work.spool.uid,
                   g_work.spool.uidLen, g_work.channel, millis());

      // Record where the spool went, now that we know the send stuck.
      if (g_work.spool.spoolmanId && spoolmanConfigured() &&
          (g_settings.spoolmanSetLocation || g_settings.spoolmanNoteLoads)) {
        String smErr;
        if (spoolmanNoteLoad(g_work.spool.spoolmanId, g_work.channel, smErr)) {
          webLog("Spoolman #" + String(g_work.spool.spoolmanId) + " marked as loaded");
        } else {
          webLog("Spoolman write-back failed: " + smErr, "warn");
        }
      }
    } else {
      webLog("send failed: " + r.error, "bad");
      led(20, 0, 0);
    }
    webBroadcastStatus();
  }

  if (g_work.ping) {
    g_work.ping = false;
    String host, err;
    g_printerOk = u1Ping(host, err);
    webLog(g_printerOk ? "printer reachable (" + host + ")"
                       : "printer unreachable: " + err,
           g_printerOk ? "ok" : "bad");
    webBroadcastStatus();
  }

  // A firmware push to the other boxes. Blocking and slow — tens of seconds
  // per peer — so it runs here on the main loop rather than on the web server
  // task, and this box reboots into the new image once it is done.
  if (fleetPlanPending() && !otaBusy()) {
    Serial.println("Fleet update: sending this image on to the other boxes...");
    fleetPushRun();
    Serial.println("Fleet update done; rebooting into the new firmware.");
    delay(400);
    g_otaRebootPending = true;
  }

  // The page asks for this on load and then on a timer, so it is no longer a
  // deliberate button press. Each scan blocks this loop on an mDNS query plus a
  // GET to every peer, during which no tag is read — so refuse to run one more
  // often than every 10 s however often it is asked for.
  static uint32_t lastFleetScan = 0;
  if (g_work.fleet && lastFleetScan && millis() - lastFleetScan < 10000) {
    g_work.fleet = false;
  }
  // Read the tag on the reader out sector by sector. Slow — hundreds of
  // authenticate attempts — so it runs here, and the reader's normal polling is
  // suspended for the duration by the same otaBusy() gate the updates use.
  if (g_work.dump) {
    g_work.dump = false;
    static CardDump dump;                 // ~1 kB; not going on the stack
    webLog("dump: reading every sector, this takes a moment...", "warn");

    if (!g_readers[0].dumpCard(dump, webDumpProgress)) {
      webDumpResult("No tag on the reader, or the reader is not answering.");
    } else {
      String t;
      t.reserve(2600);
      char line[96];

      t += "u1-spool-bridge card dump  fw " FW_VERSION "\n";
      t += "UID ";
      for (uint8_t i = 0; i < dump.uidLen; i++) {
        snprintf(line, sizeof(line), "%02X", dump.uid[i]);
        t += line;
      }
      snprintf(line, sizeof(line), "  (%u bytes)\n", (unsigned)dump.uidLen);
      t += line;

      if (!dump.classic) {
        t += "\nNot a MIFARE Classic 1K (a 7-byte UID is NTAG21x, which has no\n"
             "sectors and is read by the OpenSpool path already).\n";
      } else {
        snprintf(line, sizeof(line), "%u of 16 sectors opened\n\n",
                 (unsigned)dump.sectorsRead);
        t += line;

        for (uint8_t sc = 0; sc < 16; sc++) {
          if (!dump.ok[sc]) {
            snprintf(line, sizeof(line), "sector %2u  --  no key worked\n", (unsigned)sc);
            t += line;
            continue;
          }
          snprintf(line, sizeof(line), "sector %2u  key %s (%c)\n", (unsigned)sc,
                   dump.keyUsed[sc], dump.keyType[sc]);
          t += line;
          for (uint8_t b = 0; b < 3; b++) {
            snprintf(line, sizeof(line), "  %02u ", (unsigned)(sc * 4 + b));
            t += line;
            for (uint8_t i = 0; i < 16; i++) {
              snprintf(line, sizeof(line), "%02X", dump.data[sc][b][i]);
              t += line;
              if (i % 4 == 3) t += ' ';
            }
            t += " |";
            for (uint8_t i = 0; i < 16; i++) {
              const uint8_t c = dump.data[sc][b][i];
              t += (char)((c >= 32 && c < 127) ? c : '.');
            }
            t += "|\n";
          }
        }
      }
      webDumpResult(t);
      webLog("dump: " + String(dump.sectorsRead) + "/16 sectors read", "ok");
    }
  }

  if (g_work.groupApply) {
    g_work.groupApply = false;
    fleetSetRun();
  }

  // Our own loaded spools, re-filed under whatever the location now resolves
  // to. Without this Spoolman keeps showing the location from before the
  // rename until the spool happens to be reloaded.
  if (g_work.smRefile) {
    g_work.smRefile = false;
    if (g_settings.spoolmanEnabled && g_settings.spoolmanSetLocation) {
      for (uint8_t i = 0; i < READER_COUNT; i++) {
        const SpoolData &sp = g_lanes[i].spool;
        if (!sp.valid || !sp.spoolmanId) continue;
        String err;
        if (spoolmanRefileLocation(sp.spoolmanId, g_settings.readerChannel[i], err))
          webLog("Spoolman: slot " + String(g_settings.readerChannel[i] + 1) +
                     " re-filed", "ok");
        else
          webLog("Spoolman re-file failed: " + err, "warn");
      }
    }
    slotsDirty = true;
  }

  if (g_work.fleet) {
    g_work.fleet  = false;
    lastFleetScan = millis();
    String peers = "[";
    String err;
    int    n = MDNS.queryService("u1spool", "tcp");
    if (n <= 0) {
      err = "no other boxes answered on mDNS";
    } else {
      for (int i = 0; i < n; i++) {
        IPAddress ip   = MDNS.address(i);
        uint16_t  port = MDNS.port(i);
        if (String(MDNS.hostname(i)) == String(g_settings.hostname)) continue;  // us

        WiFiClient c;
        HTTPClient h;
        h.setTimeout(2500);
        String url = "http://" + ip.toString() + ":" + String(port) + "/api/brief";
        String body;
        if (h.begin(c, url) && h.GET() == 200) {
          body = h.getString();
        }
        h.end();
        if (!body.length()) continue;

        // Stamp in the address we actually reached this box on. "Update all
        // boxes" needs somewhere to POST, and it cannot get that from the peer
        // itself: a box running 1.11.2 or older does not report its own IP, and
        // those are precisely the boxes an update is for. Asking the peer would
        // have meant the fleet updater only worked on boxes that had already
        // been updated. We have the address right here from the mDNS answer, so
        // it works whatever firmware the peer is running.
        //
        // Prepended under its own key rather than "ip" so it cannot collide
        // with a newer peer's self-report — JSON duplicate keys would silently
        // resolve to whichever came last.
        // Guarded against an empty object: "{}" would splice into
        // {"addr":"...",} — a trailing comma, invalid JSON, and it would take
        // the whole fleet list down with it, not just this peer.
        String addr = ip.toString() + ":" + String(port);
        body.trim();
        if (body.length() > 2 && body.startsWith("{")) {
          body = "{\"addr\":\"" + addr + "\"," + body.substring(1);
        } else {
          continue;   // not something we can address or describe
        }
        if (peers.length() > 1) peers += ",";
        peers += body;
      }
    }
    peers += "]";
    webBroadcastFleet(peers, err);
  }

  if (g_work.arm) {
    g_work.arm = false;
    Lane        &lane = g_lanes[g_activeLane];
    GateDecision g = gateArm(gateCfg(), lane.gate, g_work.armChannel, millis());
    if (g.send) {
      queueSend(g_activeLane, lane.spool, g.channel, g.reason);
    } else {
      webLog("slot " + String(g_work.armChannel + 1) + " " + g.reason, "warn");
    }
    webBroadcastStatus();
  }

  if (g_work.smPing) {
    g_work.smPing = false;
    String ver, err;
    g_spoolmanOk = spoolmanPing(ver, err);
    webLog(g_spoolmanOk ? "Spoolman reachable (v" + ver + ")"
                        : "Spoolman unreachable: " + err,
           g_spoolmanOk ? "ok" : "bad");
    webBroadcastStatus();
  }

  if (g_work.smList) {
    g_work.smList = false;
    String json, err;
    if (!spoolmanListForPicker(json, err)) json = "";
    webBroadcastSpoolList(json, err);
  }

  if (g_work.smLink) {
    g_work.smLink = false;
    char uidHex[24];
    uidToHex(activeSpool().uid, activeSpool().uidLen, uidHex, sizeof(uidHex));
    String err;
    if (spoolmanLinkUid(g_work.smLinkId, uidHex, err)) {
      webLog(String("tag ") + uidHex + " linked to Spoolman #" +
                 String(g_work.smLinkId),
             "ok");
      // Re-resolve straight away so the card fills in with the real spool.
      SpoolData d = activeSpool();
      enrichFromSpoolman(d);
      activeSpool() = d;
      webBroadcastTag(g_activeLane, d, "");
    } else {
      webLog("link failed: " + err, "bad");
    }
  }

  if (g_work.reboot) {
    webLog("rebooting...", "warn");
    delay(400);
    ESP.restart();
  }

  // ---- 2b. what the printer says is loaded ---------------------------------
  // Deliberately slow: this is the fuller query, and the answer only changes
  // when a slot is loaded, unloaded or written. The dirty flag covers all three,
  // so the timer is just a backstop for changes made at the machine itself.
  if (!otaBusy() && g_settings.printerHost[0] &&
      (slotsDirty || g_work.slots || now - lastSlotFetch >= 15000)) {
    g_work.slots  = false;
    lastSlotFetch = now;
    slotsDirty    = false;
    String err;
    if (u1FetchSlots(g_slots, err)) {
      g_slotsKnown = true;
      g_slotsErr   = err;              // may carry the stock-firmware note
    } else {
      g_slotsKnown = false;
      g_slotsErr   = err;
    }
    webBroadcastStatus();
  }

  // ---- 3. housekeeping ----------------------------------------------------
  if (now - lastStatus >= 2000) {
    lastStatus = now;
    webBroadcastStatus();
  }

  // Quiet background health check, once a minute.
  if (now - lastPing >= 60000) {
    lastPing = now;
    if (g_settings.printerHost[0]) g_work.ping = true;
    if (spoolmanConfigured())      g_work.smPing = true;
  }

  delay(5);
}
