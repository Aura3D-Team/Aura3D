#!/usr/bin/env bash
# gen_embedded_metallib.sh — regenerate EmbeddedMetalLib.h from resources/shaders/metal.
#
# The Metal backend boots from shaders compiled into the binary
# (MtlShaderLibraryManager reads the symbols this script emits), so editing the
# MSL under resources/shaders/metal has NO effect until this script is run and
# the result committed.
#
# Run this after any change to resources/shaders/metal/*.
#
# The header always carries the MSL *source* -- which is why this script runs on
# any host, Linux CI included -- and additionally carries a precompiled
# .metallib whenever `xcrun metal` is available (macOS with Xcode). Those two
# cases are distinguished by AURA_METAL_HAS_EMBEDDED_METALLIB in the generated
# header; MtlShaderLibraryManager prefers the .metallib and falls back to
# compiling the source at device-creation time.
#
# Prerequisites: none to regenerate the source-only form.
#                Xcode's Metal toolchain (`xcrun metal`) to also embed a
#                .metallib, which is macOS-only.
#
# Usage:
#   ./scripts/gen_embedded_metallib.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"

SHADER_DIR="$ROOT/resources/shaders/metal"
OUT="$ROOT/engine/include/aura/Renderer/Metal/MtlAura/EmbeddedMetalLib.h"

# Concatenation order of the MSL sources. Both files are compiled into a single
# MTLLibrary holding all four entry points, so this is also the order the
# fallback source string is assembled in.
SOURCES=(
  "mtl_shader3d.metal"
  "mtl_shader2d.metal"
)

for source_file in "${SOURCES[@]}"; do
  if [[ ! -f "$SHADER_DIR/$source_file" ]]; then
    echo "Missing shader source: $SHADER_DIR/$source_file" >&2
    exit 1
  fi
done

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# ------------------------------------------------------------------------------
# Offline compile (macOS only).
#
# Each .metal is compiled to an .air, then the airs are linked into one
# .metallib -- the same two-step Xcode itself performs, and the reason per-file
# diagnostics stay attributable to the file that caused them.
# ------------------------------------------------------------------------------
HAS_METALLIB=0
if command -v xcrun >/dev/null 2>&1 && xcrun --find metal >/dev/null 2>&1; then
  AIR_FILES=()
  for source_file in "${SOURCES[@]}"; do
    air="$TMP/${source_file%.metal}.air"
    echo "Compiling $source_file -> $(basename "$air")"
    xcrun metal -std=metal3.0 -O2 -c "$SHADER_DIR/$source_file" -o "$air"
    AIR_FILES+=("$air")
  done

  echo "Linking $(basename "$OUT")'s embedded library"
  xcrun metallib "${AIR_FILES[@]}" -o "$TMP/aura.metallib"
  HAS_METALLIB=1
else
  echo "xcrun metal not found: emitting the MSL-source form only." >&2
  echo "Re-run on macOS with Xcode installed to also embed a compiled .metallib." >&2
fi

# ------------------------------------------------------------------------------
# Emit the header.
# ------------------------------------------------------------------------------
{
  echo '#ifndef EMBEDDED_METAL_LIB_H'
  echo '#define EMBEDDED_METAL_LIB_H'
  echo ''
  echo '#pragma once'
  echo ''
  echo '/*'
  echo ' * GENERATED FILE - do not edit by hand.'
  echo ' * Regenerate with scripts/gen_embedded_metallib.sh after editing'
  echo ' * resources/shaders/metal/*.'
  echo ' */'
  echo ''
  echo '/**'
  echo ' * @brief 1 when this header carries a precompiled .metallib, 0 when it'
  echo ' *        carries only MSL source.'
  echo ' *'
  echo ' * Only a macOS host with Xcode can produce the compiled form, so a header'
  echo ' * regenerated on Linux (or committed from a CI run) legitimately reports 0.'
  echo ' * MtlShaderLibraryManager handles both.'
  echo ' */'
  printf '#define AURA_METAL_HAS_EMBEDDED_METALLIB %d\n' "$HAS_METALLIB"
  echo ''
  echo 'namespace aura3d {'
  echo 'namespace mtl {'
  echo ''
  echo '//! Every MSL source under resources/shaders/metal, concatenated in the'
  echo '//! order gen_embedded_metallib.sh lists them. Compiled at runtime by'
  echo '//! MtlShaderLibraryManager when no .metallib is embedded.'
  echo 'static const char mtl_library_source[] = R"AURA_MSL('
} > "$TMP/EmbeddedMetalLib.h"

for source_file in "${SOURCES[@]}"; do
  cat "$SHADER_DIR/$source_file" >> "$TMP/EmbeddedMetalLib.h"
  echo '' >> "$TMP/EmbeddedMetalLib.h"
done

{
  echo ')AURA_MSL";'
  echo ''
} >> "$TMP/EmbeddedMetalLib.h"

if [[ "$HAS_METALLIB" -eq 1 ]]; then
  size=$(wc -c < "$TMP/aura.metallib" | tr -d ' ')
  {
    echo '//! The same shaders, precompiled. Loaded straight into an MTLLibrary,'
    echo '//! which skips the runtime MSL compile entirely.'
    printf 'static const unsigned char mtl_library_data[] = {\n  '
    # One flat comma-separated list of 0x.. bytes, matching EmbeddedSpirv.h's layout.
    od -An -v -tx1 "$TMP/aura.metallib" \
      | tr -s ' ' '\n' \
      | grep -v '^$' \
      | sed 's/^/0x/' \
      | paste -sd, - \
      | sed 's/,/, /g'
    printf '\n};\n'
    printf 'static const unsigned int mtl_library_data_len = %s;\n\n' "$size"
  } >> "$TMP/EmbeddedMetalLib.h"
fi

{
  echo '} // namespace mtl'
  echo '} // namespace aura3d'
  echo ''
  echo '#endif // EMBEDDED_METAL_LIB_H'
} >> "$TMP/EmbeddedMetalLib.h"

mkdir -p "$(dirname "$OUT")"
mv "$TMP/EmbeddedMetalLib.h" "$OUT"
echo ""
echo "Wrote $OUT (AURA_METAL_HAS_EMBEDDED_METALLIB=$HAS_METALLIB)"
