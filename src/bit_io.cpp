#include "apex/bit_io.hpp"

#include <stdexcept>

namespace apex {

void BitWriter::reserve(size_t bytes) { out_.reserve(bytes); }

void BitWriter::clear() {
  out_.clear();
  bitbuf_ = 0;
  bitcount_ = 0;
}

void BitWriter::writeBits(uint32_t value, uint32_t count) {
  if (count > 32) throw std::invalid_argument("BitWriter::writeBits count > 32");
  if (count == 0) return;
  if (count < 32) value &= ((1u << count) - 1u);

  bitbuf_ |= (static_cast<uint64_t>(value) << bitcount_);
  bitcount_ += count;

  while (bitcount_ >= 8) {
    out_.push_back(static_cast<uint8_t>(bitbuf_ & 0xFFu));
    bitbuf_ >>= 8;
    bitcount_ -= 8;
  }
}

void BitWriter::flushToByteBoundary() {
  const uint32_t pad = (8u - (bitcount_ & 7u)) & 7u;
  if (pad) writeBits(0u, pad);
}

void BitWriter::finalize() {
  if (bitcount_ != 0) {
    out_.push_back(static_cast<uint8_t>(bitbuf_ & 0xFFu));
    bitbuf_ = 0;
    bitcount_ = 0;
  }
}

void BitWriter::writeByteAligned(uint8_t b) {
  if ((bitcount_ & 7u) != 0u) throw std::logic_error("BitWriter not byte-aligned");
  out_.push_back(b);
}

void BitWriter::writeBytesAligned(const uint8_t* data, size_t len) {
  if ((bitcount_ & 7u) != 0u) throw std::logic_error("BitWriter not byte-aligned");
  out_.insert(out_.end(), data, data + len);
}

const std::vector<uint8_t>& BitWriter::buffer() const { return out_; }
std::vector<uint8_t>& BitWriter::buffer() { return out_; }
size_t BitWriter::bytesWritten() const { return out_.size(); }
uint32_t BitWriter::pendingBits() const { return bitcount_; }

BitReader::BitReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

uint32_t BitReader::readBits(uint32_t count) {
  if (count > 32) throw std::invalid_argument("BitReader::readBits count > 32");
  if (count == 0) return 0;
  ensure(count);
  const uint32_t result = static_cast<uint32_t>(
      bitbuf_ & ((count == 32) ? 0xFFFFFFFFull : ((1ull << count) - 1ull)));
  bitbuf_ >>= count;
  bitcount_ -= count;
  return result;
}

uint32_t BitReader::peekBits(uint32_t count) {
  if (count > 32) throw std::invalid_argument("BitReader::peekBits count > 32");
  if (count == 0) return 0;
  ensure(count);
  return static_cast<uint32_t>(
      bitbuf_ & ((count == 32) ? 0xFFFFFFFFull : ((1ull << count) - 1ull)));
}

void BitReader::dropBits(uint32_t count) {
  if (count > 32) throw std::invalid_argument("BitReader::dropBits count > 32");
  if (count == 0) return;
  ensure(count);
  bitbuf_ >>= count;
  bitcount_ -= count;
}

void BitReader::alignToByte() {
  const uint32_t drop = bitcount_ & 7u;
  if (drop) dropBits(drop);
}

bool BitReader::eof() const { return (pos_ >= size_) && (bitcount_ == 0); }
size_t BitReader::bytesConsumed() const { return pos_; }
uint32_t BitReader::bufferedBits() const { return bitcount_; }

void BitReader::ensure(uint32_t need) {
  while (bitcount_ < need) {
    if (pos_ >= size_) throw std::runtime_error("BitReader: unexpected end of input");
    bitbuf_ |= (static_cast<uint64_t>(data_[pos_++]) << bitcount_);
    bitcount_ += 8;
  }
}

} // namespace apex
