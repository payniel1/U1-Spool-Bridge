// ---------------------------------------------------------------------------
// fleet_wire.h — the bytes a box puts on the wire when it pushes firmware to
// another box.
//
// Deliberately free of Arduino and ESP-IDF types, for the same reason the tag
// decoders are: so the exact body that goes out can be generated and checked on
// a host machine. The composing and chunking live here rather than in the
// Stream subclass so the test exercises the shipped code path instead of a
// second implementation of it that can drift.
//
// Why a box does this at all, rather than the browser: a browser talking to a
// box it was not served from makes a cross-origin request, and cross-origin is
// enforced by the RECEIVING box. Boxes running firmware older than 1.12.0 send
// no CORS headers and have no OPTIONS route, so the preflight is redirected by
// their catch-all handler and the upload is refused before a byte of it is
// sent — and those old boxes are exactly the ones an update is for. A box has
// no such rule. It just opens a socket.
// ---------------------------------------------------------------------------
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>

// Reads `len` bytes of the firmware image at `offset`. Returns false on error.
// On the board this reads the OTA partition; the host test reads a file.
typedef bool (*FleetImageReader)(void *ctx, size_t offset, void *dst, size_t len);

// A boundary token that cannot appear in an ESP32 image by accident: the marker
// is ASCII and images are mostly not, but the receiver only needs it to be
// unique within this body, and derived from a seed it varies per request.
std::string fleetBoundary(uint32_t seed);

std::string fleetMultipartHead(const std::string &boundary, const std::string &filename);
std::string fleetMultipartTail(const std::string &boundary);

// Total Content-Length of head + image + tail.
size_t fleetBodyLength(size_t headLen, size_t imageLen, size_t tailLen);

// Copy up to `n` bytes of the composed body, starting at absolute offset `pos`.
// Returns how many were produced; short only at the end or on a read error.
size_t fleetBodyRead(size_t pos, size_t n, void *dst,
                     const char *head, size_t headLen,
                     size_t imageLen,
                     const char *tail, size_t tailLen,
                     FleetImageReader readImage, void *ctx);
