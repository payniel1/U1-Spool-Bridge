// Host-side tests for the pure decoding logic:  pio test -e native
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <string>

#include "crypto_hkdf.h"
#include "fleet_wire.h"
#include "decoders.h"
#include "send_gate.h"
#include "spool_data.h"
#include "spoolman_fields.h"
#include "ota_stall.h"
#include "u1_reply.h"

// ---------------------------------------------------------------------------
// crypto
// ---------------------------------------------------------------------------

static void toHex(const uint8_t *b, size_t n, char *out) {
  for (size_t i = 0; i < n; i++) sprintf(out + i * 2, "%02x", b[i]);
  out[n * 2] = 0;
}

void test_sha256_vector() {
  uint8_t d[32];
  char    hex[80];
  sha256((const uint8_t *)"abc", 3, d);
  toHex(d, 32, hex);
  TEST_ASSERT_EQUAL_STRING(
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", hex);
}

void test_hmac_sha256_vector() {
  uint8_t d[32];
  char    hex[80];
  const char *msg = "The quick brown fox jumps over the lazy dog";
  hmac_sha256((const uint8_t *)"key", 3, (const uint8_t *)msg, strlen(msg), d);
  toHex(d, 32, hex);
  TEST_ASSERT_EQUAL_STRING(
      "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8", hex);
}

// Reference values produced with Python hmac/hashlib using the published
// Bambu parameters (salt 9a759c..., info "RFID-A\0", ikm = UID).
void test_bambu_key_derivation() {
  const uint8_t uid[4] = {0x04, 0x1A, 0x2B, 0x3C};
  uint8_t       keys[16][6];
  bambu_derive_keys(uid, sizeof(uid), keys);

  char hex[16];
  toHex(keys[0], 6, hex);
  TEST_ASSERT_EQUAL_STRING("f0251d751224", hex);
  toHex(keys[1], 6, hex);
  TEST_ASSERT_EQUAL_STRING("62d2e494b653", hex);
  toHex(keys[15], 6, hex);
  TEST_ASSERT_EQUAL_STRING("7f654a428b7b", hex);
}

// ---------------------------------------------------------------------------
// OpenSpool / NDEF
// ---------------------------------------------------------------------------

// A real NTAG user area: NDEF Message TLV -> MIME record -> OpenSpool JSON.
static const uint8_t OPENSPOOL_TLV[] = {
    0x03, 0xEF, 0xD2, 0x10, 0xDC, 0x61, 0x70, 0x70, 0x6C, 0x69, 0x63, 0x61,
    0x74, 0x69, 0x6F, 0x6E, 0x2F, 0x6A, 0x73, 0x6F, 0x6E, 0x7B, 0x22, 0x70,
    0x72, 0x6F, 0x74, 0x6F, 0x63, 0x6F, 0x6C, 0x22, 0x3A, 0x22, 0x6F, 0x70,
    0x65, 0x6E, 0x73, 0x70, 0x6F, 0x6F, 0x6C, 0x22, 0x2C, 0x22, 0x76, 0x65,
    0x72, 0x73, 0x69, 0x6F, 0x6E, 0x22, 0x3A, 0x22, 0x31, 0x2E, 0x30, 0x22,
    0x2C, 0x22, 0x62, 0x72, 0x61, 0x6E, 0x64, 0x22, 0x3A, 0x22, 0x53, 0x6E,
    0x61, 0x70, 0x6D, 0x61, 0x6B, 0x65, 0x72, 0x22, 0x2C, 0x22, 0x74, 0x79,
    0x70, 0x65, 0x22, 0x3A, 0x22, 0x50, 0x4C, 0x41, 0x22, 0x2C, 0x22, 0x73,
    0x75, 0x62, 0x74, 0x79, 0x70, 0x65, 0x22, 0x3A, 0x22, 0x4D, 0x61, 0x74,
    0x74, 0x65, 0x22, 0x2C, 0x22, 0x61, 0x6C, 0x70, 0x68, 0x61, 0x22, 0x3A,
    0x22, 0x46, 0x46, 0x22, 0x2C, 0x22, 0x63, 0x6F, 0x6C, 0x6F, 0x72, 0x5F,
    0x68, 0x65, 0x78, 0x22, 0x3A, 0x22, 0x30, 0x30, 0x30, 0x30, 0x46, 0x46,
    0x22, 0x2C, 0x22, 0x6D, 0x69, 0x6E, 0x5F, 0x74, 0x65, 0x6D, 0x70, 0x22,
    0x3A, 0x31, 0x39, 0x30, 0x2C, 0x22, 0x6D, 0x61, 0x78, 0x5F, 0x74, 0x65,
    0x6D, 0x70, 0x22, 0x3A, 0x32, 0x32, 0x30, 0x2C, 0x22, 0x62, 0x65, 0x64,
    0x5F, 0x6D, 0x69, 0x6E, 0x5F, 0x74, 0x65, 0x6D, 0x70, 0x22, 0x3A, 0x35,
    0x30, 0x2C, 0x22, 0x62, 0x65, 0x64, 0x5F, 0x6D, 0x61, 0x78, 0x5F, 0x74,
    0x65, 0x6D, 0x70, 0x22, 0x3A, 0x36, 0x30, 0x2C, 0x22, 0x64, 0x69, 0x61,
    0x6D, 0x65, 0x74, 0x65, 0x72, 0x22, 0x3A, 0x31, 0x37, 0x35, 0x2C, 0x22,
    0x77, 0x65, 0x69, 0x67, 0x68, 0x74, 0x22, 0x3A, 0x31, 0x30, 0x30, 0x30,
    0x7D, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
};

void test_openspool_decode() {
  SpoolData d;
  d.clear();
  TEST_ASSERT_TRUE(openspool_decode(OPENSPOOL_TLV, sizeof(OPENSPOOL_TLV), d));
  TEST_ASSERT_EQUAL(SRC_OPENSPOOL, d.source);
  TEST_ASSERT_EQUAL_STRING("Snapmaker", d.vendor);
  TEST_ASSERT_EQUAL_STRING("PLA", d.mainType);
  TEST_ASSERT_EQUAL_STRING("Matte", d.subType);
  TEST_ASSERT_EQUAL_HEX32(0x0000FF, d.rgb);
  TEST_ASSERT_EQUAL(255, d.alpha);
  TEST_ASSERT_EQUAL(190, d.hotendMin);
  TEST_ASSERT_EQUAL(220, d.hotendMax);
  TEST_ASSERT_EQUAL(60, d.bedTemp);   // top of the OpenSpool bed range
  TEST_ASSERT_EQUAL(1000, d.weightG);
  TEST_ASSERT_EQUAL(1750, d.diameterUm);
}

void test_openspool_roundtrip() {
  SpoolData a;
  a.clear();
  TEST_ASSERT_TRUE(openspool_decode(OPENSPOOL_TLV, sizeof(OPENSPOOL_TLV), a));

  char json[400];
  size_t n = openspool_build_json(a, json, sizeof(json));
  TEST_ASSERT_TRUE(n > 0);

  SpoolData b;
  b.clear();
  TEST_ASSERT_TRUE(openspool_parse_json(json, n, b));
  TEST_ASSERT_EQUAL_STRING(a.mainType, b.mainType);
  TEST_ASSERT_EQUAL_STRING(a.subType, b.subType);
  TEST_ASSERT_EQUAL_HEX32(a.rgb, b.rgb);
  TEST_ASSERT_EQUAL(a.hotendMin, b.hotendMin);
  TEST_ASSERT_EQUAL(a.diameterUm, b.diameterUm);
}

void test_ndef_rejects_garbage() {
  const uint8_t junk[16] = {0xAA, 0xBB, 0xCC};
  SpoolData     d;
  d.clear();
  TEST_ASSERT_FALSE(openspool_decode(junk, sizeof(junk), d));
}

// ---------------------------------------------------------------------------
// Bambu
// ---------------------------------------------------------------------------

void test_bambu_decode() {
  static uint8_t blocks[64][16];
  static bool    present[64];
  memset(blocks, 0, sizeof(blocks));
  memset(present, 0, sizeof(present));

  auto put = [&](int b, const char *s) {
    memcpy(blocks[b], s, strlen(s));
    present[b] = true;
  };
  auto put16 = [&](int b, int off, uint16_t v) {
    blocks[b][off]     = (uint8_t)(v & 0xFF);
    blocks[b][off + 1] = (uint8_t)(v >> 8);
    present[b]         = true;
  };

  put(2, "PLA");
  put(4, "PLA Silk");

  // block 5: RGBA + weight + diameter
  present[5] = true;
  blocks[5][0] = 0x12; blocks[5][1] = 0x34; blocks[5][2] = 0x56; blocks[5][3] = 0xFF;
  put16(5, 4, 1000);
  float dia = 1.75f;
  memcpy(&blocks[5][8], &dia, 4);

  // block 6: dry temp / dry time / bed type / bed / hotend max / hotend min
  put16(6, 0, 55);
  put16(6, 2, 8);
  put16(6, 6, 60);
  put16(6, 8, 230);
  put16(6, 10, 190);

  put(9, "0123456789ABCDEF");
  put(12, "2024_09_01_10_30");
  put16(14, 4, 330);

  SpoolData d;
  d.clear();
  TEST_ASSERT_TRUE(bambu_decode(blocks, present, d));
  TEST_ASSERT_EQUAL(SRC_BAMBU, d.source);
  TEST_ASSERT_EQUAL_STRING("Bambu Lab", d.vendor);
  TEST_ASSERT_EQUAL_STRING("PLA", d.mainType);
  TEST_ASSERT_EQUAL_STRING("Silk", d.subType);
  TEST_ASSERT_EQUAL_HEX32(0x123456, d.rgb);
  TEST_ASSERT_EQUAL(255, d.alpha);
  TEST_ASSERT_EQUAL(1000, d.weightG);
  TEST_ASSERT_EQUAL(1750, d.diameterUm);
  TEST_ASSERT_EQUAL(190, d.hotendMin);
  TEST_ASSERT_EQUAL(230, d.hotendMax);
  TEST_ASSERT_EQUAL(60, d.bedTemp);
  TEST_ASSERT_EQUAL(55, d.dryTemp);
  TEST_ASSERT_EQUAL(330, d.lengthM);
  TEST_ASSERT_EQUAL_STRING("0123456789ABCDEF", d.tray);
}

void test_bambu_needs_data() {
  static uint8_t blocks[64][16];
  static bool    present[64];
  memset(blocks, 0, sizeof(blocks));
  memset(present, 0, sizeof(present));
  SpoolData d;
  d.clear();
  TEST_ASSERT_FALSE(bambu_decode(blocks, present, d));
}

// ---------------------------------------------------------------------------
// QIDI
// ---------------------------------------------------------------------------

void test_qidi_decode() {
  uint8_t block[16] = {0};
  block[0] = 39;  // PETG Basic
  block[1] = 18;  // #FF362D
  block[2] = 1;   // QIDI

  SpoolData d;
  d.clear();
  TEST_ASSERT_TRUE(qidi_decode(block, d));
  TEST_ASSERT_EQUAL(SRC_QIDI, d.source);
  TEST_ASSERT_EQUAL_STRING("QIDI", d.vendor);
  TEST_ASSERT_EQUAL_STRING("PETG", d.mainType);
  TEST_ASSERT_EQUAL_HEX32(0xFF362D, d.rgb);

  normalizeForU1(d);
  TEST_ASSERT_EQUAL(230, d.hotendMin);  // PETG defaults filled in
  TEST_ASSERT_EQUAL(75, d.bedTemp);
}

void test_qidi_rejects_unknown_code() {
  uint8_t   block[16] = {0};
  block[0] = 200;
  SpoolData d;
  d.clear();
  TEST_ASSERT_FALSE(qidi_decode(block, d));
}

// ---------------------------------------------------------------------------
// normalisation
// ---------------------------------------------------------------------------

void test_main_type_folding() {
  char out[16];
  normalizeMainType("PETG Translucent", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("PETG", out);
  normalizeMainType("TPU 95A", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("TPU", out);
  normalizeMainType("ASA-AERO", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("ASA", out);
  normalizeMainType("something odd", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("PLA", out);
}

// Filled grades keep their suffix now: the U1 takes any MAIN_TYPE string, and a
// PA-CF spool is not a PA one.
void test_filled_grades_keep_their_suffix() {
  char out[16];
  normalizeMainType("PLA-CF", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("PLA-CF", out);
  normalizeMainType("PAHT-CF", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("PAHT-CF", out);
  normalizeMainType("PETG Carbon Fiber", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("PETG-CF", out);
  normalizeMainType("PA6-GF25", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("PA-GF", out);
  normalizeMainType("PP-GF", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("PP-GF", out);
}

// PCTG used to be folded onto PETG. It is its own material and now stays put.
void test_pctg_is_its_own_type() {
  char out[16];
  normalizeMainType("PCTG", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("PCTG", out);
  normalizeMainType("Generic PCTG Clear", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("PCTG", out);
}

// The families that share letters must not shadow one another.
void test_similar_families_do_not_collide() {
  char out[16];
  normalizeMainType("PPS-CF", out, sizeof(out));   // must not become PP or PA
  TEST_ASSERT_EQUAL_STRING("PPS-CF", out);
  normalizeMainType("PPA-CF", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("PPA-CF", out);
  normalizeMainType("PAHT", out, sizeof(out));     // must not become PA
  TEST_ASSERT_EQUAL_STRING("PAHT", out);
  normalizeMainType("PCTG", out, sizeof(out));     // must not become PC
  TEST_ASSERT_EQUAL_STRING("PCTG", out);
}

// "CF" only counts as a token, so a vendor name is not enough to promote a
// plain filament to a composite.
void test_fibre_needs_a_token_not_a_substring() {
  char out[16];
  normalizeMainType("PLA Scfold", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("PLA", out);
  normalizeMainType("PLA CF", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("PLA-CF", out);
}

void test_filled_grades_run_hotter() {
  uint16_t lo = 0, hi = 0, bd = 0;
  defaultTempsFor("PLA", &lo, &hi, &bd);
  TEST_ASSERT_EQUAL(200, (int)lo);
  defaultTempsFor("PLA-CF", &lo, &hi, &bd);
  TEST_ASSERT_EQUAL(210, (int)lo);   // base + 10
  TEST_ASSERT_EQUAL(55, (int)bd);    // bed unchanged
  defaultTempsFor("PPS-CF", &lo, &hi, &bd);
  TEST_ASSERT_EQUAL(330, (int)lo);
}

void test_subtype_mapping() {
  TEST_ASSERT_EQUAL_STRING("Matte", mapSubTypeForU1("PLA Matte", "PLA"));
  TEST_ASSERT_EQUAL_STRING("Silk", mapSubTypeForU1("PLA Silk+", "PLA"));
  TEST_ASSERT_EQUAL_STRING("Support", mapSubTypeForU1("Support For PA", "PVA"));
  TEST_ASSERT_EQUAL_STRING("HF", mapSubTypeForU1("PETG Rapido", "PETG"));
  TEST_ASSERT_EQUAL_STRING("95A", mapSubTypeForU1("TPU", "TPU"));
  TEST_ASSERT_EQUAL_STRING("95A HF", mapSubTypeForU1("TPU-Aero", "TPU"));
  TEST_ASSERT_EQUAL_STRING("Basic", mapSubTypeForU1("PLA", "PLA"));
}

void test_normalize_fills_gaps() {
  SpoolData d;
  d.clear();
  snprintf(d.detailedType, sizeof(d.detailedType), "ABS");
  normalizeForU1(d);
  TEST_ASSERT_EQUAL_STRING("ABS", d.mainType);
  TEST_ASSERT_EQUAL_STRING("Generic", d.vendor);
  TEST_ASSERT_EQUAL(240, d.hotendMin);
  TEST_ASSERT_EQUAL(95, d.bedTemp);
  TEST_ASSERT_EQUAL(255, d.alpha);
  TEST_ASSERT_EQUAL(1750, d.diameterUm);
}

void test_parse_hex_color() {
  uint32_t rgb = 0;
  uint8_t  a = 0;
  TEST_ASSERT_TRUE(parseHexColor("#1A2B3C", &rgb, &a));
  TEST_ASSERT_EQUAL_HEX32(0x1A2B3C, rgb);
  TEST_ASSERT_TRUE(parseHexColor("1A2B3C80", &rgb, &a));
  TEST_ASSERT_EQUAL_HEX32(0x1A2B3C, rgb);
  TEST_ASSERT_EQUAL(0x80, a);
  TEST_ASSERT_FALSE(parseHexColor("nope", &rgb, &a));
}

// ---------------------------------------------------------------------------
// Spoolman field encoding
// ---------------------------------------------------------------------------

void test_spoolman_json_encoding() {
  // Spoolman JSON-encodes every extra-field value, whatever its declared type.
  TEST_ASSERT_EQUAL_STRING("\"AABBCCDD\"", smJsonQuote("AABBCCDD").c_str());
  TEST_ASSERT_EQUAL_STRING("AABBCCDD", smJsonUnquote("\"AABBCCDD\"").c_str());
  // Round trip through a value that needs escaping.
  std::string tricky = "a\"b\\c";
  TEST_ASSERT_EQUAL_STRING(tricky.c_str(),
                           smJsonUnquote(smJsonQuote(tricky)).c_str());
  // A bare value (someone edited it by hand) is taken at face value.
  TEST_ASSERT_EQUAL_STRING("AABBCCDD", smJsonUnquote("AABBCCDD").c_str());
  TEST_ASSERT_EQUAL_STRING("", smJsonUnquote("").c_str());
}

void test_uid_list_contains() {
  TEST_ASSERT_TRUE(smUidListContains("AABBCCDD", "AABBCCDD"));
  TEST_ASSERT_TRUE(smUidListContains("11223344,AABBCCDD,55667788", "AABBCCDD"));
  TEST_ASSERT_TRUE(smUidListContains("11223344, AABBCCDD ", "aabbccdd"));  // case + spaces
  TEST_ASSERT_FALSE(smUidListContains("11223344,55667788", "AABBCCDD"));
  TEST_ASSERT_FALSE(smUidListContains("", "AABBCCDD"));
  // Must not match on a substring — this is the bug that would silently bind
  // a tag to the wrong spool.
  TEST_ASSERT_FALSE(smUidListContains("AABBCCDDEE", "AABBCCDD"));
  TEST_ASSERT_FALSE(smUidListContains("11AABBCCDD", "AABBCCDD"));
}

void test_uid_list_add() {
  TEST_ASSERT_EQUAL_STRING("AABBCCDD", smUidListAdd("", "AABBCCDD").c_str());
  TEST_ASSERT_EQUAL_STRING("1122,AABBCCDD", smUidListAdd("1122", "AABBCCDD").c_str());
  // Already present, in any case: unchanged.
  TEST_ASSERT_EQUAL_STRING("1122,aabbccdd",
                           smUidListAdd("1122,aabbccdd", "AABBCCDD").c_str());
  // Messy input gets normalised on the way through.
  TEST_ASSERT_EQUAL_STRING("1122,3344,AABBCCDD",
                           smUidListAdd(" 1122 , 3344 ,", "AABBCCDD").c_str());
}

void test_uid_list_remove() {
  TEST_ASSERT_EQUAL_STRING("1122,3344",
                           smUidListRemove("1122,AABBCCDD,3344", "AABBCCDD").c_str());
  TEST_ASSERT_EQUAL_STRING("", smUidListRemove("AABBCCDD", "aabbccdd").c_str());
  TEST_ASSERT_EQUAL_STRING("1122", smUidListRemove("1122", "AABBCCDD").c_str());
  TEST_ASSERT_EQUAL_STRING("", smUidListRemove("", "AABBCCDD").c_str());
}

void test_location_format() {
  TEST_ASSERT_EQUAL_STRING("U1 slot 2",
                           smFormatLocation("U1 slot {slot}", 2, "", "Drybox 1").c_str());
  // An empty format falls back to the same thing a fresh box defaults to:
  // the group alone, so a whole group shares one Spoolman location.
  TEST_ASSERT_EQUAL_STRING("Drybox 1", smFormatLocation("", 1, "", "Drybox 1").c_str());
  TEST_ASSERT_EQUAL_STRING("Printer A",
                           smFormatLocation("", 1, "Printer A", "Drybox 1").c_str());
  TEST_ASSERT_EQUAL_STRING("Shelf A", smFormatLocation("Shelf A", 3, "", "Drybox 1").c_str());
  // A stray printf token must survive verbatim — the format string comes from
  // the user, and it never reaches printf.
  TEST_ASSERT_EQUAL_STRING("%s %d 4",
                           smFormatLocation("%s %d {slot}", 4, "", "Drybox 1").c_str());
}

// The point of the default: every box in a group, whatever its slot, produces
// the SAME location string, so Spoolman shows one heading for the group rather
// than one per box.
void test_location_group_only_is_shared_across_slots() {
  const char *fmt = "{group}";
  for (int slot = 1; slot <= 4; slot++) {
    TEST_ASSERT_EQUAL_STRING(
        "Printer A", smFormatLocation(fmt, slot, "Printer A", "Drybox 2").c_str());
  }
  // ...and two different groups still differ.
  TEST_ASSERT_EQUAL_STRING("Printer B",
                           smFormatLocation(fmt, 1, "Printer B", "Drybox 5").c_str());
}

void test_location_group_token() {
  TEST_ASSERT_EQUAL_STRING(
      "Printer A slot 2",
      smFormatLocation("{group} slot {slot}", 2, "Printer A", "Drybox 3").c_str());
  // No group set: fall back to the box name rather than collapsing every
  // ungrouped box into one blank location.
  TEST_ASSERT_EQUAL_STRING(
      "Drybox 3 slot 2",
      smFormatLocation("{group} slot {slot}", 2, "", "Drybox 3").c_str());
  TEST_ASSERT_EQUAL_STRING(
      "Drybox 3", smFormatLocation("{box}", 1, "Printer A", "Drybox 3").c_str());
  // Repeated tokens all get replaced.
  TEST_ASSERT_EQUAL_STRING(
      "A/A/1", smFormatLocation("{group}/{group}/{slot}", 1, "A", "B").c_str());
}

// A replacement value that itself contains a token must not be rescanned, or a
// group literally named "{slot}" would expand forever.
void test_location_substitution_does_not_recurse() {
  TEST_ASSERT_EQUAL_STRING(
      "{slot} 4", smFormatLocation("{group} {slot}", 4, "{slot}", "B").c_str());
}

void test_location_all_tokens_empty_is_blank_not_padded() {
  TEST_ASSERT_EQUAL_STRING("", smFormatLocation("  {group}  ", 1, "", "").c_str());
}

void test_comment_trail() {
  TEST_ASSERT_EQUAL_STRING("first", smAppendComment("", "first", 100).c_str());
  TEST_ASSERT_EQUAL_STRING("a\nb", smAppendComment("a", "b", 100).c_str());

  // Overflow drops whole lines from the top, never mid-line.
  std::string big;
  for (int i = 0; i < 40; i++) big += "line-" + std::to_string(i) + "\n";
  big.pop_back();
  std::string out = smAppendComment(big, "newest", 60);
  TEST_ASSERT_TRUE(out.size() <= 60);
  TEST_ASSERT_TRUE(out.rfind("newest") != std::string::npos);
  TEST_ASSERT_TRUE(out.find("line-0\n") == std::string::npos);
  // Whatever survived must begin at a line boundary, not mid-word.
  TEST_ASSERT_TRUE(out.rfind("line-", 0) == 0 || out.rfind("newest", 0) == 0);
}

// ---------------------------------------------------------------------------
// Send gate — the thing that stops a spool merely passing the reader from
// reprogramming a slot.
// ---------------------------------------------------------------------------

static const uint8_t UID_A[4] = {0x04, 0x1A, 0x2B, 0x3C};
static const uint8_t UID_B[4] = {0xDE, 0xAD, 0xBE, 0xEF};

static GateConfig cfgFor(uint8_t mode) {
  GateConfig c;
  c.mode         = mode;
  c.cooldownMs   = 30000;
  c.scanValidMs  = 300000;
  c.armTimeoutMs = 120000;
  return c;
}

void test_gate_on_load_holds_the_scan() {
  GateConfig c = cfgFor(TRIG_ON_LOAD);
  GateState  s;

  // Scanning alone must never reach the printer in this mode.
  GateDecision d = gateOnScan(c, s, UID_A, 4, 0, 1000);
  TEST_ASSERT_FALSE(d.send);
  TEST_ASSERT_TRUE(s.havePending);

  // Ten more passes of the same spool: still nothing.
  for (uint32_t t = 2000; t < 12000; t += 1000) {
    TEST_ASSERT_FALSE(gateOnScan(c, s, UID_A, 4, 0, t).send);
  }

  // The printer reports slot 3 filling up — now it goes, to slot 3, not to
  // the default slot.
  d = gateOnChannelLoaded(c, s, 2, 13000);
  TEST_ASSERT_TRUE(d.send);
  TEST_ASSERT_EQUAL(2, d.channel);
}

void test_gate_on_load_without_a_scan() {
  GateConfig   c = cfgFor(TRIG_ON_LOAD);
  GateState    s;
  GateDecision d = gateOnChannelLoaded(c, s, 1, 5000);
  TEST_ASSERT_FALSE(d.send);
}

void test_gate_on_load_ignores_a_stale_scan() {
  GateConfig c = cfgFor(TRIG_ON_LOAD);
  GateState  s;
  gateOnScan(c, s, UID_A, 4, 0, 1000);
  // Loaded a spool by hand ten minutes after waving a different one past.
  GateDecision d = gateOnChannelLoaded(c, s, 0, 1000 + 600000);
  TEST_ASSERT_FALSE(d.send);
  TEST_ASSERT_FALSE(s.havePending);
}

void test_gate_load_event_ignored_in_other_modes() {
  GateConfig c = cfgFor(TRIG_MANUAL);
  GateState  s;
  gateOnScan(c, s, UID_A, 4, 0, 1000);
  TEST_ASSERT_FALSE(gateOnChannelLoaded(c, s, 0, 2000).send);
}

void test_gate_always_mode_with_cooldown() {
  GateConfig c = cfgFor(TRIG_ALWAYS);
  GateState  s;

  GateDecision d = gateOnScan(c, s, UID_A, 4, 1, 1000);
  TEST_ASSERT_TRUE(d.send);
  TEST_ASSERT_EQUAL(1, d.channel);
  gateNoteSent(s, UID_A, 4, 1, 1000);

  // Same spool drifting in and out of range must not re-send.
  TEST_ASSERT_FALSE(gateOnScan(c, s, UID_A, 4, 1, 5000).send);
  TEST_ASSERT_FALSE(gateOnScan(c, s, UID_A, 4, 1, 30000).send);
  // A different spool is not suppressed.
  TEST_ASSERT_TRUE(gateOnScan(c, s, UID_B, 4, 1, 6000).send);
  // Nor is the same spool aimed at a different slot.
  TEST_ASSERT_TRUE(gateOnScan(c, s, UID_A, 4, 2, 7000).send);
  // And once the cooldown lapses it works again.
  TEST_ASSERT_TRUE(gateOnScan(c, s, UID_A, 4, 1, 1000 + 30001).send);
}

void test_gate_armed_mode() {
  GateConfig c = cfgFor(TRIG_ARMED);
  GateState  s;

  // Unarmed scans are held.
  TEST_ASSERT_FALSE(gateOnScan(c, s, UID_A, 4, 0, 1000).send);

  // Arming with a spool already sitting on the reader releases it at once.
  GateDecision d = gateArm(c, s, 3, 2000);
  TEST_ASSERT_TRUE(d.send);
  TEST_ASSERT_EQUAL(3, d.channel);
  gateNoteSent(s, UID_A, 4, 3, 2000);

  // Arming with nothing pending waits for the next scan.
  GateState s2;
  d = gateArm(c, s2, 1, 1000);
  TEST_ASSERT_FALSE(d.send);
  TEST_ASSERT_TRUE(s2.armed);
  d = gateOnScan(c, s2, UID_B, 4, 0, 2000);
  TEST_ASSERT_TRUE(d.send);
  TEST_ASSERT_EQUAL(1, d.channel);
  TEST_ASSERT_FALSE(s2.armed);  // one shot

  // The next scan is held again.
  TEST_ASSERT_FALSE(gateOnScan(c, s2, UID_A, 4, 0, 3000).send);
}

void test_gate_arm_times_out() {
  GateConfig c = cfgFor(TRIG_ARMED);
  GateState  s;
  gateArm(c, s, 2, 1000);
  TEST_ASSERT_TRUE(s.armed);
  gateExpire(c, s, 1000 + 119000);
  TEST_ASSERT_TRUE(s.armed);
  gateExpire(c, s, 1000 + 121000);
  TEST_ASSERT_FALSE(s.armed);
}

// One reader bolted to one drybox: the box decides the slot, and a resident
// spool must not keep re-announcing itself.
void test_gate_on_insert_uses_the_bound_slot() {
  GateConfig c = cfgFor(TRIG_ON_INSERT);
  GateState  s;

  GateDecision d = gateOnScan(c, s, UID_A, 4, 2, 1000);  // box bound to slot 3
  TEST_ASSERT_TRUE(d.send);
  TEST_ASSERT_EQUAL(2, d.channel);
  gateNoteSent(s, UID_A, 4, 2, 1000);

  // The reader keeps seeing the same spool sitting in the box. The reader's
  // absence debounce suppresses most of this; the cooldown catches the rest.
  TEST_ASSERT_FALSE(gateOnScan(c, s, UID_A, 4, 2, 2000).send);
  TEST_ASSERT_FALSE(gateOnScan(c, s, UID_A, 4, 2, 20000).send);

  // Swap in a different spool: that is a real insertion.
  d = gateOnScan(c, s, UID_B, 4, 2, 21000);
  TEST_ASSERT_TRUE(d.send);
  TEST_ASSERT_EQUAL(2, d.channel);
}

void test_gate_on_insert_ignores_load_events() {
  // A box-bound node shouldn't react to some *other* box's slot filling up.
  GateConfig c = cfgFor(TRIG_ON_INSERT);
  GateState  s;
  gateOnScan(c, s, UID_A, 4, 1, 1000);
  TEST_ASSERT_FALSE(gateOnChannelLoaded(c, s, 3, 2000).send);
}

void test_gate_manual_mode_never_fires() {
  GateConfig c = cfgFor(TRIG_MANUAL);
  GateState  s;
  TEST_ASSERT_FALSE(gateOnScan(c, s, UID_A, 4, 0, 1000).send);
  TEST_ASSERT_FALSE(gateOnChannelLoaded(c, s, 0, 2000).send);
  TEST_ASSERT_TRUE(s.havePending);  // still available for the Send button
}

void test_gate_expires_stale_pending() {
  GateConfig c = cfgFor(TRIG_ON_LOAD);
  GateState  s;
  gateOnScan(c, s, UID_A, 4, 0, 1000);
  gateExpire(c, s, 1000 + 299000);
  TEST_ASSERT_TRUE(s.havePending);
  gateExpire(c, s, 1000 + 301000);
  TEST_ASSERT_FALSE(s.havePending);
}

void test_gate_survives_millis_wrap() {
  GateConfig c = cfgFor(TRIG_ALWAYS);
  GateState  s;
  const uint32_t nearMax = 0xFFFFF000u;  // ~4 s before the 49-day rollover

  TEST_ASSERT_TRUE(gateOnScan(c, s, UID_A, 4, 0, nearMax).send);
  gateNoteSent(s, UID_A, 4, 0, nearMax);

  // 2 s later, having wrapped past zero: still inside the cooldown.
  uint32_t after = nearMax + 2000;  // wraps
  TEST_ASSERT_FALSE(gateOnScan(c, s, UID_A, 4, 0, after).send);

  // 31 s later, also wrapped: cooldown is over.
  TEST_ASSERT_TRUE(gateOnScan(c, s, UID_A, 4, 0, nearMax + 31000).send);
}

// ---------------------------------------------------------------------------

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// fleet firmware push — the bytes that go on the wire
// ---------------------------------------------------------------------------

static const char *g_fakeImage = "IMAGEBYTES-0123456789";

static bool fakeReader(void *, size_t offset, void *dst, size_t len) {
  memcpy(dst, g_fakeImage + offset, len);
  return true;
}

void test_fleet_boundary_is_stable_and_marked() {
  TEST_ASSERT_EQUAL_STRING("----u1sbdeadbeef", fleetBoundary(0xDEADBEEF).c_str());
  TEST_ASSERT_EQUAL_STRING("----u1sb00000000", fleetBoundary(0).c_str());
}

void test_fleet_multipart_envelope() {
  std::string h = fleetMultipartHead("BND", "firmware.bin");
  TEST_ASSERT_EQUAL_STRING(
      "--BND\r\n"
      "Content-Disposition: form-data; name=\"firmware\"; filename=\"firmware.bin\"\r\n"
      "Content-Type: application/octet-stream\r\n\r\n",
      h.c_str());
  TEST_ASSERT_EQUAL_STRING("\r\n--BND--\r\n", fleetMultipartTail("BND").c_str());
}

void test_fleet_body_length() {
  TEST_ASSERT_EQUAL_INT(30, (int)fleetBodyLength(10, 15, 5));
}

// The interesting failure is a read that straddles head->image or image->tail,
// so walk the whole body one byte at a time and rebuild it.
void test_fleet_body_read_one_byte_at_a_time() {
  const std::string b = "BND";
  const std::string h = fleetMultipartHead(b, "f.bin");
  const std::string t = fleetMultipartTail(b);
  const size_t      n = strlen(g_fakeImage);

  std::string built;
  char        c;
  for (size_t pos = 0; pos < fleetBodyLength(h.size(), n, t.size()); pos++) {
    TEST_ASSERT_EQUAL_INT(1, (int)fleetBodyRead(pos, 1, &c, h.data(), h.size(), n,
                                                t.data(), t.size(), fakeReader, nullptr));
    built += c;
  }
  TEST_ASSERT_EQUAL_STRING((h + g_fakeImage + t).c_str(), built.c_str());
}

void test_fleet_body_read_large_chunks_match() {
  const std::string b = "BND";
  const std::string h = fleetMultipartHead(b, "f.bin");
  const std::string t = fleetMultipartTail(b);
  const size_t      n = strlen(g_fakeImage);
  const size_t      total = fleetBodyLength(h.size(), n, t.size());

  char   buf[512];
  size_t pos = 0;
  std::string built;
  while (pos < total) {
    size_t got = fleetBodyRead(pos, 7, buf, h.data(), h.size(), n, t.data(), t.size(),
                               fakeReader, nullptr);
    TEST_ASSERT_TRUE(got > 0);
    built.append(buf, got);
    pos += got;
  }
  TEST_ASSERT_EQUAL_STRING((h + g_fakeImage + t).c_str(), built.c_str());
}

void test_fleet_body_read_past_the_end_yields_nothing() {
  const std::string h = "H", t = "T";
  char              buf[8];
  TEST_ASSERT_EQUAL_INT(0, (int)fleetBodyRead(99, 8, buf, h.data(), h.size(), 3,
                                              t.data(), t.size(), fakeReader, nullptr));
}

void test_fleet_body_read_stops_on_a_bad_image_read() {
  const std::string h = "H", t = "T";
  char              buf[64];
  // A reader that always fails must not be mistaken for end-of-body: we should
  // get the head and nothing more, rather than a silently truncated image.
  auto bad = [](void *, size_t, void *, size_t) -> bool { return false; };
  size_t got = fleetBodyRead(0, 64, buf, h.data(), h.size(), 40, t.data(), t.size(),
                             bad, nullptr);
  TEST_ASSERT_EQUAL_INT(1, (int)got);
}


// ---------------------------------------------------------------------------
// Reading the printer's answer to filament_detect/set.
//
// Both backends answer HTTP 200 whatever happens, so the body is the only
// signal. The case that matters is the Bespok3d validator refusing the whole
// request over one unknown key: a lenient check would read that as a success
// and the box would report every rejected send as delivered.
// ---------------------------------------------------------------------------

static U1Reply cls(const char *body, char *msg, size_t cap) {
  return u1ClassifyReply(body, msg, cap);
}

void test_reply_success_wrapped() {
  char m[160];
  TEST_ASSERT_EQUAL(U1_REPLY_OK, cls("{\"result\":{\"state\":\"success\"}}", m, sizeof(m)));
}

void test_reply_success_bare() {
  char m[160];
  TEST_ASSERT_EQUAL(U1_REPLY_OK, cls("{\"state\":\"success\"}", m, sizeof(m)));
}

void test_reply_bespok3d_unsupported_field() {
  char m[160];
  // The exact shape rfid_ntag.py produces for a payload carrying CARD_TYPE.
  const char *body =
      "{\"result\":{\"state\":\"error\",\"message\":\"unsupported fields: CARD_TYPE\"}}";
  TEST_ASSERT_EQUAL(U1_REPLY_BAD_FIELD, cls(body, m, sizeof(m)));
  TEST_ASSERT_TRUE(strstr(m, "CARD_TYPE") != NULL);
}

void test_reply_error_is_not_success() {
  // The regression this guards: "result" appears in the body, so a substring
  // check for it would call this a success and lose the spool silently.
  char m[160];
  const char *body =
      "{\"result\":{\"state\":\"error\",\"message\":\"channel[7] is out of range[0, 3]\"}}";
  TEST_ASSERT_EQUAL(U1_REPLY_ERROR, cls(body, m, sizeof(m)));
  TEST_ASSERT_EQUAL_STRING("channel[7] is out of range[0, 3]", m);
}

void test_reply_error_message_names_a_field() {
  char m[160];
  const char *body =
      "{\"result\":{\"state\":\"error\",\"message\":\"unknown field SKU\"}}";
  TEST_ASSERT_EQUAL(U1_REPLY_BAD_FIELD, cls(body, m, sizeof(m)));
}

void test_reply_bare_result_acknowledges() {
  char m[160];
  TEST_ASSERT_EQUAL(U1_REPLY_OK, cls("{\"result\":\"ok\"}", m, sizeof(m)));
}

void test_reply_truncated_still_caught() {
  // A body cut short mid-JSON must not parse, but the field complaint is still
  // the thing we act on, so the substring pass has to run first.
  char m[160];
  const char *body = "{\"result\":{\"state\":\"error\",\"message\":\"unsupported fields: CARD_T";
  TEST_ASSERT_EQUAL(U1_REPLY_BAD_FIELD, cls(body, m, sizeof(m)));
}

void test_reply_garbage_and_empty() {
  char m[160];
  TEST_ASSERT_EQUAL(U1_REPLY_UNKNOWN, cls("<html>404</html>", m, sizeof(m)));
  TEST_ASSERT_EQUAL(U1_REPLY_UNKNOWN, cls("", m, sizeof(m)));
  TEST_ASSERT_EQUAL(U1_REPLY_UNKNOWN, cls(NULL, m, sizeof(m)));
}

void test_reply_message_is_truncated_not_overrun() {
  char m[8];
  const char *body =
      "{\"result\":{\"state\":\"error\",\"message\":\"a considerably longer reason\"}}";
  TEST_ASSERT_EQUAL(U1_REPLY_ERROR, cls(body, m, sizeof(m)));
  TEST_ASSERT_EQUAL(7u, strlen(m));
}


// ---------------------------------------------------------------------------
// The OTA stall watchdog.
//
// A refused upload cleans up after itself; one whose connection dies does not,
// and otaBusy() would latch on forever with the whole main loop gated behind
// it. The arithmetic is the part worth pinning down: millis() wraps roughly
// every 49.7 days, so now < lastActivity is an ordinary state a few weeks into
// an uptime, and a signed comparison here would make a box that has been up
// that long unrecoverable.
// ---------------------------------------------------------------------------

void test_stall_idle_never_fires() {
  // Not busy: no upload to abandon, however long ago "activity" was.
  TEST_ASSERT_FALSE(otaStalled(false, 1000000, 0, OTA_STALL_MS));
  TEST_ASSERT_FALSE(otaStalled(false, 0, 0, OTA_STALL_MS));
}

void test_stall_live_transfer_survives() {
  // A chunk one millisecond inside the window keeps the upload alive.
  TEST_ASSERT_FALSE(otaStalled(true, 100000, 100000 - (OTA_STALL_MS - 1), OTA_STALL_MS));
  TEST_ASSERT_FALSE(otaStalled(true, 100000, 100000, OTA_STALL_MS));
}

void test_stall_fires_at_the_boundary() {
  TEST_ASSERT_TRUE(otaStalled(true, 100000, 100000 - OTA_STALL_MS, OTA_STALL_MS));
  TEST_ASSERT_TRUE(otaStalled(true, 100000, 0, OTA_STALL_MS));
}

void test_stall_survives_millis_wraparound() {
  // Uptime rolls over mid-upload: last chunk just before the wrap, now just
  // after. That is 26 ms of elapsed time, not a 49-day gap.
  const uint32_t last = 0xFFFFFFF0u;
  TEST_ASSERT_FALSE(otaStalled(true, 10, last, OTA_STALL_MS));
  TEST_ASSERT_FALSE(otaStalled(true, 0xFFFFFFFFu, last, OTA_STALL_MS));
  // And it must still fire once the window really has passed across the wrap.
  TEST_ASSERT_TRUE(otaStalled(true, last + OTA_STALL_MS, last, OTA_STALL_MS));
}

void test_stall_zero_timeout_is_immediate() {
  // Not used in the firmware, but a zero must not mean "never".
  TEST_ASSERT_TRUE(otaStalled(true, 5, 5, 0));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_sha256_vector);
  RUN_TEST(test_hmac_sha256_vector);
  RUN_TEST(test_bambu_key_derivation);
  RUN_TEST(test_openspool_decode);
  RUN_TEST(test_openspool_roundtrip);
  RUN_TEST(test_ndef_rejects_garbage);
  RUN_TEST(test_bambu_decode);
  RUN_TEST(test_bambu_needs_data);
  RUN_TEST(test_qidi_decode);
  RUN_TEST(test_qidi_rejects_unknown_code);
  RUN_TEST(test_main_type_folding);
  RUN_TEST(test_filled_grades_keep_their_suffix);
  RUN_TEST(test_pctg_is_its_own_type);
  RUN_TEST(test_similar_families_do_not_collide);
  RUN_TEST(test_fibre_needs_a_token_not_a_substring);
  RUN_TEST(test_filled_grades_run_hotter);
  RUN_TEST(test_subtype_mapping);
  RUN_TEST(test_normalize_fills_gaps);
  RUN_TEST(test_parse_hex_color);
  RUN_TEST(test_spoolman_json_encoding);
  RUN_TEST(test_uid_list_contains);
  RUN_TEST(test_uid_list_add);
  RUN_TEST(test_uid_list_remove);
  RUN_TEST(test_location_format);
  RUN_TEST(test_comment_trail);
  RUN_TEST(test_gate_on_load_holds_the_scan);
  RUN_TEST(test_gate_on_load_without_a_scan);
  RUN_TEST(test_gate_on_load_ignores_a_stale_scan);
  RUN_TEST(test_gate_load_event_ignored_in_other_modes);
  RUN_TEST(test_gate_always_mode_with_cooldown);
  RUN_TEST(test_gate_armed_mode);
  RUN_TEST(test_gate_arm_times_out);
  RUN_TEST(test_gate_on_insert_uses_the_bound_slot);
  RUN_TEST(test_gate_on_insert_ignores_load_events);
  RUN_TEST(test_gate_manual_mode_never_fires);
  RUN_TEST(test_gate_expires_stale_pending);
  RUN_TEST(test_gate_survives_millis_wrap);
  RUN_TEST(test_fleet_boundary_is_stable_and_marked);
  RUN_TEST(test_fleet_multipart_envelope);
  RUN_TEST(test_fleet_body_length);
  RUN_TEST(test_fleet_body_read_one_byte_at_a_time);
  RUN_TEST(test_fleet_body_read_large_chunks_match);
  RUN_TEST(test_fleet_body_read_past_the_end_yields_nothing);
  RUN_TEST(test_fleet_body_read_stops_on_a_bad_image_read);
  RUN_TEST(test_location_group_token);
  RUN_TEST(test_location_substitution_does_not_recurse);
  RUN_TEST(test_location_all_tokens_empty_is_blank_not_padded);
  RUN_TEST(test_location_group_only_is_shared_across_slots);
  RUN_TEST(test_reply_success_wrapped);
  RUN_TEST(test_reply_success_bare);
  RUN_TEST(test_reply_bespok3d_unsupported_field);
  RUN_TEST(test_reply_error_is_not_success);
  RUN_TEST(test_reply_error_message_names_a_field);
  RUN_TEST(test_reply_bare_result_acknowledges);
  RUN_TEST(test_reply_truncated_still_caught);
  RUN_TEST(test_reply_garbage_and_empty);
  RUN_TEST(test_reply_message_is_truncated_not_overrun);
  RUN_TEST(test_stall_idle_never_fires);
  RUN_TEST(test_stall_live_transfer_survives);
  RUN_TEST(test_stall_fires_at_the_boundary);
  RUN_TEST(test_stall_survives_millis_wraparound);
  RUN_TEST(test_stall_zero_timeout_is_immediate);
  return UNITY_END();
}
