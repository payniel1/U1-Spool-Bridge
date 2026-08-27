// ---------------------------------------------------------------------------
// QIDI decoder — MIFARE Classic 1K (FM11RF08S).
//
// QIDI's format is tiny: sector 1 / block 0 (absolute block 4) holds three
// bytes — material code, colour code, manufacturer code. Everything else
// (temperatures, weight) is looked up from the code, not stored on the tag.
// Tables from https://wiki.qidi3d.com/en/QIDIBOX/RFID
// ---------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>

#include "decoders.h"

struct QidiMaterial {
  uint8_t     code;
  const char *name;
};

static const QidiMaterial QIDI_MATERIALS[] = {
    {1, "PLA"},           {2, "PLA Matte"},     {3, "PLA Metal"},
    {4, "PLA Silk"},      {5, "PLA-CF"},        {6, "PLA-Wood"},
    {7, "PLA Basic"},     {8, "PLA Matte Basic"},
    {11, "ABS"},          {12, "ABS-GF"},       {13, "ABS-Metal"},
    {14, "ABS-Odorless"}, {18, "ASA"},          {19, "ASA-AERO"},
    {24, "UltraPA"},      {25, "PA-CF"},        {26, "UltraPA-CF25"},
    {27, "PA12-CF"},      {30, "PAHT-CF"},      {31, "PAHT-GF"},
    {32, "Support For PAHT"}, {33, "Support For PET/PA"},
    {34, "PC/ABS-FR"},    {37, "PET-CF"},       {38, "PET-GF"},
    {39, "PETG Basic"},   {40, "PETG Tough"},   {41, "PETG Rapido"},
    {42, "PETG-CF"},      {43, "PETG-GF"},      {44, "PPS-CF"},
    {45, "PETG Translucent"}, {47, "PVA"},      {49, "TPU-Aero"},
    {50, "TPU"},
};

// Codes 1..24, RGB888.
static const uint32_t QIDI_COLORS[24] = {
    0xFAFAFA, 0x060606, 0xD9E3ED, 0x5CF30F, 0x63E492, 0x2850FF,
    0xFE98FE, 0xDFD628, 0x228332, 0x99DEFF, 0x1714B0, 0xCEC0FE,
    0xCADE4B, 0x1353AB, 0x5EA9FD, 0xA878FF, 0xFE717A, 0xFF362D,
    0xE2DFCD, 0x898F9B, 0x6E3812, 0xCAC59F, 0xF28636, 0xB87F2B,
};

const char *qidi_material_name(uint8_t code) {
  for (size_t i = 0; i < sizeof(QIDI_MATERIALS) / sizeof(QIDI_MATERIALS[0]); i++) {
    if (QIDI_MATERIALS[i].code == code) return QIDI_MATERIALS[i].name;
  }
  return nullptr;
}

bool qidi_color_rgb(uint8_t code, uint32_t *rgb) {
  if (code < 1 || code > 24) return false;
  if (rgb) *rgb = QIDI_COLORS[code - 1];
  return true;
}

bool qidi_decode(const uint8_t *block4, SpoolData &out) {
  uint8_t mat = block4[0];
  uint8_t col = block4[1];
  uint8_t mfr = block4[2];

  const char *name = qidi_material_name(mat);
  if (!name) return false;  // not a QIDI record (or a code we don't know)

  out.source = SRC_QIDI;
  out.valid  = true;
  snprintf(out.vendor, sizeof(out.vendor), "%s", mfr == 1 ? "QIDI" : "Generic");
  snprintf(out.detailedType, sizeof(out.detailedType), "%s", name);

  normalizeMainType(name, out.mainType, sizeof(out.mainType));
  snprintf(out.subType, sizeof(out.subType), "%s",
           mapSubTypeForU1(name, out.mainType));

  uint32_t rgb;
  if (qidi_color_rgb(col, &rgb)) out.rgb = rgb;
  out.alpha = 255;

  // The tag carries no temperatures or weight — normalizeForU1() fills in
  // material defaults, and the user can override them in the web UI.
  out.sku = mat;
  return true;
}
