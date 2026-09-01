# metal-cpp (vendored)

Apple's official C++ bindings for Metal, Foundation and QuartzCore.

| | |
|---|---|
| Upstream | <https://github.com/apple/metal-cpp> |
| Commit | `27c4382b7151d55a51692cdcb27aaa98752240de` (2026-06-08, "metal-cpp for macOS27, iOS27") |
| License | Apache-2.0 (`LICENSE.txt`) |

Header-only, exactly like `vendor/stb` and `vendor/tinyobj`: nothing here is
compiled on its own. `engine/src/Renderer/Metal/MtlAura/MetalCppImplementation.cpp`
is the single translation unit that defines `NS_PRIVATE_IMPLEMENTATION`,
`MTL_PRIVATE_IMPLEMENTATION` and `CA_PRIVATE_IMPLEMENTATION`, which is what
emits the selector/class tables the inline bindings reference.

## What was left out, and why

Upstream's `MetalFX/` (frame interpolation, temporal/spatial upscaling) and
`SingleHeader/MakeSingleHeader.py` are not vendored. No header under
`Foundation/`, `Metal/` or `QuartzCore/` references either, so their absence
cannot break an include; add them back verbatim from the commit above if the
engine ever grows a MetalFX path.

## Updating

Copy `Foundation/`, `Metal/`, `QuartzCore/`, `LICENSE.txt` and `README.md`
straight from an upstream checkout and refresh the commit above — the vendored
tree is deliberately unmodified so a diff against upstream stays empty.
