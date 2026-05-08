#include "apex/apxc.hpp"

#include "apex/bit_io.hpp"
#include "apex/crc32.hpp"
#include "apex/deflate_tables.hpp"
#include "apex/huffman.hpp"
#include "apex/lz77.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <fstream>
#include <future>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace apex {

namespace {

constexpr std::array<uint8_t, 4> kMagic = {'A', 'P', 'X', 'C'};
constexpr uint16_t kVersion = 1;

// ---- On-disk IO helpers (little-endian) ----
//
// The original implementation wrote C++ POD structs directly, which bakes in
// padding and endianness. For a stable file format, we serialize fields
// explicitly as little-endian.
void writeU16LE(std::ostream& os, uint16_t v) {
  const uint8_t b[2] = {static_cast<uint8_t>(v & 0xFFu), static_cast<uint8_t>((v >> 8) & 0xFFu)};
  os.write(reinterpret_cast<const char*>(b), 2);
  if (!os) throw std::runtime_error("write failed");
}
void writeU32LE(std::ostream& os, uint32_t v) {
  const uint8_t b[4] = {static_cast<uint8_t>(v & 0xFFu), static_cast<uint8_t>((v >> 8) & 0xFFu),
                        static_cast<uint8_t>((v >> 16) & 0xFFu),
                        static_cast<uint8_t>((v >> 24) & 0xFFu)};
  os.write(reinterpret_cast<const char*>(b), 4);
  if (!os) throw std::runtime_error("write failed");
}
void writeU64LE(std::ostream& os, uint64_t v) {
  const uint8_t b[8] = {
      static_cast<uint8_t>(v & 0xFFu),         static_cast<uint8_t>((v >> 8) & 0xFFu),
      static_cast<uint8_t>((v >> 16) & 0xFFu), static_cast<uint8_t>((v >> 24) & 0xFFu),
      static_cast<uint8_t>((v >> 32) & 0xFFu), static_cast<uint8_t>((v >> 40) & 0xFFu),
      static_cast<uint8_t>((v >> 48) & 0xFFu), static_cast<uint8_t>((v >> 56) & 0xFFu),
  };
  os.write(reinterpret_cast<const char*>(b), 8);
  if (!os) throw std::runtime_error("write failed");
}

uint16_t readU16LE(std::istream& is) {
  uint8_t b[2]{};
  is.read(reinterpret_cast<char*>(b), 2);
  if (!is) throw std::runtime_error("read failed");
  return static_cast<uint16_t>(static_cast<uint16_t>(b[0]) | (static_cast<uint16_t>(b[1]) << 8));
}
uint32_t readU32LE(std::istream& is) {
  uint8_t b[4]{};
  is.read(reinterpret_cast<char*>(b), 4);
  if (!is) throw std::runtime_error("read failed");
  return static_cast<uint32_t>(static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
                               (static_cast<uint32_t>(b[2]) << 16) |
                               (static_cast<uint32_t>(b[3]) << 24));
}
uint64_t readU64LE(std::istream& is) {
  uint8_t b[8]{};
  is.read(reinterpret_cast<char*>(b), 8);
  if (!is) throw std::runtime_error("read failed");
  uint64_t v = 0;
  for (int i = 7; i >= 0; --i) v = (v << 8) | b[i];
  return v;
}

struct FileHeader final {
  uint16_t flags = 0;
  uint64_t originalSize = 0;
  uint32_t blockSize = 0;
  uint32_t fileCrc32 = 0;
  uint32_t blockCount = 0;
};

struct BlockHeader final {
  uint32_t uncompressedSize = 0;
  uint32_t compressedSize = 0;
  uint32_t blockCrc32 = 0;
};

void writeFileHeader(std::ostream& os, const FileHeader& h) {
  os.write(reinterpret_cast<const char*>(kMagic.data()), kMagic.size());
  if (!os) throw std::runtime_error("write failed");
  writeU16LE(os, kVersion);
  writeU16LE(os, h.flags);
  writeU64LE(os, h.originalSize);
  writeU32LE(os, h.blockSize);
  writeU32LE(os, h.fileCrc32);
  writeU32LE(os, h.blockCount);
}

FileHeader readFileHeader(std::istream& is) {
  uint8_t magic[4]{};
  is.read(reinterpret_cast<char*>(magic), 4);
  if (!is) throw std::runtime_error("read failed");
  if (!(magic[0] == kMagic[0] && magic[1] == kMagic[1] && magic[2] == kMagic[2] &&
        magic[3] == kMagic[3])) {
    throw std::runtime_error("bad magic");
  }
  const uint16_t version = readU16LE(is);
  if (version != kVersion) throw std::runtime_error("unsupported version");

  FileHeader h{};
  h.flags = readU16LE(is);
  h.originalSize = readU64LE(is);
  h.blockSize = readU32LE(is);
  h.fileCrc32 = readU32LE(is);
  h.blockCount = readU32LE(is);
  return h;
}

void writeBlockHeader(std::ostream& os, const BlockHeader& h) {
  writeU32LE(os, h.uncompressedSize);
  writeU32LE(os, h.compressedSize);
  writeU32LE(os, h.blockCrc32);
}

BlockHeader readBlockHeader(std::istream& is) {
  BlockHeader h{};
  h.uncompressedSize = readU32LE(is);
  h.compressedSize = readU32LE(is);
  h.blockCrc32 = readU32LE(is);
  return h;
}

std::vector<uint8_t> compressBlock(std::span<const uint8_t> block) {
  const auto toks = lz77Encode(block, {});

  std::array<uint32_t, 286> llFreq{};
  std::array<uint32_t, 30> dFreq{};
  for (const auto& t : toks) {
    if (t.kind == Lz77Token::Kind::Literal) {
      ++llFreq[t.lit];
    } else {
      const auto lc = deflate::encodeLength(t.length);
      const auto dc = deflate::encodeDistance(t.distance);
      ++llFreq[lc.sym];
      ++dFreq[dc.sym];
    }
  }
  ++llFreq[256];

  const auto llLensV = buildHuffmanCodeLengths(std::span<const uint32_t>(llFreq.data(), llFreq.size()), 15);
  const auto dLensV = buildHuffmanCodeLengths(std::span<const uint32_t>(dFreq.data(), dFreq.size()), 15);

  const auto llCodesV = buildCanonicalCodesFromLengths(std::span<const uint8_t>(llLensV.data(), llLensV.size()));
  const auto dCodesV = buildCanonicalCodesFromLengths(std::span<const uint8_t>(dLensV.data(), dLensV.size()));

  BitWriter bw;
  bw.reserve(block.size() / 2 + 256);

  for (uint8_t l : llLensV) bw.writeBits(l, 5);
  for (uint8_t l : dLensV) bw.writeBits(l, 5);

  for (const auto& t : toks) {
    if (t.kind == Lz77Token::Kind::Literal) {
      const auto hc = llCodesV[t.lit];
      bw.writeBits(hc.bits_lsb, hc.bitlen);
    } else {
      const auto lc = deflate::encodeLength(t.length);
      const auto dc = deflate::encodeDistance(t.distance);

      const auto llc = llCodesV[lc.sym];
      bw.writeBits(llc.bits_lsb, llc.bitlen);
      if (lc.extra) bw.writeBits(lc.extraVal, lc.extra);

      const auto dcod = dCodesV[dc.sym];
      bw.writeBits(dcod.bits_lsb, dcod.bitlen);
      if (dc.extra) bw.writeBits(dc.extraVal, dc.extra);
    }
  }

  {
    const auto eob = llCodesV[256];
    bw.writeBits(eob.bits_lsb, eob.bitlen);
  }

  bw.finalize();
  return bw.buffer();
}

std::vector<uint8_t> decompressBlock(std::span<const uint8_t> comp, uint32_t expectedSize) {
  BitReader br(comp.data(), comp.size());

  std::array<uint8_t, 286> llLens{};
  std::array<uint8_t, 30> dLens{};
  for (size_t i = 0; i < llLens.size(); ++i) llLens[i] = static_cast<uint8_t>(br.readBits(5));
  for (size_t i = 0; i < dLens.size(); ++i) dLens[i] = static_cast<uint8_t>(br.readBits(5));

  HuffmanDecoder llDec(std::span<const uint8_t>(llLens.data(), llLens.size()));
  HuffmanDecoder dDec(std::span<const uint8_t>(dLens.data(), dLens.size()));

  std::vector<uint8_t> out;
  out.reserve(expectedSize);

  // Note: This is "deflate-inspired" but not deflate bitstream compatible.
  // Symbols come from the same LL/D code spaces, but code-lengths are stored
  // directly (5 bits each) and then the data stream follows.
  while (true) {
    const uint32_t sym = llDec.decodeSymbol(br);
    if (sym < 256) {
      out.push_back(static_cast<uint8_t>(sym));
      continue;
    }
    if (sym == 256) break;

    uint16_t extraVal = 0;
    if (sym == 285) {
      extraVal = 0;
    } else {
      const uint16_t idx = static_cast<uint16_t>(sym - 257);
      static constexpr uint8_t extraTable[28] = {
          0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2,
          2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5};
      const uint8_t eb = extraTable[idx];
      if (eb) extraVal = static_cast<uint16_t>(br.readBits(eb));
    }

    uint16_t length = 0;
    if (sym == 285) {
      length = 258;
    } else {
      static constexpr uint16_t baseTable[28] = {
          3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23,
          27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227};
      length = static_cast<uint16_t>(baseTable[sym - 257] + extraVal);
    }

    const uint32_t distSym = dDec.decodeSymbol(br);
    if (distSym > 29) throw std::runtime_error("bad dist symbol");

    static constexpr uint16_t distBase[30] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
        193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
        8193, 12289, 16385, 24577};
    static constexpr uint8_t distExtra[30] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
        6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

    const uint8_t deb = distExtra[distSym];
    const uint16_t dextra = deb ? static_cast<uint16_t>(br.readBits(deb)) : 0;
    const uint16_t distance = static_cast<uint16_t>(distBase[distSym] + dextra);

    if (distance == 0 || distance > out.size()) throw std::runtime_error("bad backref");

    // Correct for overlapping copies: read from the already-produced output,
    // which may include bytes we append during this loop when distance < length.
    for (uint16_t k = 0; k < length; ++k) {
      out.push_back(out[out.size() - distance]);
    }
  }

  if (out.size() != expectedSize) throw std::runtime_error("block size mismatch");
  return out;
}

} // namespace

void compressFile(const std::filesystem::path& inPath, const std::filesystem::path& outPath,
                  const ApexCompressOptions& opt) {
  std::ifstream in(inPath, std::ios::binary);
  if (!in) throw std::runtime_error("failed to open input");

  std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
  if (!out) throw std::runtime_error("failed to open output");

  const uint32_t blockSize = (opt.blockSize == 0) ? (1u << 20) : opt.blockSize;
  constexpr uint32_t kMaxBlock = 64u * 1024u * 1024u;
  if (blockSize == 0 || blockSize > kMaxBlock) {
    throw std::invalid_argument("blockSize must be 1 .. 67108864");
  }

  unsigned hwConc = std::thread::hardware_concurrency();
  if (hwConc == 0) hwConc = 1;
  const uint32_t threads =
      (opt.threads == 0) ? static_cast<uint32_t>(hwConc) : opt.threads;
  if (threads == 0 || threads > 4096u) {
    throw std::invalid_argument("threads must be 1 .. 4096 (use 0 for auto)");
  }

  FileHeader hdr{};
  hdr.flags = 0;
  hdr.originalSize = 0;
  hdr.blockSize = blockSize;
  hdr.fileCrc32 = 0;
  hdr.blockCount = 0;

  const std::streampos headerPos = out.tellp();
  writeFileHeader(out, hdr);

  std::vector<uint8_t> buf(blockSize);
  uint32_t crc = 0xFFFFFFFFu;

  std::deque<std::future<std::pair<BlockHeader, std::vector<uint8_t>>>> inFlight;

  auto submit = [&](std::vector<uint8_t> owned, uint32_t used) {
    return std::async(std::launch::async, [owned = std::move(owned), used]() mutable {
      const auto span = std::span<const uint8_t>(owned.data(), used);
      const uint32_t bcrc = crc32(span);
      auto comp = compressBlock(span);
      BlockHeader bh{};
      bh.uncompressedSize = used;
      bh.compressedSize = static_cast<uint32_t>(comp.size());
      bh.blockCrc32 = bcrc;
      return std::make_pair(bh, std::move(comp));
    });
  };

  while (true) {
    in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    const auto got = static_cast<uint32_t>(in.gcount());
    if (got == 0) break;

    crc = crc32(std::span<const uint8_t>(buf.data(), got), crc);
    hdr.originalSize += got;
    ++hdr.blockCount;

    // Avoid copying the full `blockSize` buffer when `got < blockSize`.
    std::vector<uint8_t> owned(buf.begin(), buf.begin() + got);
    inFlight.push_back(submit(std::move(owned), got));

    if (inFlight.size() >= threads) {
      auto res = inFlight.front().get();
      inFlight.pop_front();
      writeBlockHeader(out, res.first);
      out.write(reinterpret_cast<const char*>(res.second.data()),
                static_cast<std::streamsize>(res.second.size()));
      if (!out) throw std::runtime_error("write failed");
    }
  }

  for (auto& f : inFlight) {
    auto res = f.get();
    writeBlockHeader(out, res.first);
    out.write(reinterpret_cast<const char*>(res.second.data()),
              static_cast<std::streamsize>(res.second.size()));
    if (!out) throw std::runtime_error("write failed");
  }

  hdr.fileCrc32 = crc;
  out.seekp(headerPos);
  writeFileHeader(out, hdr);
}

void decompressFile(const std::filesystem::path& inPath, const std::filesystem::path& outPath) {
  std::ifstream in(inPath, std::ios::binary);
  if (!in) throw std::runtime_error("failed to open input");

  const FileHeader hdr = readFileHeader(in);

  std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
  if (!out) throw std::runtime_error("failed to open output");

  uint64_t produced = 0;
  uint32_t crc = 0xFFFFFFFFu;

  for (uint32_t bi = 0; bi < hdr.blockCount; ++bi) {
    const BlockHeader bh = readBlockHeader(in);
    std::vector<uint8_t> comp(bh.compressedSize);
    in.read(reinterpret_cast<char*>(comp.data()), static_cast<std::streamsize>(comp.size()));
    if (!in) throw std::runtime_error("read failed");

    auto block = decompressBlock(std::span<const uint8_t>(comp.data(), comp.size()), bh.uncompressedSize);
    const uint32_t bcrc = crc32(std::span<const uint8_t>(block.data(), block.size()));
    if (bcrc != bh.blockCrc32) throw std::runtime_error("block checksum mismatch");

    out.write(reinterpret_cast<const char*>(block.data()), static_cast<std::streamsize>(block.size()));
    if (!out) throw std::runtime_error("write failed");

    crc = crc32(std::span<const uint8_t>(block.data(), block.size()), crc);
    produced += block.size();
  }

  if (produced != hdr.originalSize) throw std::runtime_error("file size mismatch");
  if (crc != hdr.fileCrc32) throw std::runtime_error("file checksum mismatch");
}

} // namespace apex

