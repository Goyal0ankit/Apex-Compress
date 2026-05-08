#pragma once

#include <cstdint>

namespace apex::deflate {

struct LenCode final {
  uint16_t sym = 0;     // 257..285
  uint16_t base = 0;    // base length
  uint8_t extra = 0;    // extra bits
  uint16_t extraVal = 0;
};

struct DistCode final {
  uint16_t sym = 0;     // 0..29
  uint16_t base = 0;    // base distance
  uint8_t extra = 0;    // extra bits
  uint16_t extraVal = 0;
};

LenCode encodeLength(uint16_t length);        // length 3..258
DistCode encodeDistance(uint16_t distance);   // distance 1..32768

uint16_t decodeLength(uint16_t sym, uint16_t extraVal);
uint16_t decodeDistance(uint16_t sym, uint16_t extraVal);

} // namespace apex::deflate

