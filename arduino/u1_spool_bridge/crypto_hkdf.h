// Minimal, dependency-free SHA-256 / HMAC-SHA256 / HKDF (RFC 5869).
// Self-contained so the Bambu key derivation can be unit-tested on a host
// without pulling in mbedTLS.
#pragma once

#include <stddef.h>
#include <stdint.h>

void sha256(const uint8_t *data, size_t len, uint8_t out[32]);

void hmac_sha256(const uint8_t *key, size_t keyLen, const uint8_t *msg,
                 size_t msgLen, uint8_t out[32]);

// Full extract-then-expand HKDF.
void hkdf_sha256(const uint8_t *salt, size_t saltLen, const uint8_t *ikm,
                 size_t ikmLen, const uint8_t *info, size_t infoLen,
                 uint8_t *okm, size_t okmLen);
