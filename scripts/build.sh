#!/bin/sh
# Build apex-compress (CLI) and apex-gui (Dear ImGui + GLFW + OpenGL 3).
# Prerequisites:
#   - C++20 compiler (clang++/g++/MSVC not covered here on Windows)
#   - GLFW:
#       macOS: brew install glfw
#       Linux: libglfw3-dev (provides pkg-config glfw3)
#
# Usage:
#   ./scripts/build.sh           # CLI + GUI
#   ./scripts/build.sh --cli-only
#   ./scripts/build.sh --app     # macOS: build and create build/ApexCompress.app
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

CLI_ONLY=""
MAKE_APP=""
for arg in "$@"; do
  case "$arg" in
    --cli-only) CLI_ONLY=1 ;;
    --app) MAKE_APP=1 ;;
  esac
done

VER="$(tr -d ' \t\r\n' <VERSION)"
if [ -z "$VER" ]; then VER="0.1.0"; fi

GEN="$ROOT/build/generated"
mkdir -p "$GEN"
sed "s/@PROJECT_VERSION@/${VER}/" "$ROOT/scripts/apex_compress_version.hpp.in" >"$GEN/apex_compress_version.hpp"

CXX="${CXX:-c++}"
OUT_DIR="${OUT_DIR:-$ROOT/build}"
mkdir -p "$OUT_DIR"

FLAGS_RELEASE="-std=c++20 -O3 -Wall -Wextra -Wpedantic -Wconversion -Wshadow"
FLAGS_INC="-I${ROOT}/include -I${GEN}"

CORE_SRCS="${ROOT}/src/bit_io.cpp ${ROOT}/src/crc32.cpp ${ROOT}/src/deflate_tables.cpp \
${ROOT}/src/huffman.cpp ${ROOT}/src/apxc.cpp ${ROOT}/src/lz77.cpp"

THREAD_LIBS=""
case "$(uname -s)" in
  Darwin|Linux) THREAD_LIBS="-pthread" ;;
esac

echo "==> apex-compress (CLI)"
set -x
"$CXX" $FLAGS_RELEASE $FLAGS_INC $THREAD_LIBS \
  $CORE_SRCS "${ROOT}/src/main_cli.cpp" -o "${OUT_DIR}/apex-compress"
set +x

if [ -n "$CLI_ONLY" ]; then
  echo "Built: ${OUT_DIR}/apex-compress"
  exit 0
fi

IMGUI_DIR="${ROOT}/third_party/imgui"
IMGUI_SRCS="${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp \
${IMGUI_DIR}/imgui_widgets.cpp ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp \
${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"

PFD_INC="-isystem ${ROOT}/third_party/portable_file_dialogs"
IMGUI_INC="-I${IMGUI_DIR} -I${IMGUI_DIR}/backends"

GLFW_INC=""
GLFW_LIB=""
EXTRA_GUI_LIBS=""

case "$(uname -s)" in
  Darwin)
    GLFW_PREFIX=""
    if command -v brew >/dev/null 2>&1; then
      GLFW_PREFIX="$(brew --prefix glfw 2>/dev/null || true)"
    fi
    if [ -z "$GLFW_PREFIX" ] || [ ! -r "${GLFW_PREFIX}/include/GLFW/glfw3.h" ]; then
      echo "apex-gui: GLFW not found. Install with: brew install glfw" >&2
      exit 1
    fi
    GLFW_INC="-I${GLFW_PREFIX}/include"
    # Homebrew may ship glfw3 as static or dylib; this works for both typical layouts.
    GLFW_LIB="-L${GLFW_PREFIX}/lib -lglfw3"
    EXTRA_GUI_LIBS="-framework Cocoa -framework OpenGL -framework IOKit -framework CoreVideo"
    ;;
  Linux)
    if ! command -v pkg-config >/dev/null 2>&1; then
      echo "apex-gui: pkg-config is required on Linux (install pkg-config)." >&2
      exit 1
    fi
    if ! pkg-config --exists glfw3; then
      echo "apex-gui: GLFW not found. Install libglfw3-dev (Debian/Ubuntu) or equivalent." >&2
      exit 1
    fi
    GLFW_INC="$(pkg-config --cflags glfw3)"
    GLFW_LIB="$(pkg-config --libs glfw3)"
    EXTRA_GUI_LIBS="-lGL"
    ;;
  *)
    echo "apex-gui: unsupported OS $(uname -s). Build CLI only with: $0 --cli-only" >&2
    exit 1
    ;;
esac

echo "==> apex-gui"
set -x
"$CXX" $FLAGS_RELEASE $FLAGS_INC $IMGUI_INC $GLFW_INC $PFD_INC $THREAD_LIBS \
  $CORE_SRCS $IMGUI_SRCS "${ROOT}/src/gui_main.cpp" \
  $GLFW_LIB $EXTRA_GUI_LIBS -o "${OUT_DIR}/apex-gui"
set +x

echo "Built: ${OUT_DIR}/apex-compress"
echo "Built: ${OUT_DIR}/apex-gui"

if [ -n "$MAKE_APP" ]; then
  if [ "$(uname -s)" != "Darwin" ]; then
    echo "--app is only supported on macOS." >&2
    exit 1
  fi
  if [ ! -x "${OUT_DIR}/apex-gui" ]; then
    echo "Expected ${OUT_DIR}/apex-gui to exist." >&2
    exit 1
  fi

  APP="${OUT_DIR}/ApexCompress.app"
  MACOS_DIR="${APP}/Contents/MacOS"
  FW_DIR="${APP}/Contents/Frameworks"
  RES_DIR="${APP}/Contents/Resources"
  mkdir -p "${MACOS_DIR}" "${FW_DIR}" "${RES_DIR}"

  # Minimal Info.plist (enough to run + show name).
  cat > "${APP}/Contents/Info.plist" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key><string>en</string>
  <key>CFBundleExecutable</key><string>ApexCompress</string>
  <key>CFBundleIdentifier</key><string>com.example.apexcompress</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundleName</key><string>ApexCompress</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>0.1.0</string>
  <key>CFBundleVersion</key><string>0.1.0</string>
  <key>LSMinimumSystemVersion</key><string>10.13</string>
</dict>
</plist>
EOF

  cp -f "${OUT_DIR}/apex-gui" "${MACOS_DIR}/ApexCompress"
  chmod +x "${MACOS_DIR}/ApexCompress"

  # Bundle Homebrew GLFW dylib if apex-gui links to it.
  # We make the app look for frameworks relative to the executable.
  if command -v otool >/dev/null 2>&1 && command -v install_name_tool >/dev/null 2>&1; then
    if otool -L "${MACOS_DIR}/ApexCompress" | grep -q "libglfw"; then
      # Try common Homebrew locations.
      GLFW_DYLIB=""
      if [ -n "${GLFW_PREFIX}" ] && [ -r "${GLFW_PREFIX}/lib/libglfw.3.dylib" ]; then
        GLFW_DYLIB="${GLFW_PREFIX}/lib/libglfw.3.dylib"
      elif [ -r "/opt/homebrew/opt/glfw/lib/libglfw.3.dylib" ]; then
        GLFW_DYLIB="/opt/homebrew/opt/glfw/lib/libglfw.3.dylib"
      elif [ -r "/usr/local/opt/glfw/lib/libglfw.3.dylib" ]; then
        GLFW_DYLIB="/usr/local/opt/glfw/lib/libglfw.3.dylib"
      fi

      if [ -n "${GLFW_DYLIB}" ]; then
        cp -f "${GLFW_DYLIB}" "${FW_DIR}/libglfw.3.dylib"
        chmod 644 "${FW_DIR}/libglfw.3.dylib"
        install_name_tool -add_rpath "@executable_path/../Frameworks" "${MACOS_DIR}/ApexCompress" 2>/dev/null || true
        install_name_tool -change "${GLFW_DYLIB}" "@rpath/libglfw.3.dylib" "${MACOS_DIR}/ApexCompress" 2>/dev/null || true
      else
        echo "Warning: apex-gui links to libglfw but the dylib wasn't found to bundle." >&2
      fi
    fi
  fi

  echo "Built app bundle: ${APP}"
fi
