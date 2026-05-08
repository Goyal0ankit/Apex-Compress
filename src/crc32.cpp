#include "apex/crc32.hpp"

#include <array>

namespace apex {

namespace {
constexpr uint32_t kPoly = 0xEDB88320u;

inline uint32_t tableEntry(uint32_t i) {
  uint32_t c = i;
  for (int k = 0; k < 8; ++k) c = (c & 1u) ? (kPoly ^ (c >> 1)) : (c >> 1);
  return c;
}

inline const uint32_t* table() {
  // Thread-safe initialization (C++11+ guarantees this for function-local statics).
  static const auto t = [] {
    std::array<uint32_t, 256> tmp{};
    for (uint32_t i = 0; i < 256; ++i) tmp[i] = tableEntry(i);
    return tmp;
  }();
  return t.data();
}
} // namespace

uint32_t crc32(std::span<const uint8_t> data, uint32_t seed) {
  const uint32_t* t = table();
  uint32_t c = seed;
  for (uint8_t b : data) c = t[(c ^ b) & 0xFFu] ^ (c >> 8);
  return c;
}

} // namespace apex

