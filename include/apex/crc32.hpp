#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace apex {

uint32_t crc32(std::span<const uint8_t> data, uint32_t seed = 0xFFFFFFFFu);

} // namespace apex

