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
#     Place them under $ANDROID_DEPS_PREFIX (default: /usr/local).
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

for arg in "$@"; do
  case "$arg" in
    --abi)    shift; ABI="$1" ;;
    --apk)    BUILD_APK=1 ;;
    --debug)  BUILD_TYPE="Debug" ;;
    --prefix) shift; ANDROID_DEPS_PREFIX="$1" ;;
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
  -DCMAKE_PREFIX_PATH="$ANDROID_DEPS_PREFIX" \
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
