#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace apex {

class BitWriter final {
public:
  BitWriter() = default;

  void reserve(size_t bytes);
  void clear();

  void writeBits(uint32_t value, uint32_t count);
  void flushToByteBoundary();
  void finalize();

  void writeByteAligned(uint8_t b);
  void writeBytesAligned(const uint8_t* data, size_t len);

  const std::vector<uint8_t>& buffer() const;
  std::vector<uint8_t>& buffer();

  size_t bytesWritten() const;
  uint32_t pendingBits() const;

private:
  std::vector<uint8_t> out_;
  uint64_t bitbuf_ = 0;
  uint32_t bitcount_ = 0;
};

class BitReader final {
public:
  BitReader(const uint8_t* data, size_t size);

  uint32_t readBits(uint32_t count);
  uint32_t peekBits(uint32_t count);
  void dropBits(uint32_t count);
  void alignToByte();

  bool eof() const;
  size_t bytesConsumed() const;
  uint32_t bufferedBits() const;

private:
  void ensure(uint32_t need);

  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
  size_t pos_ = 0;
  uint64_t bitbuf_ = 0;
  uint32_t bitcount_ = 0;
};

} // namespace apex
