// ---------------------------------------------------------------------------
// decoders.h — pure (hardware-free) tag decoders.
//
// Each decoder takes raw bytes that were already lifted off the tag and turns
// them into a SpoolData. No PN532 or Arduino calls live in here, which is what
// makes `pio test -e native` possible.
// ---------------------------------------------------------------------------
#pragma once

#include "spool_data.h"

// --- OpenSpool: NTAG213/215/216 carrying an NDEF message ------------------

// Walk an NTAG user-memory TLV area and hand back the first NDEF record
// payload that looks like JSON. `tlv` should start at page 4.
bool ndef_find_json(const uint8_t *tlv, size_t len, const uint8_t **payload,
                    size_t *payloadLen);

// Parse an OpenSpool JSON document into a SpoolData.
bool openspool_parse_json(const char *json, size_t len, SpoolData &out);

// Convenience: TLV bytes straight in, SpoolData out.
bool openspool_decode(const uint8_t *tlv, size_t len, SpoolData &out);

// Build the OpenSpool JSON for a spool (used by the web UI's "export tag"
// helper and by the /api/tagjson endpoint).
size_t openspool_build_json(const SpoolData &d, char *out, size_t outLen);

// --- Bambu Lab: MIFARE Classic 1K, per-sector keys derived from the UID ---

// keys[s] is the 6-byte Key A for sector s (0..15).
void bambu_derive_keys(const uint8_t *uid, size_t uidLen, uint8_t keys[16][6]);

// `blocks` is indexed by absolute block number (0..63); `present[i]` says
// whether that block was actually read.
bool bambu_decode(const uint8_t blocks[64][16], const bool present[64],
                  SpoolData &out);

// Which absolute blocks the Bambu decoder wants. Terminated by 0xFF.
extern const uint8_t BAMBU_BLOCKS_OF_INTEREST[];

// --- QIDI: MIFARE Classic 1K (FM11RF08S), 3 bytes in sector 1 block 0 -----

bool qidi_decode(const uint8_t *block4, SpoolData &out);
const char *qidi_material_name(uint8_t code);
bool        qidi_color_rgb(uint8_t code, uint32_t *rgb);
