// ---------------------------------------------------------------------------
// OpenSpool decoder — NTAG21x / NDEF / JSON.
//
// This is the format the Snapmaker U1 itself speaks (with the paxx12 Extended
// Firmware, or any OpenSpool-aware host). Example payload:
//
// {"protocol":"openspool","version":"1.0","brand":"Snapmaker","type":"PLA",
//  "subtype":"Matte","alpha":"FF","color_hex":"0000FF","min_temp":190,
//  "max_temp":220,"bed_min_temp":50,"bed_max_temp":60,"diameter":175,
//  "weight":1000}
// ---------------------------------------------------------------------------

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

#include "decoders.h"

// NTAG user memory is a TLV stream. We only care about the NDEF Message TLV
// (tag 0x03). 0x00 is padding, 0xFE is the terminator, everything else has a
// length field we can skip over.
bool ndef_find_json(const uint8_t *tlv, size_t len, const uint8_t **payload,
                    size_t *payloadLen) {
  size_t i = 0;
  while (i < len) {
    uint8_t t = tlv[i];
    if (t == 0x00) { i++; continue; }   // NULL TLV / padding
    if (t == 0xFE) return false;        // terminator
    if (i + 1 >= len) return false;

    size_t vlen;
    size_t hdr;
    if (tlv[i + 1] == 0xFF) {           // 3-byte length
      if (i + 3 >= len) return false;
      vlen = ((size_t)tlv[i + 2] << 8) | tlv[i + 3];
      hdr  = 4;
    } else {
      vlen = tlv[i + 1];
      hdr  = 2;
    }
    if (i + hdr + vlen > len) return false;

    if (t == 0x03) {  // NDEF message
      const uint8_t *msg = tlv + i + hdr;
      size_t         rem = vlen;
      size_t         p   = 0;
      while (p < rem) {
        uint8_t hdrByte = msg[p];
        bool    sr      = (hdrByte & 0x10) != 0;  // short record
        bool    il      = (hdrByte & 0x08) != 0;  // ID length present
        size_t  q       = p + 1;
        if (q >= rem) return false;

        uint8_t typeLen = msg[q++];
        size_t  payLen;
        if (sr) {
          if (q >= rem) return false;
          payLen = msg[q++];
        } else {
          if (q + 3 >= rem) return false;
          payLen = ((size_t)msg[q] << 24) | ((size_t)msg[q + 1] << 16) |
                   ((size_t)msg[q + 2] << 8) | msg[q + 3];
          q += 4;
        }
        uint8_t idLen = 0;
        if (il) {
          if (q >= rem) return false;
          idLen = msg[q++];
        }
        const uint8_t *type = msg + q;
        q += typeLen;
        q += idLen;
        if (q + payLen > rem) return false;
        const uint8_t *pay = msg + q;

        bool isJson =
            (typeLen == 16 && memcmp(type, "application/json", 16) == 0) ||
            (payLen > 1 && pay[0] == '{');
        if (isJson) {
          *payload    = pay;
          *payloadLen = payLen;
          return true;
        }
        p = q + payLen;
      }
      return false;
    }
    i += hdr + vlen;
  }
  return false;
}

bool openspool_parse_json(const char *json, size_t len, SpoolData &out) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json, len);
  if (err) return false;
  if (!doc.is<JsonObject>()) return false;

  // A tag that does not say "openspool" is still worth reading if it has the
  // right shape, but we refuse documents with nothing recognisable in them.
  const char *proto = doc["protocol"] | "";
  const char *type  = doc["type"] | doc["material"] | "";
  if (type[0] == '\0' && strcmp(proto, "openspool") != 0) return false;

  out.source = SRC_OPENSPOOL;
  out.valid  = true;

  snprintf(out.vendor, sizeof(out.vendor), "%s",
           (const char *)(doc["brand"] | doc["vendor"] | "Generic"));
  snprintf(out.detailedType, sizeof(out.detailedType), "%s %s", type,
           (const char *)(doc["subtype"] | ""));

  normalizeMainType(type, out.mainType, sizeof(out.mainType));

  const char *sub = doc["subtype"] | "";
  if (sub[0]) {
    snprintf(out.subType, sizeof(out.subType), "%s",
             mapSubTypeForU1(sub, out.mainType));
  }

  const char *colour = doc["color_hex"] | doc["color"] | "";
  uint8_t     a      = 255;
  if (colour[0]) parseHexColor(colour, &out.rgb, &a);
  out.alpha = a;

  // "alpha" may be a hex string ("FF") or a plain number.
  JsonVariant av = doc["alpha"];
  if (!av.isNull()) {
    if (av.is<const char *>()) {
      const char  *as = av.as<const char *>();
      unsigned int v  = 0;  // must be `unsigned int` for %x, not uint32_t
      if (sscanf(as, "%x", &v) == 1) out.alpha = (uint8_t)(v & 0xFF);
    } else {
      out.alpha = (uint8_t)(av.as<int>() & 0xFF);
    }
  }

  out.hotendMin = (uint16_t)(doc["min_temp"] | doc["temp_min"] | 0);
  out.hotendMax = (uint16_t)(doc["max_temp"] | doc["temp_max"] | 0);

  // OpenSpool carries a bed range; the U1 stores a single bed temperature, so
  // take the top of the range (that is what the printer preheats to).
  int bedMax = doc["bed_max_temp"] | 0;
  int bedMin = doc["bed_min_temp"] | 0;
  out.bedTemp = (uint16_t)(bedMax ? bedMax : bedMin);

  out.weightG = (uint16_t)(doc["weight"] | 0);

  // "diameter" is 175 in the U1 flavour, 1.75 in some other writers.
  JsonVariant dia = doc["diameter"];
  if (!dia.isNull()) {
    float f = dia.as<float>();
    out.diameterUm = (f < 10.0f) ? (uint16_t)(f * 1000.0f) : (uint16_t)(f * 10.0f);
  }

  const char *sku = doc["sku"] | "";
  if (sku[0]) snprintf(out.skuStr, sizeof(out.skuStr), "%s", sku);
  out.sku = (uint32_t)(doc["sku_id"] | 0);

  return true;
}

bool openspool_decode(const uint8_t *tlv, size_t len, SpoolData &out) {
  const uint8_t *pay = nullptr;
  size_t         payLen = 0;
  if (!ndef_find_json(tlv, len, &pay, &payLen)) return false;
  return openspool_parse_json((const char *)pay, payLen, out);
}

size_t openspool_build_json(const SpoolData &d, char *out, size_t outLen) {
  JsonDocument doc;
  doc["protocol"]     = "openspool";
  doc["version"]      = "1.0";
  doc["brand"]        = d.vendor;
  doc["type"]         = d.mainType;
  doc["subtype"]      = d.subType;
  char alpha[4];
  snprintf(alpha, sizeof(alpha), "%02X", d.alpha);
  doc["alpha"]        = alpha;
  char col[8];
  snprintf(col, sizeof(col), "%06lX", (unsigned long)(d.rgb & 0xFFFFFF));
  doc["color_hex"]    = col;
  doc["min_temp"]     = d.hotendMin;
  doc["max_temp"]     = d.hotendMax;
  doc["bed_min_temp"] = d.bedTemp > 10 ? d.bedTemp - 10 : d.bedTemp;
  doc["bed_max_temp"] = d.bedTemp;
  doc["diameter"]     = d.diameterUm / 10;  // 1750 um -> 175
  doc["weight"]       = d.weightG;
  return serializeJson(doc, out, outLen);
}
