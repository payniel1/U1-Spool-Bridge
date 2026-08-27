#include "fleet_wire.h"

#include <string.h>

std::string fleetBoundary(uint32_t seed) {
  static const char *hex = "0123456789abcdef";
  std::string b = "----u1sb";
  for (int i = 7; i >= 0; i--) b += hex[(seed >> (i * 4)) & 0xf];
  return b;
}

std::string fleetMultipartHead(const std::string &boundary, const std::string &filename) {
  return "--" + boundary + "\r\n" +
         "Content-Disposition: form-data; name=\"firmware\"; filename=\"" + filename +
         "\"\r\n" + "Content-Type: application/octet-stream\r\n\r\n";
}

std::string fleetMultipartTail(const std::string &boundary) {
  return "\r\n--" + boundary + "--\r\n";
}

size_t fleetBodyLength(size_t headLen, size_t imageLen, size_t tailLen) {
  return headLen + imageLen + tailLen;
}

size_t fleetBodyRead(size_t pos, size_t n, void *dst,
                     const char *head, size_t headLen,
                     size_t imageLen,
                     const char *tail, size_t tailLen,
                     FleetImageReader readImage, void *ctx) {
  uint8_t     *out   = (uint8_t *)dst;
  const size_t total = fleetBodyLength(headLen, imageLen, tailLen);
  size_t       done  = 0;

  while (done < n && pos < total) {
    size_t take;
    if (pos < headLen) {
      take = headLen - pos;
      if (take > n - done) take = n - done;
      memcpy(out + done, head + pos, take);
    } else if (pos < headLen + imageLen) {
      const size_t off = pos - headLen;
      take = imageLen - off;
      if (take > n - done) take = n - done;
      if (!readImage || !readImage(ctx, off, out + done, take)) break;
    } else {
      const size_t off = pos - headLen - imageLen;
      take = tailLen - off;
      if (take > n - done) take = n - done;
      memcpy(out + done, tail + off, take);
    }
    if (take == 0) break;
    pos += take;
    done += take;
  }
  return done;
}
