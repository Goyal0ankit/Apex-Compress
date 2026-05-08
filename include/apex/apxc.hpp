#pragma once

#include <cstdint>
#include <filesystem>

namespace apex {

struct ApexCompressOptions final {
  uint32_t blockSize = 1u << 20; // 1 MiB
  uint32_t threads = 0;         // 0 => hardware_concurrency()
};

void compressFile(const std::filesystem::path& inPath, const std::filesystem::path& outPath,
                  const ApexCompressOptions& opt = {});

void decompressFile(const std::filesystem::path& inPath, const std::filesystem::path& outPath);

} // namespace apex

