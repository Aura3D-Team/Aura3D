# stb (vendored)

Sean Barrett's single-file public-domain libraries.

| File | Version | Upstream |
|---|---|---|
| `stb_image.h` | v2.30 + security fixes | <https://github.com/Arthu-RL/stb-aura3d-fork> |
| `stb_truetype.h` | v1.26 + security fixes | <https://github.com/Arthu-RL/stb-aura3d-fork> |
| `stb_vorbis.c` | v1.22 + security fixes | <https://github.com/Arthu-RL/stb-aura3d-fork> |

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

These three files are copied from the hardened stb fork above, which is
upstream stb plus bounds-checking, overflow and leak fixes (stb_image, stb_truetype,
stb_vorbis). They are MIT / public domain like upstream. Update by copying the
files from the fork; do not edit them here. The fork's changes are not sent
upstream.

`stb_truetype.h` gains `stbtt_InitFontSized(info, data, data_size, offset)`, which
validates every font table against the buffer length. `FontAtlas::fromMemory`
uses it instead of `stbtt_InitFont`, so a truncated or malformed font is rejected
at load time rather than read past its end.
