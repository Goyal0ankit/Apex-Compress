#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace apex {

struct HuffmanCode final {
  uint32_t bits_lsb = 0;
  uint8_t bitlen = 0;
};

using FreqTable = std::array<uint32_t, 256>;
using CodeTable = std::array<HuffmanCode, 256>;
using LengthTable = std::array<uint8_t, 256>;

FreqTable countFrequencies(std::span<const uint8_t> data);

LengthTable buildHuffmanCodeLengths(const FreqTable& freqs, uint8_t maxBits = 15);

CodeTable buildCanonicalCodesFromLengths(const LengthTable& lengths);

uint32_t reverseBits(uint32_t v, uint32_t bitlen);

std::vector<uint8_t> buildHuffmanCodeLengths(std::span<const uint32_t> freqs, uint8_t maxBits = 15);
std::vector<HuffmanCode> buildCanonicalCodesFromLengths(std::span<const uint8_t> lengths);

class BitReader;

class HuffmanDecoder final {
public:
  explicit HuffmanDecoder(std::span<const uint8_t> lengths);
  uint32_t decodeSymbol(BitReader& br) const;

private:
  struct Entry final {
    uint16_t sym = 0;
    uint8_t len = 0;
  };

  // Fast LSB-first decode table: index by `peekBits(tableBits_)`.
  // Size is <= 2^15 for deflate-like alphabets, so it's compact and branch-free.
  uint8_t tableBits_ = 0;
  std::vector<Entry> table_;
  std::vector<uint32_t> firstCode_;
  std::vector<uint32_t> firstSym_;
  std::vector<uint32_t> blCount_;
  std::vector<uint16_t> symsByLen_;
  uint8_t maxLen_ = 0;
};

} // namespace apex
