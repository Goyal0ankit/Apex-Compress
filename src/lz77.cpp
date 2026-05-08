#include "apex/lz77.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace apex {

namespace {

constexpr uint32_t kHashBits = 15;
constexpr uint32_t kHashSize = 1u << kHashBits;
constexpr int32_t kNil = -1;

inline uint32_t hash3(uint8_t a, uint8_t b, uint8_t c) {
  uint32_t x = static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 8) |
               (static_cast<uint32_t>(c) << 16);
  x *= 0x1e35a7bdU;
  return (x >> (32 - kHashBits)) & (kHashSize - 1u);
}

inline uint32_t matchLength(std::span<const uint8_t> in, size_t i, size_t j, uint32_t maxLen) {
  uint32_t n = 0;
  const size_t end = std::min(in.size(), i + static_cast<size_t>(maxLen));
  while (i + n < end && in[i + n] == in[j + n]) ++n;
  return n;
}

} // namespace

std::vector<Lz77Token> lz77Encode(std::span<const uint8_t> input, const Lz77Options& opt) {
  if (opt.windowSize == 0) throw std::invalid_argument("windowSize=0");
  if (opt.windowSize > 32u * 1024u) throw std::invalid_argument("windowSize > 32KiB not supported");
  if (opt.minMatch < 3) throw std::invalid_argument("minMatch < 3");
  if (opt.maxMatch < opt.minMatch) throw std::invalid_argument("maxMatch < minMatch");
  if (opt.maxMatch > 258) throw std::invalid_argument("maxMatch > 258 not supported");

  std::array<int32_t, kHashSize> head{};
  head.fill(kNil);

  const uint32_t wMask = opt.windowSize - 1u;
  if ((opt.windowSize & wMask) != 0u) {
    throw std::invalid_argument("windowSize must be power-of-two (e.g., 32768)");
  }

  std::vector<int32_t> prev(opt.windowSize, kNil);

  std::vector<Lz77Token> out;
  out.reserve(input.size());

  auto insertPos = [&](size_t pos) {
    if (pos + 2 >= input.size()) return;
    const uint32_t h = hash3(input[pos], input[pos + 1], input[pos + 2]);
    const uint32_t wpos = static_cast<uint32_t>(pos) & wMask;
    prev[wpos] = head[h];
    head[h] = static_cast<int32_t>(pos);
  };

  size_t i = 0;
  while (i < input.size()) {
    uint32_t bestLen = 0;
    uint32_t bestDist = 0;

    if (i + opt.minMatch <= input.size() && i + 2 < input.size()) {
      const uint32_t h = hash3(input[i], input[i + 1], input[i + 2]);
      int32_t cur = head[h];
      uint32_t chain = 0;

      const size_t windowStart = (i > opt.windowSize) ? (i - opt.windowSize) : 0;

      while (cur != kNil && chain++ < opt.maxChain) {
        const size_t j = static_cast<size_t>(cur);
        if (j < windowStart) break;
        const uint32_t dist = static_cast<uint32_t>(i - j);
        if (dist == 0 || dist > opt.windowSize) {
          cur = prev[static_cast<uint32_t>(cur) & wMask];
          continue;
        }

        const uint32_t maxLen =
            std::min<uint32_t>(opt.maxMatch, static_cast<uint32_t>(input.size() - i));

        if (bestLen < opt.niceLength) {
          const uint32_t len = matchLength(input, i, j, maxLen);
          if (len > bestLen) {
            bestLen = len;
            bestDist = dist;
            if (bestLen >= opt.niceLength) break;
          }
        } else {
          break;
        }

        cur = prev[static_cast<uint32_t>(cur) & wMask];
      }
    }

    if (bestLen >= opt.minMatch) {
      out.push_back(Lz77Token::match(static_cast<uint16_t>(bestLen),
                                     static_cast<uint16_t>(bestDist)));

      insertPos(i);
      for (uint32_t k = 1; k < bestLen; ++k) insertPos(i + k);
      i += bestLen;
    } else {
      out.push_back(Lz77Token::literal(input[i]));
      insertPos(i);
      ++i;
    }
  }

  return out;
}

} // namespace apex

