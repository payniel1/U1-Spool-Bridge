// ---------------------------------------------------------------------------
// tag_reader.h — PN532 front end.
//
// Owns the radio, works out what kind of tag is on the antenna, pulls the
// bytes off it and hands them to the right decoder in decoders.h.
// ---------------------------------------------------------------------------
#pragma once

#include <Adafruit_PN532.h>
#include <Arduino.h>

#include "config.h"
#include "spool_data.h"

enum ScanResult : uint8_t {
  SCAN_NO_TAG = 0,   // nothing on the antenna
  SCAN_SAME_TAG,     // same tag as last time, nothing to do
  SCAN_SETTLING,     // a new tag is there but hasn't sat still long enough yet
  SCAN_NEW_TAG,      // decoded successfully — `out` is populated
  SCAN_UNREADABLE,   // tag present but we could not decode it — `out` has the UID
  SCAN_REMOVED,      // the tag really has gone (not just a momentary dropout)
  SCAN_BUS_ERROR     // the reader stopped answering — the bus needs resetting
};

// HSU only. The I2C and SPI paths are gone: I2C is what the
// ESP_ERR_INVALID_STATE fault was — one bad transaction latches the IDF driver
// and the reader is dead until reboot — and SPI needed four wires on different
// pads to avoid a fault a UART cannot have in the first place.
struct ReaderPins {
  int8_t rx = -1, tx = -1;
  int8_t rst = -1;   // RSTO. Not optional: the driver pulses it on every init.
};

// A full MIFARE Classic 1K read-out, for working out a tag format we do not yet
// have a decoder for. 16 sectors of 4 blocks; block 3 of each is the trailer
// (keys + access bits) and is deliberately not stored — it is the one block
// whose contents are secrets rather than data.
struct CardDump {
  uint8_t uid[10] = {0};
  uint8_t uidLen  = 0;
  bool    classic = false;          // false = not a MIFARE Classic 1K
  bool    ok[16]  = {false};        // did this sector authenticate at all
  char    keyUsed[16][13] = {{0}};  // the key that worked, hex
  char    keyType[16] = {0};        // 'A' or 'B'
  uint8_t data[16][3][16] = {{{0}}};// blocks 0..2 of each sector
  uint8_t sectorsRead = 0;
};

class TagReader {
 public:
  // Try every key we know against every sector and record what comes back.
  // Slow — hundreds of authenticate attempts, each needing the card reselected
  // — so it runs from the main loop, never the web server task.
  bool dumpCard(CardDump &out);

  bool       begin(uint8_t index, const ReaderPins &pins);
  uint8_t    index() const { return _index; }
  const char *busName() const;
  bool       ready() const { return _ready; }

  // Close the port, hard-reset the PN532 over RSTO and start over. On HSU
  // there is no bus state to unstick — that was the I2C failure this whole
  // mechanism was built for — but a module that has stopped answering still
  // needs its reset line pulled.
  bool       recover();
  // Ask the reader something only a live one can answer. Cheap, and safe to
  // call whenever the bus is otherwise quiet.
  bool       alive();
  uint16_t   recoveries() const { return _recoveries; }
  uint32_t   lastRecoveryAt() const { return _lastRecoveryAt; }
  bool       everWorked() const { return _everWorked; }
  uint32_t   firmwareVersion() const { return _fwVersion; }
  ScanResult poll(SpoolData &out, String &note);
  void       forgetLastTag() { _lastUidLen = 0; _candUidLen = 0; }
  const char *lastError() const { return _lastError.c_str(); }

 private:
  bool reselect(uint8_t *uid, uint8_t *uidLen);
  bool readNtagBytes(uint8_t *buf, size_t maxLen, size_t *outLen);
  bool tryOpenSpool(uint8_t *uid, uint8_t uidLen, SpoolData &out);
  bool tryBambu(uint8_t *uid, uint8_t uidLen, SpoolData &out);
  bool tryQidi(uint8_t *uid, uint8_t uidLen, SpoolData &out);

  Adafruit_PN532 *_nfc = nullptr;
  ReaderPins      _pins;
  uint8_t         _index = 0;
  bool            _ready = false;
  uint32_t        _fwVersion = 0;
  uint8_t         _lastUid[10] = {0};
  uint8_t         _lastUidLen = 0;
  // Dwell filter: a tag has to stay put before we bother decoding it, so a
  // spool swinging past the antenna is ignored outright.
  uint8_t         _candUid[10] = {0};
  uint8_t         _candUidLen = 0;
  uint32_t        _candSince = 0;
  // A spool living in a drybox sits at the edge of the field and blinks out
  // now and then. Only a sustained absence counts as removal.
  uint32_t        _lostSince = 0;
  // Health watch: after a run of empty polls, check the reader is still
  // answering at all. Cheap, and it never runs while a tag is present.
  uint16_t        _quietPolls = 0;
  // Health probes since the last re-arm. See the SAMConfig note in poll().
  uint16_t        _probes = 0;
  // Consecutive failed probes. One failure is not evidence — getFirmwareVersion()
  // returns 0 for a misaligned reply as readily as for a dead reader.
  uint8_t         _probeFails = 0;
  uint16_t        _recoveries = 0;
  uint32_t        _lastRecoveryAt = 0;
  bool            _everWorked = false;
  String          _lastError;
};

extern TagReader g_readers[MAX_READERS];
// Bring every configured reader up. Returns how many answered.
uint8_t readersBegin();
