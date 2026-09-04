#!/usr/bin/env bash
# build_android.sh — Build Aura3D for Android (Vulkan, NDK CMake)
#
# Two modes
#   1. CMake-only  : builds libAura3D.a for the requested ABI.
#                   Useful for embedding Aura3D into an existing app.
#   2. Gradle (APK): builds the full Sandbox APK via android/app/build.gradle.
#
# Prerequisites
#   • ANDROID_NDK_HOME env var set (NDK 27+)
#   • ANDROID_HOME env var set (SDK with build-tools & platform 29+)
#   • wma and ink built with the Android NDK and their CMake config available.
#     Both install into $ANDROID_DEPS_PREFIX (default: /usr/local), telling
#     platforms apart by ABI tag rather than by directory. Third-party deps
#     that are still per-platform (SDL3) stay under $ANDROID_DEPS_PREFIX/android,
#     which is appended to CMAKE_PREFIX_PATH below.
#
# Usage
#   ./scripts/build_android.sh [--abi arm64-v8a|x86_64] [--apk] [--debug]
#   --abi ABI       Target ABI (default: arm64-v8a)
#   --apk           Build full APK via Gradle (requires ANDROID_HOME)
#   --debug         Debug build (default: Release)
#   --prefix PATH   Override Android deps prefix (wma, ink)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"

ABI="arm64-v8a"
BUILD_APK=0
BUILD_TYPE="Release"
ANDROID_DEPS_PREFIX="${ANDROID_DEPS_PREFIX:-/usr/local}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --abi)    ABI="$2"; shift 2 ;;
    --apk)    BUILD_APK=1; shift ;;
    --debug)  BUILD_TYPE="Debug"; shift ;;
    --prefix) ANDROID_DEPS_PREFIX="$2"; shift 2 ;;
    *)        shift ;;
  esac
done

# Sanity checks
if [[ -z "${ANDROID_NDK_HOME:-}" ]]; then
  echo "❌  ANDROID_NDK_HOME is not set."
  exit 1
fi

TOOLCHAIN="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"
if [[ ! -f "$TOOLCHAIN" ]]; then
  echo "❌  NDK toolchain file not found at: $TOOLCHAIN"
  exit 1
fi

echo "▶  NDK         : $ANDROID_NDK_HOME"
echo "▶  ABI         : $ABI"
echo "▶  Build type  : $BUILD_TYPE"
echo "▶  Deps prefix : $ANDROID_DEPS_PREFIX"
echo ""

# CMake build of libAura3D.a
BUILD_DIR="$ROOT/build/android-${ABI}-${BUILD_TYPE,,}"

cmake -S "$ROOT" -B "$BUILD_DIR" \
  -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DANDROID_ABI="$ABI" \
  -DANDROID_PLATFORM="android-29" \
  -DANDROID_STL="c++_shared" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_PREFIX_PATH="$ANDROID_DEPS_PREFIX;$ANDROID_DEPS_PREFIX/android" \
  -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

cmake --build "$BUILD_DIR" -- -j"$(nproc)"

echo ""
echo "✅  libAura3D.a built for $ABI."
echo "    Output: $BUILD_DIR/libAura3D.a"
echo ""

# Optional Gradle APK
if [[ "$BUILD_APK" -eq 1 ]]; then
  if [[ -z "${ANDROID_HOME:-}" ]]; then
    echo "❌  ANDROID_HOME is not set. Required for Gradle APK build."
    exit 1
  fi

  GRADLE="$ROOT/android/gradlew"
  if [[ ! -x "$GRADLE" ]]; then
    echo "❌  gradlew not found at $GRADLE. Run 'gradle wrapper' inside android/ first."
    exit 1
  fi

  # AGP's externalNativeBuild ignores the system `cmake` on PATH by default
  # left alone, it downloads its own SDK-managed CMake (currently pinned to
  # 3.22.1), which is older than this project's cmake_minimum_required and
  # fails to configure. Point it at the system CMake via local.properties'
  # cmake.dir (the only mechanism AGP honors for this), and verify that CMake
  # actually satisfies the project's minimum before handing it to Gradle
  # better to fail here with a clear message than deep inside a Gradle task.
  REQUIRED_CMAKE_VERSION="$(sed -n 's/^cmake_minimum_required(VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
  CMAKE_BIN="$(command -v cmake || true)"
  if [[ -z "$CMAKE_BIN" ]]; then
    echo "❌  cmake not found on PATH. Required for Gradle's externalNativeBuild (need >= $REQUIRED_CMAKE_VERSION)."
    exit 1
  fi
  CMAKE_VERSION="$(cmake --version | head -1 | awk '{print $3}')"
  if [[ -n "$REQUIRED_CMAKE_VERSION" ]] &&
     [[ "$(printf '%s\n%s\n' "$REQUIRED_CMAKE_VERSION" "$CMAKE_VERSION" | sort -V | head -1)" != "$REQUIRED_CMAKE_VERSION" ]]; then
    echo "❌  System cmake ($CMAKE_BIN) is $CMAKE_VERSION, need >= $REQUIRED_CMAKE_VERSION."
    exit 1
  fi
  # cmake.dir expects the prefix containing bin/cmake, not the bin dir itself.
  CMAKE_PREFIX="$(dirname "$(dirname "$CMAKE_BIN")")"
  echo "▶  Gradle CMake: $CMAKE_VERSION at $CMAKE_PREFIX"

  LOCAL_PROPS="$ROOT/android/local.properties"
  touch "$LOCAL_PROPS"
  grep -v '^cmake\.dir=' "$LOCAL_PROPS" > "$LOCAL_PROPS.tmp" || true
  echo "cmake.dir=$CMAKE_PREFIX" >> "$LOCAL_PROPS.tmp"
  mv "$LOCAL_PROPS.tmp" "$LOCAL_PROPS"

  GRADLE_TASK="assemble$([ "$BUILD_TYPE" = "Debug" ] && echo "Debug" || echo "Release")"

  echo "▶  Running Gradle: $GRADLE_TASK"
  cd "$ROOT/android"
  ./gradlew "$GRADLE_TASK" \
    -PANDROID_DEPS_PREFIX="$ANDROID_DEPS_PREFIX"

  echo ""
  echo "✅  APK built."
  find "$ROOT/android/app/build/outputs/apk" -name "*.apk" | while read -r apk; do
    echo "    $apk"
  done
fi
