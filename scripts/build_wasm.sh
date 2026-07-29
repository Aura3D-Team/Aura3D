#!/usr/bin/env bash
# build_wasm.sh — Build Aura3D for WebAssembly (Emscripten → WebGL2)
#
# Prerequisites
#   • EMSDK env var set (source $EMSDK/emsdk_env.sh)
#   • wma and ink built with Emscripten and their CMake config available.
#     Place them under $WASM_DEPS_PREFIX (default: /usr/local/wasm-deps).
#     Example:
#       emcmake cmake -B build-wma-wasm <wma-src> -DCMAKE_INSTALL_PREFIX=/usr/local/wasm-deps
#       cmake --build build-wma-wasm --target install
#
# Usage
#   ./scripts/build_wasm.sh [--release] [--debug] [--serve]
#   --release       RelWithDebInfo build (default)
#   --debug         Debug build (larger output, source maps)
#   --serve         Start a local server after build (requires Python 3)
#   --no-asyncify   Disable Asyncify (faster build/smaller output; may hang)
#   --prefix PATH   Override WASM deps prefix (wma, ink)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"

BUILD_TYPE="Release"
SERVE=0
ASYNCIFY="ON"
WASM_DEPS_PREFIX="${WASM_DEPS_PREFIX:-/usr/local/wasm-deps}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --debug)        BUILD_TYPE="Debug"; shift ;;
    --release)      BUILD_TYPE="Release"; shift ;;
    --serve)        SERVE=1; shift ;;
    --no-asyncify)  ASYNCIFY="OFF"; shift ;;
    --prefix)       WASM_DEPS_PREFIX="$2"; shift 2 ;;
    *)              shift ;;
  esac
done

# Sanity checks
if [[ -z "${EMSDK:-}" ]]; then
  echo "❌  EMSDK is not set. Source the Emscripten environment first:"
  echo "      source \$EMSDK/emsdk_env.sh"
  exit 1
fi

EMCMAKE="$(command -v emcmake 2>/dev/null || true)"
if [[ -z "$EMCMAKE" ]]; then
  echo "❌  emcmake not found. Is Emscripten in your PATH?"
  exit 1
fi

echo "▶  Emscripten : $EMSDK"
echo "▶  Build type : $BUILD_TYPE"
echo "▶  Asyncify   : $ASYNCIFY"
echo "▶  Deps prefix: $WASM_DEPS_PREFIX"
echo ""

BUILD_DIR="$ROOT/build/wasm-${BUILD_TYPE,,}"

# Configure
#
# CMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH: the Emscripten toolchain file
# defaults find_package() to searching only its own sysroot, which would
# never see wma/ink installed under WASM_DEPS_PREFIX.
emcmake cmake -S "$ROOT" -B "$BUILD_DIR" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_PREFIX_PATH="$WASM_DEPS_PREFIX" \
  -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH \
  -DAURA_WASM_ASYNCIFY="$ASYNCIFY" \
  -DAURA_WASM_OUTPUT_NAME="Aura3D" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Build
cmake --build "$BUILD_DIR" -- -j"$(nproc)"

echo ""
echo "✅  WASM build complete."
echo "    Output: $BUILD_DIR/pkg/Aura3D.js + Aura3D.wasm"
echo ""

# Copy the root index.html next to the pkg/ output for easy serving
cp "$ROOT/index.html" "$BUILD_DIR/"

# Optional local server
if [[ "$SERVE" -eq 1 ]]; then
  echo "▶  Starting HTTP server at http://localhost:8080 (Ctrl-C to stop)"
  cd "$BUILD_DIR"
  python3 -m http.server 8080
fi
