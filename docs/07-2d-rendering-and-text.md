# 7. 2D Rendering & Text

Every backend has a second, independent pipeline dedicated to unlit,
window-pixel-space 2D geometry — HUDs, UI, text, sprites — layered on top of
whatever the 3D scene drew. It shares nothing with the 3D pipeline's camera,
lighting, or depth state.

## `gfx::Vertex2D` and `drawBatch2D`

```cpp
namespace aura3d::gfx {
struct Vertex2D {
    glm::vec2 pos;      // window pixels, (0,0) = top-left
    glm::vec2 texCoord;
    glm::vec4 color;
};
}
```

```cpp
virtual void drawBatch2D(std::span<const gfx::Vertex2D> vertices,
                         std::span<const u32> indices,
                         TextureHandle texture) = 0;
```

Everything about this call is designed around "a whole HUD's worth of quads
in one draw":

- **Coordinates** are window pixels with `(0,0)` at the top-left; the
  backend derives its own orthographic projection from the current
  framebuffer size, so you never fold a camera in yourself.
- **Shading is unlit**: `texel * vertexColor`. The scene light has no effect
  on 2D content.
- **Depth testing is off, alpha blending is on** (straight/non-premultiplied
  source-over) — a 2D batch always composites over everything drawn so far
  this pass.
- **The whole batch is one draw call** — a 500-quad HUD costs the same as a
  single quad. Vertex/index data is copied into backend-owned buffers, so
  your arrays can be reused or discarded the instant this returns.
- `texture = INVALID_HANDLE` draws untextured, vertex-color only.

Call it **between `beginRenderPass()`/`endRenderPass()`, after your 3D
draws** — it leaves no 2D pipeline state bound, so the next 3D draw call
rebinds its own pipeline cleanly.

```cpp
std::vector<gfx::Vertex2D> verts = {
    {{10, 10}, {0, 0}, {1, 1, 1, 1}},
    {{110, 10}, {1, 0}, {1, 1, 1, 1}},
    {{110, 60}, {1, 1}, {1, 1, 1, 1}},
    {{10, 60}, {0, 1}, {1, 1, 1, 1}},
};
std::vector<u32> idx = {0, 1, 2, 2, 3, 0};
r->drawBatch2D(verts, idx, panelTexture);
```

On the CPU backend, `drawBatch2D` is backed by
`CpuFrameBufferManager::drawTriangle2D` — the software rasterizer's
counterpart to its 3D `drawTriangle`: affine (not perspective-correct)
interpolation, no depth interaction, the same source-over blending.

## `TextOverlay`: text on top of `drawBatch2D`

Writing per-glyph quads by hand is exactly what `TextOverlay`
(`aura/Core/TextOverlay/TextOverlay.h`) exists to avoid.

```cpp
#include "aura/Core/TextOverlay/TextOverlay.h"

TextOverlayDesc desc;
desc.fontPath    = "resources/fonts/Inter.ttf"; // optional
desc.pixelHeight = 18.0f;
desc.atlasSize   = 2048;
desc.color       = {1, 1, 1, 1};
TextOverlay overlay(r, desc); // built once — allocates the glyph atlas texture

// per frame, between beginRenderPass()/endRenderPass():
overlay.drawText("Score: 1200", 10.0f, 10.0f);
overlay.drawText("Warning!", 10.0f, 40.0f, glm::vec4(1, 0.3f, 0.3f, 1)); // per-call color
overlay.drawFPS(10.0f, 60.0f); // formats "FPS: <n>" from live frame timing
```

**Font loading never fails visibly**: leave `fontPath` empty, or point it at
a file that can't be loaded, and the overlay falls back to the engine's
embedded bitmap font — which needs no asset on disk at all, so text keeps
working on WASM/Android with nothing staged. Check
`overlay.usingTrueType()` if you need to know which one is active.

**Text is UTF-8** (`std::string_view` in, decoded internally); `'\n'` starts
a new line; codepoints the font doesn't carry render as `?`.

```cpp
glm::vec2 size = overlay.measureText("Game Over", 2.0f); // pixel bounds at this scale
float lh = overlay.lineHeight(1.0f);                     // baseline-to-baseline distance
overlay.setColor({1, 1, 0, 1});                            // default color for future drawText calls
```

`measureText` is not `const` — measuring a string rasterizes any glyph in it
that isn't cached yet, same as drawing it would.

### Why this is cheap: `FontAtlas`

Under the hood, `TextOverlay` owns a `FontAtlas` — one large GPU texture
(`atlasSize`² by default 2048×2048), allocated once via
`IRenderer::createDynamicTexture`. Each glyph is rasterized lazily, the
first time it's actually drawn, packed in with a shelf allocator, and
uploaded as a small sub-image via `updateTextureRegion` — a new character
costs a few hundred bytes, never a new texture. This is what makes growing
vocabulary (scores, chat, dynamic labels) affordable on Vulkan in particular,
where every texture permanently consumes a descriptor-pool slot: recreating
one per new character would exhaust the pool within minutes.

Drawing a string walks it once, appends one quad per glyph into two vectors
reused across frames, and hands the whole thing to `drawBatch2D` as a single
draw call — a 500-character string costs one draw call, not 500.

Next: **[08-input.md](08-input.md)**.
