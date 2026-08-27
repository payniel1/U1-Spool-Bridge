#include "spool_data.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

const char *tagSourceName(TagSource s) {
  switch (s) {
    case SRC_OPENSPOOL: return "OpenSpool";
    case SRC_BAMBU:     return "Bambu Lab";
    case SRC_QIDI:      return "QIDI";
    case SRC_CREALITY:  return "Creality";
    case SRC_UNKNOWN:   return "Unknown tag";
    case SRC_MANUAL:    return "Manual";
    case SRC_SPOOLMAN:  return "Spoolman";
    default:            return "None";
  }
}

void SpoolData::clear() { memset(this, 0, sizeof(SpoolData)); }

bool SpoolData::sameTagAs(const SpoolData &o) const {
  return uidLen == o.uidLen && uidLen > 0 && memcmp(uid, o.uid, uidLen) == 0;
}

uint16_t rd_u16le(const uint8_t *p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

float rd_f32le(const uint8_t *p) {
  uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
               ((uint32_t)p[3] << 24);
  float f;
  memcpy(&f, &v, 4);
  return f;
}

// Copy a fixed-width, possibly NUL- or space-padded vendor string.
void copyTrimmed(char *dst, size_t dstLen, const uint8_t *src, size_t srcLen) {
  if (dstLen == 0) return;
  size_t n = 0;
  for (size_t i = 0; i < srcLen && n + 1 < dstLen; i++) {
    if (src[i] == 0x00) break;
    if (src[i] < 0x20 || src[i] > 0x7E) continue;  // drop non-printables
    dst[n++] = (char)src[i];
  }
  while (n > 0 && dst[n - 1] == ' ') n--;  // right-trim
  dst[n] = '\0';
}

bool parseHexColor(const char *s, uint32_t *rgb, uint8_t *alpha) {
  if (!s) return false;
  while (*s == '#' || *s == ' ') s++;
  size_t len = strlen(s);
  if (len != 6 && len != 8) return false;
  uint32_t v = 0;
  for (size_t i = 0; i < len; i++) {
    char c = s[i];
    uint32_t d;
    if (c >= '0' && c <= '9')      d = (uint32_t)(c - '0');
    else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
    else return false;
    v = (v << 4) | d;
  }
  if (len == 8) {  // RRGGBBAA
    if (alpha) *alpha = (uint8_t)(v & 0xFF);
    v >>= 8;
  }
  if (rgb) *rgb = v & 0xFFFFFF;
  return true;
}

void uidToHex(const uint8_t *uid, uint8_t len, char *out, size_t outLen) {
  size_t n = 0;
  for (uint8_t i = 0; i < len && n + 3 < outLen; i++) {
    snprintf(out + n, outLen - n, "%02X", uid[i]);
    n += 2;
  }
  if (outLen) out[n] = '\0';
}

// ---------------------------------------------------------------------------
// Material normalisation
// ---------------------------------------------------------------------------

static bool containsCI(const char *hay, const char *needle) {
  if (!hay || !needle) return false;
  size_t nl = strlen(needle);
  if (nl == 0) return false;
  for (const char *p = hay; *p; p++) {
    size_t i = 0;
    while (i < nl && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
    if (i == nl) return true;
  }
  return false;
}

// The U1 firmware only stores a small set of MAIN_TYPE strings. Anything else
// is rejected or shows up blank in the slicer, so fold aggressively.
// Is this a reinforced grade? Looked for as a token rather than a bare substring,
// so a vendor name that merely happens to contain the letters doesn't promote a
// plain filament to composite.
static bool hasFibreToken(const char *raw, const char *tok) {
  if (!raw || !tok) return false;
  size_t tl = strlen(tok), rl = strlen(raw);
  for (size_t i = 0; i + tl <= rl; i++) {
    bool match = true;
    for (size_t j = 0; j < tl; j++) {
      if (toupper((unsigned char)raw[i + j]) != toupper((unsigned char)tok[j])) {
        match = false;
        break;
      }
    }
    if (!match) continue;
    char before = i ? raw[i - 1] : ' ';
    char after  = (i + tl < rl) ? raw[i + tl] : ' ';
    bool okBefore = !isalnum((unsigned char)before);
    bool okAfter  = !isalnum((unsigned char)after);
    if (okBefore && okAfter) return true;
  }
  return false;
}

// Fold a vendor's free-text material name onto something the printer will show
// sensibly. The base families are matched most-specific-first — PAHT before PA,
// PPA and PPS before PP, PCTG before both PETG and PC — and a carbon- or
// glass-filled grade keeps its suffix rather than being flattened onto the base,
// because the U1 accepts any MAIN_TYPE string and a PA-CF spool is not a PA one.
void normalizeMainType(const char *raw, char *out, size_t outLen) {
  static const struct { const char *needle; const char *name; } BASES[] = {
      {"PAHT", "PAHT"}, {"PPA", "PPA"},  {"PPS", "PPS"},
      {"PCTG", "PCTG"}, {"PETG", "PETG"}, {"PET-", "PET"}, {"PET ", "PET"},
      {"TPU", "TPU"},   {"TPE", "TPU"},
      {"PVA", "PVA"},   {"BVOH", "PVA"},
      {"ASA", "ASA"},   {"ABS", "ABS"},  {"HIPS", "HIPS"},
      {"PC", "PC"},     {"PP", "PP"},
      {"PA12", "PA"},   {"NYLON", "PA"}, {"PA", "PA"},
      {"PLA", "PLA"},
  };

  const char *base = "PLA";
  for (size_t i = 0; i < sizeof(BASES) / sizeof(BASES[0]); i++) {
    if (containsCI(raw, BASES[i].needle)) { base = BASES[i].name; break; }
  }

  const char *fibre = "";
  if (hasFibreToken(raw, "CF") || containsCI(raw, "carbon")) fibre = "-CF";
  else if (hasFibreToken(raw, "GF") || containsCI(raw, "glass") ||
           hasFibreToken(raw, "GF25") || containsCI(raw, "fiberglass")) fibre = "-GF";

  // PAHT is already a reinforced-nylon trade name; PAHT-CF is the usual product.
  snprintf(out, outLen, "%s%s", base, fibre);
}

// Basic, Matte, SnapSpeed, Silk, Support, HF, 95A, 95A HF
const char *mapSubTypeForU1(const char *raw, const char *mainType) {
  bool hf = containsCI(raw, "high speed") || containsCI(raw, "HF") ||
            containsCI(raw, "Rapido") || containsCI(raw, "Aero");

  if (mainType && strcmp(mainType, "TPU") == 0) return hf ? "95A HF" : "95A";
  if (containsCI(raw, "support"))  return "Support";
  if (containsCI(raw, "matte"))    return "Matte";
  if (containsCI(raw, "silk"))     return "Silk";
  if (containsCI(raw, "snapspeed")) return "SnapSpeed";
  if (hf)                          return "HF";
  return "Basic";
}

void defaultTempsFor(const char *m, uint16_t *hotMin, uint16_t *hotMax, uint16_t *bed) {
  uint16_t lo = 200, hi = 230, bd = 55;

  // Compare on the base only, so PLA-CF inherits PLA's numbers rather than
  // falling through to the default.
  char basebuf[16] = {0};
  if (m) {
    size_t i = 0;
    while (m[i] && m[i] != '-' && i + 1 < sizeof(basebuf)) { basebuf[i] = m[i]; i++; }
  }
  const char *b = basebuf;
  const bool filled = m && strchr(m, '-') != nullptr;

  if      (!m)                      { }
  else if (strcmp(b, "PETG") == 0)  { lo = 230; hi = 260; bd = 75; }
  else if (strcmp(b, "PCTG") == 0)  { lo = 230; hi = 260; bd = 75; }
  else if (strcmp(b, "PET")  == 0)  { lo = 250; hi = 280; bd = 80; }
  else if (strcmp(b, "ABS")  == 0)  { lo = 240; hi = 270; bd = 95; }
  else if (strcmp(b, "ASA")  == 0)  { lo = 240; hi = 280; bd = 95; }
  else if (strcmp(b, "TPU")  == 0)  { lo = 210; hi = 240; bd = 40; }
  else if (strcmp(b, "PVA")  == 0)  { lo = 200; hi = 220; bd = 50; }
  else if (strcmp(b, "PC")   == 0)  { lo = 260; hi = 300; bd = 100; }
  else if (strcmp(b, "PAHT") == 0)  { lo = 270; hi = 310; bd = 90; }
  else if (strcmp(b, "PA")   == 0)  { lo = 260; hi = 300; bd = 90; }
  else if (strcmp(b, "PPA")  == 0)  { lo = 290; hi = 320; bd = 100; }
  else if (strcmp(b, "PPS")  == 0)  { lo = 320; hi = 350; bd = 120; }
  else if (strcmp(b, "PP")   == 0)  { lo = 220; hi = 250; bd = 60; }
  else if (strcmp(b, "HIPS") == 0)  { lo = 230; hi = 250; bd = 100; }

  // Filled grades are abrasive and stiffer; every vendor's sheet asks for a
  // little more heat than the unfilled equivalent.
  if (filled) { lo += 10; hi += 10; }

  if (hotMin) *hotMin = lo;
  if (hotMax) *hotMax = hi;
  if (bed)    *bed    = bd;
}

void normalizeForU1(SpoolData &d) {
  if (d.mainType[0] == '\0') {
    const char *src = d.detailedType[0] ? d.detailedType : "PLA";
    normalizeMainType(src, d.mainType, sizeof(d.mainType));
  } else {
    char tmp[16];
    normalizeMainType(d.mainType, tmp, sizeof(tmp));
    snprintf(d.mainType, sizeof(d.mainType), "%s", tmp);
  }

  if (d.subType[0] == '\0') {
    const char *src = d.detailedType[0] ? d.detailedType : d.mainType;
    snprintf(d.subType, sizeof(d.subType), "%s", mapSubTypeForU1(src, d.mainType));
  }

  uint16_t lo, hi, bd;
  defaultTempsFor(d.mainType, &lo, &hi, &bd);
  if (d.hotendMin == 0 || d.hotendMin > 500) d.hotendMin = lo;
  if (d.hotendMax == 0 || d.hotendMax > 500) d.hotendMax = hi;
  if (d.hotendMax < d.hotendMin) d.hotendMax = d.hotendMin;
  if (d.bedTemp == 0 || d.bedTemp > 200)     d.bedTemp = bd;

  if (d.alpha == 0)      d.alpha = 255;
  if (d.diameterUm == 0) d.diameterUm = 1750;
  if (d.weightG == 0)    d.weightG = 1000;
  if (d.vendor[0] == '\0') snprintf(d.vendor, sizeof(d.vendor), "Generic");
}
