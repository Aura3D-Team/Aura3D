# stb (vendored)

Sean Barrett's single-file public-domain libraries.

| File | Version | Upstream |
|---|---|---|
| `stb_image.h` | see the header's own changelog | <https://github.com/nothings/stb> |
| `stb_truetype.h` | see the header's own changelog | <https://github.com/nothings/stb> |
| `stb_vorbis.c` | v1.22 (2026-08-13) | <https://raw.githubusercontent.com/nothings/stb/master/stb_vorbis.c> |

All three are dual-licensed public domain / MIT (see the license block at the
end of each file).

## stb_vorbis is compiled, the other two are not

`stb_image.h` and `stb_truetype.h` are header-only: a single translation unit
defines `STB_IMAGE_IMPLEMENTATION` / `STB_TRUETYPE_IMPLEMENTATION` and no
separate source file enters the build.

`stb_vorbis.c` is different — upstream ships it as a `.c`, not a header — so it
is compiled as its own translation unit, the way `vendor/glad/glad.c` is. See
the root `CMakeLists.txt`, which appends it to the engine's sources and
compiles it with `STB_VORBIS_NO_STDIO` (Aura3D reads the file itself, so the
decoder only ever needs the in-memory entry point) and `STB_VORBIS_NO_PUSHDATA_API`
(the pull API is the one `AudioClipLoader` uses).

## Updating

Copy the file straight from an upstream checkout and refresh the version above
— the vendored copy is deliberately unmodified so a diff against upstream stays
empty.
