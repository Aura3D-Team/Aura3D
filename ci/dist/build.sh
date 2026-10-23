#!/usr/bin/env bash
# Builds libink, libwma and Aura3D with the linux-dist preset inside the ci/dist image, installs them to
# $LOCAL_PREFIX, runs Aura3D's tests and refuses binaries that need more than the floor.
#
#   docker run --rm -v "$PWD/..":/workspace aura-dist bash Aura3D/ci/dist/build.sh [ink-dir] [wma-dir]
#
# The sources are siblings by default (../libink, ../libwma), as in a developer's workspace.
set -euo pipefail

aura=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
ink=${1:-$aura/../libink}
wma=${2:-$aura/../libwma}

build() {
    local source=$1
    shift
    # --fresh: the build directory may be left over from another image, with its cached tool paths.
    (cd "$source" && cmake --preset linux-dist --fresh "$@" && cmake --build build/linux/dist --parallel \
        && cmake --install build/linux/dist)
}

build "$ink" -DINK_BUILD_TESTS=OFF
build "$wma" -DWMA_BUILD_TESTS=OFF -DWMA_BUILD_EXAMPLES=OFF
build "$aura" -DAURA_BUILD_TESTS=ON

ctest --test-dir "$aura/build/linux/dist" --output-on-failure

mapfile -t binaries < <(find "$aura/build/linux/dist/apps" -maxdepth 2 -type f -executable ! -name '*.so*')
python3 "$aura/ci/check-portability.py" --glibc "${AURA_GLIBC_FLOOR:-2.27}" "${binaries[@]}"

# The image runs as root; hand the results back to whoever mounted the workspace.
if [[ -n "${HOST_UID:-}" ]]; then
    chown -R "$HOST_UID:${HOST_GID:-$HOST_UID}" "$aura/build" "$ink/build" "$wma/build"
fi
