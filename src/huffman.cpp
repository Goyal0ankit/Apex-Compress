#include "apex/huffman.hpp"

#include "apex/bit_io.hpp"

#include <algorithm>
#include <cstdint>
#include <queue>
#include <stdexcept>
#include <utility>
#include <vector>

namespace apex {

FreqTable countFrequencies(std::span<const uint8_t> data) {
  FreqTable f{};
  for (uint8_t b : data) ++f[b];
  return f;
}

namespace {

struct GNode final {
  uint32_t freq = 0;
  int32_t symbol = -1;
  int32_t left = -1;
  int32_t right = -1;
  uint32_t minSymbol = std::numeric_limits<uint32_t>::max();
};

struct GHeapItem final {
  uint32_t freq;
  uint32_t minSymbol;
  uint32_t id;
  int32_t nodeIndex;
};

struct GHeapLess {
  bool operator()(const GHeapItem& a, const GHeapItem& b) const {
    if (a.freq != b.freq) return a.freq > b.freq;
    if (a.minSymbol != b.minSymbol) return a.minSymbol > b.minSymbol;
    return a.id > b.id;
  }
};

void assignLengthsDfsGeneric(const std::vector<GNode>& nodes, int32_t idx, uint32_t depth,
                             std::span<uint8_t> out) {
  const GNode& n = nodes[static_cast<size_t>(idx)];
  if (n.symbol >= 0) {
    out[static_cast<size_t>(n.symbol)] = static_cast<uint8_t>(depth);
    return;
  }
  assignLengthsDfsGeneric(nodes, n.left, depth + 1, out);
  assignLengthsDfsGeneric(nodes, n.right, depth + 1, out);
}

} // namespace

std::vector<uint8_t> buildHuffmanCodeLengths(std::span<const uint32_t> freqs, uint8_t maxBits) {
  if (maxBits == 0 || maxBits > 31) throw std::invalid_argument("maxBits out of range");
  if (freqs.empty()) return {};

  std::vector<GNode> nodes;
  nodes.reserve(freqs.size() * 2u);

  std::priority_queue<GHeapItem, std::vector<GHeapItem>, GHeapLess> pq;
  uint32_t nextId = 0;

  for (uint32_t s = 0; s < freqs.size(); ++s) {
    const uint32_t f = freqs[s];
    if (f == 0) continue;
    GNode n;
    n.freq = f;
    n.symbol = static_cast<int32_t>(s);
    n.minSymbol = s;
    const int32_t idx = static_cast<int32_t>(nodes.size());
    nodes.push_back(n);
    pq.push(GHeapItem{f, n.minSymbol, nextId++, idx});
  }

  std::vector<uint8_t> lengths(freqs.size(), 0);
  if (pq.empty()) return lengths;

  if (pq.size() == 1) {
    const auto single = pq.top();
    lengths[static_cast<size_t>(nodes[static_cast<size_t>(single.nodeIndex)].symbol)] = 1;
    return lengths;
  }

  while (pq.size() > 1) {
    const GHeapItem a = pq.top();
    pq.pop();
    const GHeapItem b = pq.top();
    pq.pop();

    GNode parent;
    parent.freq = a.freq + b.freq;
    parent.left = a.nodeIndex;
    parent.right = b.nodeIndex;
    parent.minSymbol = std::min(a.minSymbol, b.minSymbol);

    const int32_t pidx = static_cast<int32_t>(nodes.size());
    nodes.push_back(parent);
    pq.push(GHeapItem{parent.freq, parent.minSymbol, nextId++, pidx});
  }

  const int32_t root = pq.top().nodeIndex;
  assignLengthsDfsGeneric(nodes, root, 0, std::span<uint8_t>(lengths.data(), lengths.size()));

  uint8_t maxLen = 0;
  for (uint8_t l : lengths) maxLen = std::max(maxLen, l);
  if (maxLen > maxBits) {
    throw std::runtime_error("Huffman code lengths exceed maxBits (length limiting not implemented yet)");
  }

  return lengths;
}

LengthTable buildHuffmanCodeLengths(const FreqTable& freqs, uint8_t maxBits) {
  std::array<uint32_t, 256> tmp = freqs;
  const auto v = buildHuffmanCodeLengths(std::span<const uint32_t>(tmp.data(), tmp.size()), maxBits);
  LengthTable out{};
  for (size_t i = 0; i < 256; ++i) out[i] = v[i];
  return out;
}

uint32_t reverseBits(uint32_t v, uint32_t bitlen) {
  uint32_t r = 0;
  for (uint32_t i = 0; i < bitlen; ++i) {
    r = (r << 1) | (v & 1u);
    v >>= 1u;
  }
  return r;
}

CodeTable buildCanonicalCodesFromLengths(const LengthTable& lengths) {
  struct SymLen {
    uint16_t sym;
    uint8_t len;
  };

  std::vector<SymLen> syms;
  syms.reserve(256);
  uint8_t maxLen = 0;
  for (uint16_t s = 0; s < 256; ++s) {
    const uint8_t l = lengths[s];
    if (l == 0) continue;
    syms.push_back(SymLen{s, l});
    maxLen = std::max(maxLen, l);
  }

  CodeTable out{};
  if (syms.empty()) return out;

  std::sort(syms.begin(), syms.end(), [](const SymLen& a, const SymLen& b) {
    if (a.len != b.len) return a.len < b.len;
    return a.sym < b.sym;
  });

  std::vector<uint32_t> bl_count(static_cast<size_t>(maxLen) + 1, 0);
  for (const auto& sl : syms) ++bl_count[sl.len];

  std::vector<uint32_t> next_code(static_cast<size_t>(maxLen) + 1, 0);
  uint32_t code = 0;
  for (uint32_t bits = 1; bits <= maxLen; ++bits) {
    code = (code + bl_count[bits - 1]) << 1;
    next_code[bits] = code;
  }

  for (const auto& sl : syms) {
    const uint32_t c = next_code[sl.len]++;
    out[sl.sym].bitlen = sl.len;
    out[sl.sym].bits_lsb = reverseBits(c, sl.len);
  }

  return out;
}

std::vector<HuffmanCode> buildCanonicalCodesFromLengths(std::span<const uint8_t> lengths) {
  struct SymLen {
    uint32_t sym;
    uint8_t len;
  };

  std::vector<SymLen> syms;
  syms.reserve(lengths.size());
  uint8_t maxLen = 0;
  for (uint32_t s = 0; s < lengths.size(); ++s) {
    const uint8_t l = lengths[s];
    if (l == 0) continue;
    syms.push_back(SymLen{s, l});
    maxLen = std::max(maxLen, l);
  }

  std::vector<HuffmanCode> out(lengths.size());
  if (syms.empty()) return out;

  std::sort(syms.begin(), syms.end(), [](const SymLen& a, const SymLen& b) {
    if (a.len != b.len) return a.len < b.len;
    return a.sym < b.sym;
  });

  std::vector<uint32_t> bl_count(static_cast<size_t>(maxLen) + 1, 0);
  for (const auto& sl : syms) ++bl_count[sl.len];

  std::vector<uint32_t> next_code(static_cast<size_t>(maxLen) + 1, 0);
  uint32_t code = 0;
  for (uint32_t bits = 1; bits <= maxLen; ++bits) {
    code = (code + bl_count[bits - 1]) << 1;
    next_code[bits] = code;
  }

  for (const auto& sl : syms) {
    const uint32_t c = next_code[sl.len]++;
    out[sl.sym].bitlen = sl.len;
    out[sl.sym].bits_lsb = reverseBits(c, sl.len);
  }

  return out;
}

HuffmanDecoder::HuffmanDecoder(std::span<const uint8_t> lengths) {
  if (lengths.empty()) throw std::invalid_argument("HuffmanDecoder: empty lengths");

  maxLen_ = 0;
  for (uint8_t l : lengths) maxLen_ = std::max(maxLen_, l);
  if (maxLen_ == 0) throw std::invalid_argument("HuffmanDecoder: all zero lengths");

  blCount_.assign(static_cast<size_t>(maxLen_) + 1, 0);
  for (uint8_t l : lengths) {
    if (l > 0) ++blCount_[l];
  }

  firstCode_.assign(static_cast<size_t>(maxLen_) + 1, 0);
  firstSym_.assign(static_cast<size_t>(maxLen_) + 1, 0);

  uint32_t code = 0;
  uint32_t sym = 0;
  for (uint32_t bits = 1; bits <= maxLen_; ++bits) {
    code = (code + blCount_[bits - 1]) << 1;
    firstCode_[bits] = code;
    firstSym_[bits] = sym;
    sym += blCount_[bits];
  }

  symsByLen_.resize(sym);
  std::vector<uint32_t> nextSym(static_cast<size_t>(maxLen_) + 1, 0);
  for (uint32_t bits = 1; bits <= maxLen_; ++bits) nextSym[bits] = firstSym_[bits];
  for (uint32_t s = 0; s < lengths.size(); ++s) {
    const uint8_t l = lengths[s];
    if (l == 0) continue;
    symsByLen_[nextSym[l]++] = static_cast<uint16_t>(s);
  }

  // Build a full decode table in the same LSB-first bit order that BitReader
  // provides. This avoids the subtle MSB/LSB mismatch that can happen with
  // "textbook" canonical decode loops.
  tableBits_ = maxLen_;
  table_.assign(1u << tableBits_, Entry{0, 0});
  const auto codes = buildCanonicalCodesFromLengths(lengths);
  for (uint32_t s = 0; s < codes.size(); ++s) {
    const auto hc = codes[s];
    if (hc.bitlen == 0) continue;
    const uint32_t fill = 1u << (tableBits_ - hc.bitlen);
    const uint32_t base = hc.bits_lsb;
    for (uint32_t k = 0; k < fill; ++k) {
      // `peekBits(tableBits_)` returns the next bits in LSB-first order, i.e.
      // the code occupies the *lowest* `bitlen` bits. The remaining bits can
      // be anything, so we fill all continuations.
      table_[base | (k << hc.bitlen)] = Entry{static_cast<uint16_t>(s), hc.bitlen};
    }
  }
}

uint32_t HuffmanDecoder::decodeSymbol(BitReader& br) const {
  const uint32_t peek = br.peekBits(tableBits_);
  const Entry e = table_[peek];
  if (e.len == 0) throw std::runtime_error("HuffmanDecoder: invalid code");
  br.dropBits(e.len);
  return e.sym;
}

} // namespace apex
