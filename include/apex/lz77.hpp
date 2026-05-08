#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace apex {

struct Lz77Token final {
  enum class Kind : uint8_t { Literal = 0, Match = 1 };
  Kind kind = Kind::Literal;
  uint8_t lit = 0;
  uint16_t length = 0;
  uint16_t distance = 0;

  static Lz77Token literal(uint8_t b) {
    Lz77Token t;
    t.kind = Kind::Literal;
    t.lit = b;
    return t;
  }

  static Lz77Token match(uint16_t len, uint16_t dist) {
    Lz77Token t;
    t.kind = Kind::Match;
    t.length = len;
    t.distance = dist;
    return t;
  }
};

struct Lz77Options final {
  uint32_t windowSize = 32u * 1024u;
  uint32_t minMatch = 3;
  uint32_t maxMatch = 258;
  uint32_t maxChain = 256;
  uint32_t niceLength = 32;
};

std::vector<Lz77Token> lz77Encode(std::span<const uint8_t> input, const Lz77Options& opt = {});

} // namespace apex

