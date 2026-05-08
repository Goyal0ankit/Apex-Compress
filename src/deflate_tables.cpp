#include "apex/deflate_tables.hpp"

#include <array>
#include <stdexcept>

namespace apex::deflate {

namespace {

constexpr std::array<uint16_t, 29> kLenBase = {
    3,  4,  5,  6,  7,  8,  9,  10, 11, 13, 15, 17, 19, 23, 27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258,
};

constexpr std::array<uint8_t, 29> kLenExtra = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
};

constexpr std::array<uint16_t, 30> kDistBase = {
    1,     2,     3,     4,     5,     7,     9,     13,    17,    25,
    33,    49,    65,    97,    129,   193,   257,   385,   513,   769,
    1025,  1537,  2049,  3073,  4097,  6145,  8193,  12289, 16385, 24577,
};

constexpr std::array<uint8_t, 30> kDistExtra = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3,
    4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
    9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
};

} // namespace

LenCode encodeLength(uint16_t length) {
  if (length < 3 || length > 258) throw std::invalid_argument("length out of range");
  if (length == 258) return LenCode{285, 258, 0, 0};
  for (uint16_t i = 0; i < 28; ++i) {
    const uint16_t base = kLenBase[i];
    const uint16_t next = kLenBase[i + 1];
    if (length >= base && length < next) {
      const uint8_t eb = kLenExtra[i];
      const uint16_t extraVal = static_cast<uint16_t>(length - base);
      return LenCode{static_cast<uint16_t>(257 + i), base, eb, extraVal};
    }
  }
  throw std::runtime_error("encodeLength internal");
}

DistCode encodeDistance(uint16_t distance) {
  if (distance < 1 || distance > 32768) throw std::invalid_argument("distance out of range");
  for (uint16_t i = 0; i < 29; ++i) {
    const uint16_t base = kDistBase[i];
    const uint16_t next = kDistBase[i + 1];
    if (distance >= base && distance < next) {
      const uint8_t eb = kDistExtra[i];
      const uint16_t extraVal = static_cast<uint16_t>(distance - base);
      return DistCode{i, base, eb, extraVal};
    }
  }
  const uint16_t i = 29;
  const uint16_t base = kDistBase[i];
  const uint8_t eb = kDistExtra[i];
  const uint16_t extraVal = static_cast<uint16_t>(distance - base);
  return DistCode{i, base, eb, extraVal};
}

uint16_t decodeLength(uint16_t sym, uint16_t extraVal) {
  if (sym == 285) return 258;
  if (sym < 257 || sym > 284) throw std::invalid_argument("length sym out of range");
  const uint16_t idx = static_cast<uint16_t>(sym - 257);
  return static_cast<uint16_t>(kLenBase[idx] + extraVal);
}

uint16_t decodeDistance(uint16_t sym, uint16_t extraVal) {
  if (sym > 29) throw std::invalid_argument("dist sym out of range");
  return static_cast<uint16_t>(kDistBase[sym] + extraVal);
}

} // namespace apex::deflate

