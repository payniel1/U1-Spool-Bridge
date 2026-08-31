#include "tag_reader.h"

#include <HardwareSerial.h>

// The PN532's HSU port. Serial1 on every part we target; Serial (USB CDC) is
// the console and Serial0 is the boot-log UART, so neither is available.
#define READER_UART Serial1

#include "decoders.h"
#include "settings.h"

TagReader g_readers[MAX_READERS];

const char *TagReader::busName() const { return "UART"; }

bool TagReader::begin(uint8_t index, const ReaderPins &pins) {
  _index = index;
  _pins  = pins;
  _quietPolls = 0;
  _probes     = 0;
  _probeFails = 0;

  // Adafruit_PN532 has no destructor, and every constructor heap-allocates its
  // bus device — for the HSU constructor that is nothing on the heap today,
  // but the class still has no destructor, so deleting it would leak whatever
  // a future version does allocate. recover() can run every few seconds on a
  // flapping module, which turns a small leak into hours-to-failure. The
  // object is built once and reused; only the port is closed and reopened.
  const bool fresh = (_nfc == nullptr);

  // Pin the UART to our pads first. The library's own begin() calls
  // Serial1.begin(115200) with no pin arguments, and arduino-esp32 keeps
  // whatever pins were set previously rather than reverting to the variant
  // defaults — so this mapping survives.
  READER_UART.setPins(pins.rx, pins.tx);
  READER_UART.begin(115200, SERIAL_8N1, pins.rx, pins.tx);
  if (fresh) _nfc = new Adafruit_PN532(pins.rst, &READER_UART);

  if (!_nfc) { _ready = false; _lastError = "could not create the reader"; return false; }
  _nfc->begin();

  _fwVersion = _nfc->getFirmwareVersion();
  if (!_fwVersion) {
    _lastError = "PN532 not answering on HSU — check the wiring and that BOTH "
                 "DIP switches are OFF";
    _ready = false;
    return false;
  }

  _nfc->SAMConfig();
  // One retry only: readPassiveTargetID must return quickly so the web server
  // keeps serving while we poll.
  _nfc->setPassiveActivationRetries(0x01);
  _ready = true;
  _everWorked = true;
  _lastError = "";
  return true;
}

bool TagReader::alive() {
  if (!_ready || !_nfc) return false;
  // Same stale-byte trap as poll(): getFirmwareVersion() checks the first six
  // bytes of the reply against a fixed preamble, so leftovers in the RX buffer
  // make a healthy reader look dead. This is the check that runs right after
  // association, when the port has sat unread for several seconds — precisely
  // when leftovers are most likely.
  while (READER_UART.available()) READER_UART.read();
  // And ask twice before believing a "no". One miss is not evidence.
  if (_nfc->getFirmwareVersion()) return true;
  delay(20);
  while (READER_UART.available()) READER_UART.read();
  return _nfc->getFirmwareVersion() != 0;
}

bool TagReader::recover() {
  const bool hadWorked = _everWorked;
  _ready = false;
  _lastUidLen = _candUidLen = 0;
  _lostSince = 0;

  // Nothing can be wedged here — a UART has no shared bus and no driver state
  // that survives a bad frame. Closing the port is all that's needed, and
  // end() also drops any half-received frame still sitting in the FIFO.
  READER_UART.end();

  // We wired RSTO for exactly this moment.
  if (_pins.rst >= 0) {
    pinMode(_pins.rst, OUTPUT);
    digitalWrite(_pins.rst, LOW);
    delay(20);
    digitalWrite(_pins.rst, HIGH);
    delay(20);
  }

  // Deliberately NOT deleting _nfc — see the note in begin().
  const bool ok = begin(_index, _pins);
  // Count resets that actually put a working reader back, so the number the UI
  // shows means "this box's wiring is marginal" rather than "this box has
  // nothing plugged into it".
  if (ok && hadWorked) {
    _recoveries++;
    _lastRecoveryAt = millis();
  }
  return ok;
}

uint8_t readersBegin() {
  // One reader, HSU, RSTO wired. RSTO is not optional: the library pulses it on
  // every begin() to put the module in a known state, and recover() drives it
  // by hand when the module has stopped answering.
  ReaderPins p0;
  p0.rx  = PIN_PN532_RX;
  p0.tx  = PIN_PN532_TX;
  p0.rst = PIN_PN532_RST;
  return g_readers[0].begin(0, p0) ? 1 : 0;
}

bool TagReader::reselect(uint8_t *uid, uint8_t *uidLen) {
  uint8_t len = 0;
  bool ok = _nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &len, 60);
  if (ok) *uidLen = len;
  return ok;
}

// Read the NTAG user area starting at page 4. Uses the raw READ command
// (0x30) so we get 4 pages per exchange instead of 1.
bool TagReader::readNtagBytes(uint8_t *buf, size_t maxLen, size_t *outLen) {
  size_t got = 0;
  for (uint8_t page = 4; got + 16 <= maxLen && page < 130; page += 4) {
    uint8_t cmd[2] = {0x30, page};
    uint8_t rsp[20];
    uint8_t rspLen = sizeof(rsp);
    if (!_nfc->inDataExchange(cmd, 2, rsp, &rspLen) || rspLen < 16) {
      break;  // ran off the end of user memory, or a read error
    }
    memcpy(buf + got, rsp, 16);
    got += 16;

    // Stop early once we have the whole NDEF TLV — no point reading 500 bytes
    // of 0x00 off an NTAG216.
    if (got >= 4 && buf[0] == 0x03) {
      size_t need = (buf[1] == 0xFF) ? (((size_t)buf[2] << 8) | buf[3]) + 4
                                     : (size_t)buf[1] + 2;
      if (got >= need) break;
    }
  }
  *outLen = got;
  return got > 0;
}

bool TagReader::tryOpenSpool(uint8_t *uid, uint8_t uidLen, SpoolData &out) {
  uint8_t buf[352];
  size_t  len = 0;
  if (!readNtagBytes(buf, sizeof(buf), &len)) return false;
  if (!openspool_decode(buf, len, out)) return false;

  memcpy(out.uid, uid, uidLen);
  out.uidLen = uidLen;
  snprintf(out.cardType, sizeof(out.cardType), "NTAG21x");
  return true;
}

bool TagReader::tryBambu(uint8_t *uid, uint8_t uidLen, SpoolData &out) {
  uint8_t keys[16][6];
  bambu_derive_keys(uid, uidLen, keys);

  static uint8_t blocks[64][16];
  static bool    present[64];
  memset(blocks, 0, sizeof(blocks));
  memset(present, 0, sizeof(present));

  int8_t currentSector = -1;
  bool   anyRead = false;

  for (const uint8_t *b = BAMBU_BLOCKS_OF_INTEREST; *b != 0xFF; b++) {
    uint8_t blk    = *b;
    uint8_t sector = blk / 4;

    if (sector != (uint8_t)currentSector) {
      // Every auth failure kills the session, so re-select before each sector.
      if (!reselect(uid, &uidLen)) return anyRead;
      if (!_nfc->mifareclassic_AuthenticateBlock(uid, uidLen, sector * 4, 0,
                                                 keys[sector])) {
        if (sector == 0) return false;  // definitely not a Bambu tag
        currentSector = -1;
        continue;
      }
      currentSector = (int8_t)sector;
    }

    if (_nfc->mifareclassic_ReadDataBlock(blk, blocks[blk])) {
      present[blk] = true;
      anyRead = true;
    }
  }

  if (!anyRead) return false;
  if (!bambu_decode(blocks, present, out)) return false;

  memcpy(out.uid, uid, uidLen);
  out.uidLen = uidLen;
  snprintf(out.cardType, sizeof(out.cardType), "MIFARE_1K");
  return true;
}

static bool hexToKey(const char *hex, uint8_t key[6]) {
  if (!hex || strlen(hex) != 12) return false;
  for (int i = 0; i < 6; i++) {
    unsigned v;
    char     pair[3] = {hex[i * 2], hex[i * 2 + 1], 0};
    if (sscanf(pair, "%02x", &v) != 1) return false;
    key[i] = (uint8_t)v;
  }
  return true;
}

bool TagReader::tryQidi(uint8_t *uid, uint8_t uidLen, SpoolData &out) {
  // QIDI keeps its record in sector 1, block 0 (absolute block 4).
  for (int k = 0; k < MAX_EXTRA_KEYS; k++) {
    uint8_t key[6];
    if (!hexToKey(g_settings.extraKeys[k], key)) continue;
    if (!reselect(uid, &uidLen)) return false;
    if (!_nfc->mifareclassic_AuthenticateBlock(uid, uidLen, 4, 0, key)) continue;

    uint8_t data[16];
    if (!_nfc->mifareclassic_ReadDataBlock(4, data)) continue;
    if (!qidi_decode(data, out)) continue;

    memcpy(out.uid, uid, uidLen);
    out.uidLen = uidLen;
    snprintf(out.cardType, sizeof(out.cardType), "MIFARE_1K");
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Card dump — the instrument for a tag format we cannot decode yet.
//
// A MIFARE Classic sector will not give up a byte without the right 48-bit key,
// so "can we read this tag" is really "do we have its keys". This walks every
// sector with every key we know and records exactly what authenticated, so an
// unknown tag stops being a guess and becomes bytes on a screen.
//
// The order matters. Bambu derives one key per sector from the UID and this
// firmware already computes those, so they go first — if another vendor used a
// similar scheme it shows up immediately. Then whatever the user has pasted
// into Settings, then the public defaults.
// ---------------------------------------------------------------------------
static const char *kCommonKeys[] = {
    "FFFFFFFFFFFF",  // factory default
    "A0A1A2A3A4A5",  // MAD key A
    "D3F7D3F7D3F7",  // NDEF public key
    "000000000000",
    "B0B1B2B3B4B5", "4D3A99C351DD", "1A982C7E459A", "AABBCCDDEEFF",
    "714C5C886E97", "587EE5F9350F", "A0478CC39091", "533CB6C723F6",
    "8FD0A4F256E9",
};

bool TagReader::dumpCard(CardDump &out, DumpProgress onSector) {
  if (!_ready || !_nfc) return false;

  out = CardDump();

  uint8_t uid[10] = {0}, uidLen = 0;
  while (READER_UART.available()) READER_UART.read();
  if (!_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 200)) return false;
  memcpy(out.uid, uid, uidLen);
  out.uidLen = uidLen;

  // A 4-byte UID is the Classic 1K shape; NTAG21x answers with 7 and has no
  // sectors to walk.
  out.classic = (uidLen == 4);
  if (!out.classic) return true;

  uint8_t derived[16][6];
  bambu_derive_keys(uid, uidLen, derived);

  for (uint8_t sector = 0; sector < 16; sector++) {
    // Build this sector's candidate list: derived key for THIS sector first.
    uint8_t cand[4 + MAX_EXTRA_KEYS + 16][6];
    char    candHex[4 + MAX_EXTRA_KEYS + 16][13];
    uint8_t n = 0;

    memcpy(cand[n], derived[sector], 6);
    snprintf(candHex[n], 13, "%02X%02X%02X%02X%02X%02X", derived[sector][0],
             derived[sector][1], derived[sector][2], derived[sector][3],
             derived[sector][4], derived[sector][5]);
    n++;

    for (int k = 0; k < MAX_EXTRA_KEYS && n < (uint8_t)(sizeof(cand) / 6); k++) {
      uint8_t key[6];
      if (!hexToKey(g_settings.extraKeys[k], key)) continue;
      memcpy(cand[n], key, 6);
      snprintf(candHex[n], 13, "%s", g_settings.extraKeys[k]);
      n++;
    }
    for (unsigned k = 0; k < sizeof(kCommonKeys) / sizeof(kCommonKeys[0]) &&
                        n < (uint8_t)(sizeof(cand) / 6); k++) {
      uint8_t key[6];
      if (!hexToKey(kCommonKeys[k], key)) continue;
      memcpy(cand[n], key, 6);
      snprintf(candHex[n], 13, "%s", kCommonKeys[k]);
      n++;
    }

    const uint8_t first = sector * 4;
    for (uint8_t i = 0; i < n && !out.ok[sector]; i++) {
      for (uint8_t type = 0; type < 2 && !out.ok[sector]; type++) {
        // A failed authenticate drops the card, so it has to be reselected
        // before the next attempt or every later try fails for the wrong reason.
        if (!reselect(uid, &uidLen)) { delay(2); continue; }
        if (!_nfc->mifareclassic_AuthenticateBlock(uid, uidLen, first, type, cand[i]))
          continue;

        bool anyBlock = false;
        for (uint8_t b = 0; b < 3; b++) {
          if (_nfc->mifareclassic_ReadDataBlock(first + b, out.data[sector][b]))
            anyBlock = true;
        }
        if (!anyBlock) continue;

        out.ok[sector]      = true;
        out.keyType[sector] = type ? 'B' : 'A';
        snprintf(out.keyUsed[sector], 13, "%s", candHex[i]);
        out.sectorsRead++;
      }
      delay(0);   // keep the watchdog and the web server happy
    }
    if (onSector) onSector(sector + 1, 16);
  }
  return true;
}

ScanResult TagReader::poll(SpoolData &out, String &note) {
  if (!_ready) return SCAN_NO_TAG;

  // Start every cycle on a clean stream.
  //
  // HSU has no framing to resynchronise on: Adafruit_PN532::readdata() for a
  // serial device is a bare readBytes(), so whatever bytes happen to be waiting
  // are taken as the reply. And readPassiveTargetID() returns the moment its ACK
  // wait times out, without draining what the module sends afterwards. In a
  // polling loop with no tag on the antenna that path runs every cycle, so late
  // replies accumulate and shift every subsequent read by however many bytes are
  // left over — turning good reads into garbage and a healthy reader into one
  // that appears to have stopped answering.
  //
  // Anything still in the buffer at this point belongs to a transaction we have
  // already given up on, so dropping it is exactly right.
  while (READER_UART.available()) READER_UART.read();

  uint8_t uid[10] = {0};
  uint8_t uidLen  = 0;
  if (!_nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 50)) {
    _candUidLen = 0;

    // "No tag" and "the module has stopped answering" look identical from here,
    // so every so
    // often during a quiet spell, ask the reader something only a live reader
    // can answer. Skipped entirely while a tag is on the antenna.
    if (++_quietPolls >= 5) {
      _quietPolls = 0;

      // Drain anything stale before asking. A readPassiveTargetID that timed out
      // can leave a partial frame in the RX buffer, and the next reply then
      // arrives misaligned — getFirmwareVersion() checks the first six bytes
      // against the expected preamble and returns 0 when they don't match, which
      // is indistinguishable from a dead reader. Left alone, that turns a healthy
      // module into an endless reset loop.
      while (READER_UART.available()) READER_UART.read();

      if (!_nfc->getFirmwareVersion()) {
        // One failure is not evidence. Ask again next time round before
        // condemning a reader that is probably fine.
        if (++_probeFails < 3) return SCAN_NO_TAG;
        _probeFails = 0;
        note = "stopped answering";
        return SCAN_READER_ERROR;
      }
      _probeFails = 0;
      // getFirmwareVersion() is answered even by a module that has just
      // power-cycled, so "it replied" is NOT the same as "it is configured".
      // A brown-out deep enough to reset the PN532 drops it back to its
      // power-on state, where it still identifies itself but has lost the
      // SAMConfig that makes it hunt for tags — a reader that looks healthy
      // and silently never sees a spool again. Re-arming periodically costs
      // one command exchange per ~20 s of idle and closes that hole. Only
      // ever runs on an empty antenna.
      if (++_probes >= 10) {
        _probes = 0;
        _nfc->SAMConfig();
        _nfc->setPassiveActivationRetries(0x01);
      }
    }

    if (_lastUidLen) {
      // Absence debounce. A spool sitting in a drybox is at the edge of the
      // field and will drop out for a poll or two; treating that as a removal
      // would make it look like a fresh insertion a moment later, and the
      // printer would get rewritten for no reason.
      uint32_t nowMs = millis();
      if (!_lostSince) _lostSince = nowMs ? nowMs : 1;
      if (nowMs - _lostSince < g_settings.absenceMs) return SCAN_SAME_TAG;

      _lastUidLen = 0;
      _lostSince  = 0;
      note = "spool removed";
      return SCAN_REMOVED;
    }
    return SCAN_NO_TAG;
  }
  _lostSince  = 0;  // seen again, so it never really left
  _quietPolls = 0;

  if (uidLen == _lastUidLen && memcmp(uid, _lastUid, uidLen) == 0) {
    return SCAN_SAME_TAG;
  }

  // Dwell filter. A spool carried past the antenna is in range for a moment;
  // one being loaded sits there. Only the latter is worth a decode — which
  // also spares us the ~1 s of MIFARE traffic on a tag that's already gone.
  uint32_t nowMs = millis();
  if (uidLen != _candUidLen || memcmp(uid, _candUid, uidLen) != 0) {
    memcpy(_candUid, uid, uidLen);
    _candUidLen = uidLen;
    _candSince  = nowMs;
    return SCAN_SETTLING;
  }
  if (nowMs - _candSince < g_settings.dwellMs) return SCAN_SETTLING;

  out.clear();

  bool decoded = false;
  if (uidLen == 7) {
    // 7-byte UID => NTAG21x / Ultralight family. OpenSpool lives here.
    decoded = tryOpenSpool(uid, uidLen, out);
    if (!decoded) note = "NTAG found but no OpenSpool NDEF record on it";
  } else if (uidLen == 4) {
    // 4-byte UID => MIFARE Classic 1K. Bambu first (keys are derivable), then
    // the fixed-key vendors.
    decoded = tryBambu(uid, uidLen, out);
    if (!decoded) decoded = tryQidi(uid, uidLen, out);
    if (!decoded) {
      // The likely answer on a U1 owner's desk is a Snapmaker spool: those are
      // MIFARE Classic 1K too, with per-tag keys nobody outside Snapmaker has,
      // so no dump and no decoder will ever open one. Say the useful thing
      // instead of listing what it isn't — the NTAG215 route works today and
      // costs pennies. (Creality's are AES and equally closed.)
      note = "MIFARE Classic tag with keys we don't have — a Snapmaker or "
             "Creality spool, most likely. Neither format is public, so this "
             "cannot be decoded. Put an NTAG215 sticker on the spool and write "
             "it from Copy tag JSON; the printer reads those natively.";
    }
  } else {
    note = "unrecognised tag family";
  }

  memcpy(_lastUid, uid, uidLen);
  _lastUidLen = uidLen;

  if (!decoded) {
    out.clear();
    out.source = SRC_UNKNOWN;
    memcpy(out.uid, uid, uidLen);
    out.uidLen = uidLen;
    snprintf(out.cardType, sizeof(out.cardType), "%s",
             uidLen == 7 ? "NTAG21x" : "MIFARE_1K");
    return SCAN_UNREADABLE;
  }

  normalizeForU1(out);
  if (g_settings.forceGenericVendor) {
    snprintf(out.vendor, sizeof(out.vendor), "Generic");
  }
  return SCAN_NEW_TAG;
}
