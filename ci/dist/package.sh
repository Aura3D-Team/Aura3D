#!/usr/bin/env bash
# Stages the Aura3D install tree for a release and folds libwma and libink into its archive, so a consumer needs
# only Vulkan headers (docs/11-using-releases.md). Runs in the ci/dist image after build.sh.
#
#   package.sh <stage-dir> [ink-dir] [wma-dir]
set -euo pipefail

aura=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
stage=$(realpath -m "${1:?usage: package.sh <stage-dir> [ink-dir] [wma-dir]}")
ink_source=$(realpath "${2:-$aura/../libink}")
wma_source=$(realpath "${3:-$aura/../libwma}")
platform=linux_x86_64

cmake --install "$aura/build/linux/dist" --prefix "$stage"

# Each archive goes into its own directory first: two libraries can each contain an object named "Utils.cpp.o".
fat="$stage/lib/libaura3d_$platform.a"
# From the build directories build.sh left behind, not from $LOCAL_PREFIX: that was installed in another container.
wma=$(find "$wma_source/build/linux/dist" -name "libwma_$platform.a" | head -1)
ink=$(find "$ink_source/build/linux/dist" -name "libink_$platform.a" | head -1)
if [[ -z "$wma" || -z "$ink" ]]; then
    echo "libwma/libink for $platform not found under their build/linux/dist; run build.sh first" >&2
    exit 1
fi

# The archives hold LTO objects, which plain ar/ranlib cannot index: use the compiler's wrappers.
gcc_ar=$(command -v gcc-ar-15 || command -v gcc-ar)
gcc_ranlib=$(command -v gcc-ranlib-15 || command -v gcc-ranlib)

merge=$(mktemp -d)
n=0
for lib in "$fat" "$wma" "$ink"; do
    mkdir -p "$merge/$n"
    (cd "$merge/$n" && ar -x "$lib")
    n=$((n + 1))
done
find "$merge" -name '*.o' -print0 | xargs -0 "$gcc_ar" -qc "$fat.merged"
"$gcc_ranlib" "$fat.merged"
mv "$fat.merged" "$fat"

# The merged archive carries wma and ink; the package must not ask for them again.
sed -i '/find_dependency(wma)/d;/find_dependency(ink)/d' "$stage/lib/cmake/Aura3D/Aura3D-deps-$platform.cmake"

if [[ -n "${HOST_UID:-}" ]]; then
    chown -R "$HOST_UID:${HOST_GID:-$HOST_UID}" "$stage"
fi
