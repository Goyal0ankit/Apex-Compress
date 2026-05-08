#include "apex/apxc.hpp"

#if __has_include("apex_compress_version.hpp")
#include "apex_compress_version.hpp"
#else
#define APEX_COMPRESS_VERSION "0.1.0-dev"
#endif

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] int usage(std::ostream& os, int code) {
  os << "apex-compress " APEX_COMPRESS_VERSION << R"(

LZ77 + Huffman compressor. Output format: .apxc (not ZIP / not gzip).

Commands:
  compress <input> <output.apxc> [--threads N] [--block-size BYTES]
  decompress <input.apxc> <output>

Options:
  --threads N       Parallel block jobs (0 = hardware concurrency).
  --block-size N    Uncompressed bytes per block (default 1048576).

Examples:
  apex-compress compress  big.bin  big.apxc
  apex-compress decompress big.apxc out.bin

Documentation: README.md in the project repository.
)";
  return code;
}

void parseCompressOpts(int argc, char** argv, int start, apex::ApexCompressOptions& out) {
  for (int i = start; i < argc; ++i) {
    const std::string_view a(argv[i]);
    if (a == "--threads" && i + 1 < argc) {
      const auto v = std::stoul(argv[++i]);
      if (v > 4096u) throw std::runtime_error("--threads must be <= 4096");
      out.threads = static_cast<uint32_t>(v);
    } else if (a == "--block-size" && i + 1 < argc) {
      const auto v = std::stoul(argv[++i]);
      if (v == 0 || v > 64u * 1024u * 1024u)
        throw std::runtime_error("--block-size must be 1 .. 67108864 bytes");
      out.blockSize = static_cast<uint32_t>(v);
    } else {
      throw std::runtime_error(std::string("unknown option: ") + std::string(a));
    }
  }
}

} // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 2) return usage(std::cout, 0);

    const std::string_view cmd(argv[1]);
    if (cmd == "-h" || cmd == "--help" || cmd == "help") return usage(std::cout, 0);
    if (cmd == "-V" || cmd == "--version") {
      std::cout << "apex-compress " APEX_COMPRESS_VERSION << "\n";
      return 0;
    }

    if (cmd == "compress") {
      if (argc < 4) throw std::runtime_error("missing arguments (see --help)");
      apex::ApexCompressOptions opt;
      parseCompressOpts(argc, argv, 4, opt);
      apex::compressFile(std::filesystem::path(argv[2]), std::filesystem::path(argv[3]), opt);
      return 0;
    }

    if (cmd == "decompress") {
      if (argc < 4) throw std::runtime_error("missing arguments (see --help)");
      if (argc > 4)
        throw std::runtime_error("decompress takes only two paths (see --help)");
      apex::decompressFile(std::filesystem::path(argv[2]), std::filesystem::path(argv[3]));
      return 0;
    }

    std::cerr << "unknown command \"" << argv[1] << "\" (try --help)\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "apex-compress: " << e.what() << "\n";
    return 1;
  }
}
