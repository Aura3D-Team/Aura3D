#!/usr/bin/env bash
# gen_embedded_spirv.sh — regenerate EmbeddedSpirv.h from resources/shaders/vulkan.
#
# The Vulkan backend boots from SPIR-V modules compiled into the binary
# (VulkanRenderer::createResourceManagers passes vk_vert_3d / vk_frag_3d), so
# editing the GLSL under resources/shaders/vulkan has NO effect until this
# script is run and the result committed.
#
# Run this after any change to resources/shaders/vulkan/*.
#
# Prerequisites: glslc (Vulkan SDK / shaderc).
#
# Usage:
#   ./scripts/gen_embedded_spirv.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"

SHADER_DIR="$ROOT/resources/shaders/vulkan"
OUT="$ROOT/engine/include/aura/Renderer/Vulkan/VkAura/EmbeddedSpirv.h"

if ! command -v glslc >/dev/null 2>&1; then
  echo "glslc not found. Install the Vulkan SDK / shaderc and retry." >&2
  exit 1
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# symbol_name : source file
SHADERS=(
  "vk_vert_2d:vk_shader2d.vert"
  "vk_frag_2d:vk_shader2d.frag"
  "vk_vert_3d:vk_shader3d.vert"
  "vk_frag_3d:vk_shader3d.frag"
)

{
  echo '#ifndef EMBEDDED_SPIRV_SHADERS_H'
  echo '#define EMBEDDED_SPIRV_SHADERS_H'
  echo ''
  echo '#pragma once'
  echo ''
  echo '/*'
  echo ' * GENERATED FILE - do not edit by hand.'
  echo ' * Regenerate with scripts/gen_embedded_spirv.sh after editing'
  echo ' * resources/shaders/vulkan/*.'
  echo ' */'
  echo ''
  echo 'namespace aura3d {'
  echo 'namespace vk {'
  echo ''
} > "$TMP/EmbeddedSpirv.h"

for entry in "${SHADERS[@]}"; do
  symbol="${entry%%:*}"
  source_file="${entry#*:}"

  echo "Compiling $source_file -> $symbol"
  glslc "$SHADER_DIR/$source_file" -o "$TMP/$symbol.spv"

  size=$(stat -c%s "$TMP/$symbol.spv")

  {
    printf 'static const unsigned char %s[] = {\n  ' "$symbol"
    # One flat comma-separated list of 0x.. bytes, matching the existing layout.
    od -An -v -tx1 "$TMP/$symbol.spv" \
      | tr -s ' ' '\n' \
      | grep -v '^$' \
      | sed 's/^/0x/' \
      | paste -sd, - \
      | sed 's/,/, /g'
    printf '\n};\n'
    printf 'static const unsigned int %s_len = %s;\n\n' "$symbol" "$size"
  } >> "$TMP/EmbeddedSpirv.h"
done

{
  echo '} // namespace vk'
  echo '} // namespace aura3d'
  echo ''
  echo '#endif // EMBEDDED_SPIRV_SHADERS_H'
} >> "$TMP/EmbeddedSpirv.h"

mv "$TMP/EmbeddedSpirv.h" "$OUT"
echo ""
echo "Wrote $OUT"
