#include "crypto_hkdf.h"

#include <string.h>

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4)
// ---------------------------------------------------------------------------

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static inline uint32_t ror32(uint32_t x, uint32_t n) {
  return (x >> n) | (x << (32 - n));
}

struct Sha256Ctx {
  uint32_t h[8];
  uint64_t bitlen;
  uint8_t  buf[64];
  size_t   buflen;
};

static void sha256_init(Sha256Ctx *c) {
  c->h[0] = 0x6a09e667; c->h[1] = 0xbb67ae85;
  c->h[2] = 0x3c6ef372; c->h[3] = 0xa54ff53a;
  c->h[4] = 0x510e527f; c->h[5] = 0x9b05688c;
  c->h[6] = 0x1f83d9ab; c->h[7] = 0x5be0cd19;
  c->bitlen = 0;
  c->buflen = 0;
}

static void sha256_block(Sha256Ctx *c, const uint8_t *p) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++)
    w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
           ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3];
  uint32_t e = c->h[4], f = c->h[5], g = c->h[6], hh = c->h[7];
  for (int i = 0; i < 64; i++) {
    uint32_t S1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);
    uint32_t ch = (e & f) ^ ((~e) & g);
    uint32_t t1 = hh + S1 + ch + K[i] + w[i];
    uint32_t S0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
    uint32_t mj = (a & b) ^ (a & cc) ^ (b & cc);
    uint32_t t2 = S0 + mj;
    hh = g; g = f; f = e; e = d + t1;
    d = cc; cc = b; b = a; a = t1 + t2;
  }
  c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
  c->h[4] += e; c->h[5] += f; c->h[6] += g;  c->h[7] += hh;
}

static void sha256_update(Sha256Ctx *c, const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    c->buf[c->buflen++] = data[i];
    if (c->buflen == 64) {
      sha256_block(c, c->buf);
      c->bitlen += 512;
      c->buflen = 0;
    }
  }
}

static void sha256_final(Sha256Ctx *c, uint8_t out[32]) {
  uint64_t bits = c->bitlen + (uint64_t)c->buflen * 8;
  size_t i = c->buflen;
  c->buf[i++] = 0x80;
  if (i > 56) {
    while (i < 64) c->buf[i++] = 0;
    sha256_block(c, c->buf);
    i = 0;
  }
  while (i < 56) c->buf[i++] = 0;
  for (int j = 7; j >= 0; j--) c->buf[i++] = (uint8_t)((bits >> (j * 8)) & 0xFF);
  sha256_block(c, c->buf);
  for (int j = 0; j < 8; j++) {
    out[j * 4]     = (uint8_t)(c->h[j] >> 24);
    out[j * 4 + 1] = (uint8_t)(c->h[j] >> 16);
    out[j * 4 + 2] = (uint8_t)(c->h[j] >> 8);
    out[j * 4 + 3] = (uint8_t)(c->h[j]);
  }
}

void sha256(const uint8_t *data, size_t len, uint8_t out[32]) {
  Sha256Ctx c;
  sha256_init(&c);
  sha256_update(&c, data, len);
  sha256_final(&c, out);
}

// ---------------------------------------------------------------------------
// HMAC-SHA256 (RFC 2104)
// ---------------------------------------------------------------------------

void hmac_sha256(const uint8_t *key, size_t keyLen, const uint8_t *msg,
                 size_t msgLen, uint8_t out[32]) {
  uint8_t k[64];
  memset(k, 0, sizeof(k));
  if (keyLen > 64) {
    sha256(key, keyLen, k);
  } else {
    memcpy(k, key, keyLen);
  }

  uint8_t ipad[64], opad[64];
  for (int i = 0; i < 64; i++) {
    ipad[i] = (uint8_t)(k[i] ^ 0x36);
    opad[i] = (uint8_t)(k[i] ^ 0x5C);
  }

  Sha256Ctx c;
  uint8_t inner[32];
  sha256_init(&c);
  sha256_update(&c, ipad, 64);
  sha256_update(&c, msg, msgLen);
  sha256_final(&c, inner);

  sha256_init(&c);
  sha256_update(&c, opad, 64);
  sha256_update(&c, inner, 32);
  sha256_final(&c, out);
}

// ---------------------------------------------------------------------------
// HKDF (RFC 5869)
// ---------------------------------------------------------------------------

void hkdf_sha256(const uint8_t *salt, size_t saltLen, const uint8_t *ikm,
                 size_t ikmLen, const uint8_t *info, size_t infoLen,
                 uint8_t *okm, size_t okmLen) {
  uint8_t zeroSalt[32];
  if (salt == nullptr || saltLen == 0) {
    memset(zeroSalt, 0, sizeof(zeroSalt));
    salt = zeroSalt;
    saltLen = sizeof(zeroSalt);
  }

  // Extract
  uint8_t prk[32];
  hmac_sha256(salt, saltLen, ikm, ikmLen, prk);

  // Expand
  uint8_t t[32];
  size_t  tLen = 0;
  size_t  done = 0;
  uint8_t counter = 1;
  uint8_t block[32 + 256 + 1];

  while (done < okmLen) {
    size_t n = 0;
    if (tLen) { memcpy(block, t, tLen); n += tLen; }
    if (infoLen) {
      size_t cap = sizeof(block) - n - 1;
      size_t ilen = infoLen < cap ? infoLen : cap;
      memcpy(block + n, info, ilen);
      n += ilen;
    }
    block[n++] = counter++;
    hmac_sha256(prk, 32, block, n, t);
    tLen = 32;
    size_t take = (okmLen - done) < 32 ? (okmLen - done) : 32;
    memcpy(okm + done, t, take);
    done += take;
  }
}
