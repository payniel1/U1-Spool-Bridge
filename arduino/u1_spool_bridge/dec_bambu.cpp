// ---------------------------------------------------------------------------
// Bambu Lab decoder — MIFARE Classic 1K.
//
// Key derivation and the block map are from the public
// Bambu-Research-Group/RFID-Tag-Guide reverse-engineering work:
//
//   keys = HKDF-SHA256(ikm = tag UID,
//                      salt = 9a759cf2c4f7caff222cb9769b41bc96,
//                      info = "RFID-A\0",
//                      L    = 16 * 6)
//   keys[s] is the 6-byte Key A for sector s.
//
// This only recovers data from tags you physically own; it does not forge the
// RSA signature block, so these tags stay read-only as far as we're concerned.
// ---------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>

#include "crypto_hkdf.h"
#include "decoders.h"

static const uint8_t BAMBU_SALT[16] = {0x9a, 0x75, 0x9c, 0xf2, 0xc4, 0xf7,
                                       0xca, 0xff, 0x22, 0x2c, 0xb9, 0x76,
                                       0x9b, 0x41, 0xbc, 0x96};
static const uint8_t BAMBU_INFO[7] = {'R', 'F', 'I', 'D', '-', 'A', 0x00};

void bambu_derive_keys(const uint8_t *uid, size_t uidLen, uint8_t keys[16][6]) {
  uint8_t okm[16 * 6];
  hkdf_sha256(BAMBU_SALT, sizeof(BAMBU_SALT), uid, uidLen, BAMBU_INFO,
              sizeof(BAMBU_INFO), okm, sizeof(okm));
  for (int s = 0; s < 16; s++) memcpy(keys[s], okm + s * 6, 6);
}

// Absolute block numbers we need; sector n owns blocks 4n..4n+2 (4n+3 is the
// key trailer and holds nothing useful).
const uint8_t BAMBU_BLOCKS_OF_INTEREST[] = {1, 2, 4, 5, 6, 9, 10, 12, 14, 16, 0xFF};

bool bambu_decode(const uint8_t blocks[64][16], const bool present[64],
                  SpoolData &out) {
  // Block 2 holds the filament family; without it we have nothing.
  if (!present[2] && !present[4]) return false;

  out.source = SRC_BAMBU;
  out.valid  = true;
  snprintf(out.vendor, sizeof(out.vendor), "Bambu Lab");

  char family[20] = {0};
  if (present[2]) copyTrimmed(family, sizeof(family), blocks[2], 16);
  if (present[4]) copyTrimmed(out.detailedType, sizeof(out.detailedType), blocks[4], 16);
  if (out.detailedType[0] == '\0' && family[0]) {
    snprintf(out.detailedType, sizeof(out.detailedType), "%s", family);
  }

  const char *typeSrc = family[0] ? family : out.detailedType;
  normalizeMainType(typeSrc, out.mainType, sizeof(out.mainType));
  snprintf(out.subType, sizeof(out.subType), "%s",
           mapSubTypeForU1(out.detailedType, out.mainType));

  // Block 1: material variant id (0..7) + material id (8..15). Keep the
  // material id as the SKU string — it is what Bambu prints on the label.
  if (present[1]) {
    char matId[10] = {0};
    copyTrimmed(matId, sizeof(matId), blocks[1] + 8, 8);
    if (matId[0]) snprintf(out.skuStr, sizeof(out.skuStr), "%s", matId);
  }

  // Block 5: colour RGBA, spool weight, filament diameter.
  if (present[5]) {
    const uint8_t *b = blocks[5];
    out.rgb   = ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2];
    out.alpha = b[3] ? b[3] : 255;
    out.weightG = rd_u16le(b + 4);
    float dia = rd_f32le(b + 8);
    if (dia > 0.5f && dia < 5.0f) out.diameterUm = (uint16_t)(dia * 1000.0f + 0.5f);
  }

  // Block 6: drying + temperature envelope.
  if (present[6]) {
    const uint8_t *b = blocks[6];
    out.dryTemp   = rd_u16le(b + 0);
    out.dryTimeH  = rd_u16le(b + 2);
    out.bedTemp   = rd_u16le(b + 6);
    out.hotendMax = rd_u16le(b + 8);
    out.hotendMin = rd_u16le(b + 10);
  }

  // Block 9: tray UID (Bambu's per-spool serial).
  if (present[9]) copyTrimmed(out.tray, sizeof(out.tray), blocks[9], 16);

  // Block 12: production date/time.
  if (present[12]) copyTrimmed(out.prodDate, sizeof(out.prodDate), blocks[12], 16);

  // Block 14 +4: filament length in metres.
  if (present[14]) out.lengthM = rd_u16le(blocks[14] + 4);

  // Block 16 +4: secondary colour, stored ABGR.
  if (present[16]) {
    const uint8_t *b = blocks[16] + 4;
    uint32_t abgr = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
                    ((uint32_t)b[2] << 8) | b[3];
    if (abgr != 0 && abgr != 0xFFFFFFFF) {
      // ABGR -> RGB
      out.rgb2 = ((abgr >> 8) & 0xFF) << 16 | ((abgr >> 16) & 0xFF) << 8 |
                 ((abgr >> 24) & 0xFF);
    }
  }

  return true;
}
