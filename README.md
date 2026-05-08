# Apex-Compress (`apex-compress`)

Modern C++20 compression tool showcasing **low-level bit I/O**, **LZ77 match finding**, **canonical Huffman coding**, and a simple **block-based container format** with CRC32 integrity checks.

This is a learning / portfolio project: the codec is *deflate-inspired* (same length/distance symbol spaces), but the produced bitstream is **not** RFC1951-deflate compatible. It is **not** a drop-in replacement for `.zip` / `gzip`; it is **only** interoperable with this tool (`*.apxc`).

## Giving the tool to someone else

1. **Recommended (source)**  
   Publish the repo (GitHub / GitLab / zip archive). Others install **GLFW** for the GUI (see **Build**), then run `./scripts/build.sh`. Binaries appear under `build/`.

2. **Ship a binary**  
   Build on the target OS/CPU, put `apex-compress` and/or `apex-gui` in a release archive. Recipients do not need a compiler. The GUI may still need **system OpenGL** and (on macOS/Linux) GLFW if you link GLFW dynamically (`brew`/distro GLFW is typically dylib/so).

## What it does

- **Container format**: `APXC` file header + per-block headers, each protected by CRC32.
- **Compression pipeline (per block)**:
  - LZ77 tokenization (`literal` / `length+distance` matches)
  - Huffman frequency count over the token stream
  - Canonical Huffman code generation
  - Bit-packed output using a tiny LSB-first bit writer
- **Decompression** mirrors the above and validates CRCs.

## Requirements / dependencies

- **Compressor core**: C++ standard library only (codec has no runtime third-party libs).
- **Build**: POSIX `sh`, a **C++20** compiler (`c++`, overridable with `CXX=`).
- **GUI (`apex-gui`)**:
  - Vendored [Dear ImGui](https://github.com/ocornut/imgui) (`third_party/imgui`)
  - Vendored [portable-file-dialogs](https://github.com/samhocevar/portable-file-dialogs) header (`third_party/portable_file_dialogs`)
  - **GLFW 3**: **not** vendored — install a development library the linker can find:
    - **macOS**: `brew install glfw`
    - **Debian/Ubuntu**: `sudo apt-get install libglfw3-dev`
- **Platforms**: `./scripts/build.sh` supports **macOS** and **Linux** for the GUI. **Windows** is not scripted here (use MSVC / your own flags, or **WSL** with the Linux path).

Project version number is **`VERSION`** in the repo root; `scripts/build.sh` generates `build/generated/apex_compress_version.hpp` from **`scripts/apex_compress_version.hpp.in`**.

## Build

```bash
chmod +x scripts/build.sh   # once, if needed
rm -rf build   # recommended once if this tree used to be built with CMake (stale compile_commands confuse editors)
./scripts/build.sh              # builds build/apex-compress and build/apex-gui
./scripts/build.sh --cli-only   # CLI only (no GLFW needed)
```

Optional: `OUT_DIR=/tmp/out ./scripts/build.sh` to choose the output directory.

Binaries:

- **`build/apex-compress`** — CLI compressor/decompressor.
- **`build/apex-gui`** — windowed UI (ImGui + GLFW + OpenGL 3).

## Usage (CLI)

```bash
build/apex-compress --help
build/apex-compress --version

# Compress
build/apex-compress compress input.bin output.apxc --threads 8 --block-size 1048576

# Decompress (no extra flags)
build/apex-compress decompress output.apxc roundtrip.bin
```

Notes:

- `--threads 0` (default) uses `std::thread::hardware_concurrency()`.
- `--block-size` defaults to **1 MiB**. Blocks compress independently.

### GUI (`apex-gui`)

Run `./build/apex-gui` and use the UI to **Compress** or **Decompress**.

## Editor / IntelliSense (clangd)

**`compile_flags.txt`** and **`.clangd`** list include paths for the core, ImGui, portable-file-dialogs, and common Homebrew GLFW locations. If `GLFW/glfw3.h` still does not resolve, add your GLFW include path (for example `$(brew --prefix glfw)/include`) to **`.clangd`** or **`compile_flags.txt`**, then reload the editor.

## `APXC` on-disk format (v1)

The file and block headers use **explicit little-endian** field encoding (not raw `struct` writes), so the container layout is stable across typical ABIs.

- **File header** (`FileHeader` in `src/apxc.cpp`)
  - magic: `'A' 'P' 'X' 'C'`
  - version: `1`
  - flags: reserved
  - originalSize: total uncompressed bytes
  - blockSize: chosen block size in bytes
  - fileCrc32: CRC32 over the full uncompressed stream (seeded with `0xFFFFFFFF`)
  - blockCount: number of blocks
- **For each block**
  - `BlockHeader`:
    - uncompressedSize
    - compressedSize
    - blockCrc32 (CRC32 over the uncompressed block, same seed convention)
  - `compressedSize` bytes of compressed payload

### Block payload layout (deflate-inspired, custom)

1. **Huffman code lengths** (stored *verbatim*):
   - 286 literal/length code lengths, **5 bits each**
   - 30 distance code lengths, **5 bits each**
2. **Token stream**, encoded with those canonical codes:
   - literals: symbol 0..255 in the LL alphabet
   - matches:
     - length symbol 257..285 + extra bits (deflate length tables)
     - distance symbol 0..29 + extra bits (deflate distance tables)
   - end-of-block: LL symbol **256**

## Code tour (where to look)

- `include/apex/bit_io.hpp`, `src/bit_io.cpp`: LSB-first bit packing/unpacking
- `include/apex/lz77.hpp`, `src/lz77.cpp`: hash-chain LZ77 (window=32KiB, max match=258)
- `include/apex/huffman.hpp`, `src/huffman.cpp`: frequency count, canonical codes, fast-ish decoder
- `include/apex/deflate_tables.hpp`, `src/deflate_tables.cpp`: length/distance base+extra tables
- `include/apex/crc32.hpp`, `src/crc32.cpp`: CRC32 implementation
- `include/apex/apxc.hpp`, `src/apxc.cpp`: container format + threaded block compressor

## Design notes (portfolio highlights)

- **RAII everywhere**: streams own file handles; vectors own buffers; no manual `new/delete`.
- **STL + low-level control**: `std::span` for bounds-safe views, `std::array` for fixed tables, `std::vector` for contiguous storage, `std::future` for async blocks.
- **Performance-minded hot paths**:
  - bounded hash-chain search in LZ77 (`maxChain`, `niceLength`)
  - canonical Huffman decoding with a small root lookup table
  - block compressor avoids copying unused tail bytes of the input block

## A deeper read for beginners

If you are new to compression, the order that helped me was: **LZ77 first** (where repeats come from), **Huffman second** (how symbols become bits), then **open `src/apxc.cpp`** and watch those two meet inside `compressBlock`. Everything else in the repo is mostly plumbing: files, CRCs, threads, and the GUI.

### The big picture in one breath

Uncompressed data often repeats. LZ77 turns repeats into short instructions: essentially “copy from earlier in the stream.” Those instructions are still just symbols in a larger alphabet (raw bytes, length codes, distance codes). Huffman assigns **shorter bit patterns** to **common** symbols and longer patterns to rare ones. `BitWriter` packs those patterns into bytes. That byte blob, plus a small header per block, is what you get in a `.apxc` file.

ZIP's DEFLATE uses the same *family* of ideas. This project uses the same *length/distance symbol layout* as DEFLATE on purpose so you can read the RFC with a straight face, but the **exact bitstream layout here is custom** so the code stays small. Your system `unzip` or `gunzip` will not understand `.apxc`, and that is expected.

### LZ77 — what it is doing, with a hand trace

LZ77 does not understand English, JSON, or JPEG. It only sees bytes and a sliding **window** of recent past. When the next bytes already appeared **behind** the current position, the encoder can emit a **match**: “starting from here, the next *length* bytes are the same as the *length* bytes that start *distance* bytes back in the output we are building.”

Take a silly string (quotes are not in the data):

`AAAABAAAA`

Scan left to right in your head. After `AAAAB`, the tail `AAAA` overlaps with the four `A`s you already emitted right after the `B`. An LZ77-style description might compress that overlap into one match instead of four separate literal `A` bytes. The exact split between literals and matches depends on thresholds in `Lz77Options`, but the *idea* is always: **pointer backward** instead of repeating yourself.

This codebase finds candidates quickly with a **hash chain** over 3-byte keys (`src/lz77.cpp`). It hashes three consecutive bytes, looks up previous positions with the same hash, walks a short chain (`maxChain`), and measures how long a real match runs. If nothing clears `minMatch`, you get a **literal** (one raw byte). That is why random data barely shrinks: there is nothing to point at, so you pay for every byte.

Defaults you will see in `include/apex/lz77.hpp`: a **32 KiB** window (power of two, masked), **min** match 3, **max** match 258 (DEFLATE's upper bound), plus `niceLength` so the encoder stops searching once a “good enough” match shows up. Those knobs trade CPU for ratio.

### Huffman — same toy, different angle

Huffman needs a finite **alphabet** and a **count** per symbol. In plain byte-wise Huffman the alphabet is just 0–255. After LZ77, the alphabet is larger on the literal/length side: 256 literals, extra symbols that stand for length ranges, and symbol **256** which means “end of this compressed block.” Distances use another small alphabet (30 symbols in the DEFLATE-style layout). You do not need the numbers memorized on day one; just know the tree is built from **whatever actually appeared** in that block's token list.

Micro-example with three symbols only — `A`, `B`, and `END` — with counts 45, 13, and 12. Huffman repeatedly merges the two smallest piles until one tree remains. Leaves that sit higher in the tree get **shorter** codewords. `A` was common, so it gets, say, a 1-bit code; `B` and `END` get longer ones. **Canonical** Huffman fixes the mapping from “code length” to “actual bits” so the decoder only needs the **list of lengths**, not the whole tree shape. That is why `compressBlock` can write 286 lengths with 5 bits each, then 30 more, and the other side can still rebuild decoders.

In code: lengths come from `buildHuffmanCodeLengths`, codewords from `buildCanonicalCodesFromLengths`, and decoding goes through `HuffmanDecoder` in `src/huffman.cpp` with `BitReader` from `src/bit_io.cpp`.

### Where LZ77 and Huffman shake hands (`compressBlock`)

In `src/apxc.cpp`, function `compressBlock` is the spine:

1. `lz77Encode` → `std::vector<Lz77Token>` (each token is either a literal byte or a match with length and distance).
2. Walk the tokens and fill two frequency tables: literal/length (286 entries) and distance (30). Always bump symbol **256** once so the end-of-block code exists.
3. Build Huffman lengths (max 15 bits here), then canonical codewords.
4. `BitWriter` stores the length tables (5 bits per length), then walks the tokens again and emits Huffman bits. Matches also pull **extra bits** through `deflate::encodeLength` / `encodeDistance` (`include/apex/deflate_tables.hpp`) — same numeric tables as DEFLATE, different wrapping.

Decompression is the mirror image inside `decompressBlock`: read lengths, build decoders, read symbols until 256, expand literals and backward copies. There is a comment in that function worth reading once: **deflate-inspired, not a byte-compatible DEFLATE stream**.

### What each file is for (when you are lost)

- `include/apex/apxc.hpp` + `src/apxc.cpp` — the only “product” API: `compressFile` / `decompressFile`, the `.apxc` header layout, threading over blocks, CRC32 of the whole stream and each block. If you only read one implementation file, read this one after LZ77/Huffman click.
- `include/apex/bit_io.hpp` + `src/bit_io.cpp` — `BitWriter` / `BitReader`, LSB-first. When bit counts disagree by one, you are probably here.
- `include/apex/lz77.hpp` + `src/lz77.cpp` — tokenization only. Good place to unit-test a tiny string and print tokens.
- `include/apex/huffman.hpp` + `src/huffman.cpp` — tree build, canonical codes, `HuffmanDecoder::decodeSymbol`.
- `include/apex/deflate_tables.hpp` + `src/deflate_tables.cpp` — maps match lengths 3..258 and distances into symbol + extra-bit bundles shared with DEFLATE's tables.
- `include/apex/crc32.hpp` + `src/crc32.cpp` — integrity. Mismatches throw rather than silently writing garbage.
- `src/main_cli.cpp` — parses argv and calls the two public functions.
- `src/gui_main.cpp` — same codec, ImGui + GLFW front end; `third_party/` holds vendored UI bits and file dialogs.
- `scripts/build.sh`, `VERSION`, `scripts/apex_compress_version.hpp.in` — no CMake here; the shell script lists `.cpp` files explicitly and generates `build/generated/apex_compress_version.hpp`.

### Two experiments worth doing once

Make a 64 KiB file that is only the letter `e`. Compress it. Ratio should look almost unfair because LZ77 finds huge runs and Huffman gives those symbols very short codes. Then try a few megabytes from `/dev/urandom` (or any high-entropy blob). Size stays almost flat — almost all literals, almost no structure to learn. Seeing both ends of that spectrum explains real-world compression better than any diagram.

When something throws, read the message literally. `bad magic` means the first four bytes were not `APXC`. Checksum errors mean a block or the whole-file CRC disagreed with the bytes you thought you wrote — often truncation, manual hex editing, or mixing format versions.

