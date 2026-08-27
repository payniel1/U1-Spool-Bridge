// ---------------------------------------------------------------------------
// spool_data.h — vendor-neutral representation of one filament spool.
//
// Every tag decoder produces one of these; the U1 client consumes one of these.
// Deliberately free of Arduino types so the decoders can be unit-tested on a
// host machine (`pio test -e native`).
// ---------------------------------------------------------------------------
#pragma once

#include <stddef.h>
#include <stdint.h>

enum TagSource : uint8_t {
  SRC_NONE = 0,
  SRC_OPENSPOOL,  // NTAG21x, NDEF JSON, OpenSpool protocol  (what the U1 reads)
  SRC_BAMBU,      // MIFARE Classic 1K, keys derived from UID
  SRC_QIDI,       // MIFARE Classic 1K (FM11RF08S), 3-byte code record
  SRC_CREALITY,   // MIFARE Classic 1K, AES payload — detected, not decoded
  SRC_UNKNOWN,    // tag present, format not recognised
  SRC_MANUAL,     // typed in via the web UI
  SRC_SPOOLMAN    // UID meant nothing on its own; Spoolman knew the spool
};

const char *tagSourceName(TagSource s);

struct SpoolData {
  bool      valid;
  TagSource source;

  char vendor[32];        // "Bambu Lab", "Snapmaker", "Generic", ...
  char mainType[16];      // PLA, PETG, PCTG, ABS, ASA, TPU, PVA, PC, PA, PAHT,
                          // PP, PPA, PPS, HIPS — plus a -CF / -GF suffix
  char subType[24];       // Basic, Matte, Silk, Support, HF, 95A, ...
  char detailedType[56];  // raw vendor / Spoolman name, display only

  uint32_t rgb;    // 0xRRGGBB primary colour
  uint32_t rgb2;   // 0xRRGGBB secondary colour (0 = unused)
  uint8_t  alpha;  // 0..255

  uint16_t hotendMin;   // degC
  uint16_t hotendMax;   // degC
  uint16_t bedTemp;     // degC
  uint16_t dryTemp;     // degC   (0 = unknown)
  uint16_t dryTimeH;    // hours  (0 = unknown)
  uint16_t weightG;     // grams  (net filament weight)
  uint16_t diameterUm;  // 1750 = 1.75 mm
  uint16_t lengthM;     // metres (0 = unknown)

  uint32_t sku;          // numeric SKU for the U1 (0 = unset)
  char     skuStr[24];   // vendor material id, if it is not numeric
  char     tray[24];     // Bambu tray UID
  char     prodDate[24]; // production date string, if the tag carries one

  uint8_t uid[10];
  uint8_t uidLen;
  char    cardType[16];  // "NTAG215", "MIFARE_1K", ...

  // Populated when the tag resolves to a Spoolman inventory entry.
  uint32_t spoolmanId;  // 0 = not linked
  uint16_t remainingG;  // grams left on the spool per Spoolman (0 = unknown)

  void clear();
  bool sameTagAs(const SpoolData &other) const;
};

// Fill in anything the tag did not carry: sane temperatures for the material,
// a U1-legal SUB_TYPE, 1.75 mm diameter, opaque alpha.
void normalizeForU1(SpoolData &d);

// Map an arbitrary vendor sub-type string onto the closed set the U1 firmware
// accepts. Returns "Basic" when nothing matches.
const char *mapSubTypeForU1(const char *raw, const char *mainType);

// Normalise a vendor material string ("PLA Matte", "pla-cf") to a U1 MAIN_TYPE.
void normalizeMainType(const char *raw, char *out, size_t outLen);

// Default temperatures for a material, used when the tag omits them.
void defaultTempsFor(const char *mainType, uint16_t *hotMin, uint16_t *hotMax,
                     uint16_t *bed);

// Small helpers shared by the decoders.
uint16_t rd_u16le(const uint8_t *p);
float    rd_f32le(const uint8_t *p);
void     copyTrimmed(char *dst, size_t dstLen, const uint8_t *src, size_t srcLen);
bool     parseHexColor(const char *s, uint32_t *rgb, uint8_t *alpha);
void     uidToHex(const uint8_t *uid, uint8_t len, char *out, size_t outLen);
