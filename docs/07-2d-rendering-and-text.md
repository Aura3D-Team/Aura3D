# 7. 2D Rendering & Text

## Batches

```cpp
const std::array<gfx::BatchVertex, 4> vertices{{
    {{10, 10, 0}, {0, 0}, {1, 1, 1, 1}},
    {{110, 10, 0}, {1, 0}, {1, 1, 1, 1}},
    {{110, 60, 0}, {1, 1}, {1, 1, 1, 1}},
    {{10, 60, 0}, {0, 1}, {1, 1, 1, 1}},
}};
constexpr std::array<u32, 6> indices{0, 1, 2, 2, 3, 0};
r->drawBatch(vertices, indices, panelTexture);                        // screen pixels
r->drawBatch(vertices, indices, panelTexture, gfx::BatchSpace::World); // through the transform
```

One pipeline draws every batch, in submission order with meshes: unlit
`texel * color`, straight alpha, depth-tested less-or-equal, no depth writes.
Draw opaque meshes first and UI last.

Colours are linear on every backend: blending is linear and the target stores
sRGB, so `{0.5, 0.5, 0.5, 1}` shows as 188, not 128. A hex or colour-picker
value is sRGB; convert it to linear before passing it.

| `gfx::BatchSpace` | Positions |
|---|---|
| `Screen` (default) | Render-target pixels from the top-left; `z` is depth, `0` on the near plane (in front of everything), `1` on the far one. |
| `World` | Through the current model, view and projection. `drawMeshes()` leaves the model it was given. |

- `{}` as the texture samples white, so the vertex colour alone shows. The arrays may be reused on return.
- Every `drawBatch` is one draw call: put what shares a texture and space in one pair of arrays.
- Rebuild the arrays each frame into vectors that keep their capacity, or keep them and draw them in `World` space under a transform: `setTransform({model, view, projection})`.

## Text

Text is drawn by AuraUI (`AURA_ENABLE_UI`, on by default). A `ui::UIView` owns
the text engine and draws through `drawBatch`, so a HUD is a few labels:

```cpp
#include "aura/UI/UI.hpp"

ui::UIView ui(*r, {.fontPath = "resources/fonts/Inter.ttf"}); // optional font
auto &hud = ui.root().setContent<ui::Column>();
hud.layout().padding = ui::Thickness::all(10.0f);
auto &score = hud.add<ui::Label>("Score: 0");
score.setFontSize(18.0f);

// per frame, between beginRenderPass()/endRenderPass(), after the scene:
score.text = "Score: " + std::to_string(points);
ui.render(deltaSeconds);
```

**Font loading never fails visibly**: leave `fontPath` empty, or point it at
a file that can't be loaded, and the UI falls back to the engine's embedded
bitmap font, which needs no asset on disk, so text works on WASM/Android with
nothing staged.

The bitmap face is drawn at 16 px. From 13 to 16 px it keeps every glyph row
and only the line box changes, so those sizes look alike; smaller sizes give up
the least telling rows, larger ones repeat them. Load a `.ttf` for sizes that
must differ continuously.

**Text is UTF-8**; `'\n'` starts a new line; codepoints the font doesn't carry
render as `?`. Without widgets, `ui::DrawList::drawText()` draws text shaped by
`ui::TextEngine`; see [14-auraui-toolkit.md](14-auraui-toolkit.md).

### Why this is cheap: `FontAtlas`

Under the hood, the text engine keeps glyphs in `FontAtlas` pages — each one
GPU texture (`glyphPageSize`², 1024×1024 by default), allocated once via
`IRenderer::createCoverageTexture`. Each glyph is rasterized lazily, the
first time it's actually drawn, packed in with a shelf allocator, and
uploaded as a small sub-image via `updateCoverageTextureRegion` — a new character
costs a few hundred bytes, never a new texture. This is what makes growing
vocabulary (scores, chat, dynamic labels) affordable on Vulkan in particular,
where every texture permanently consumes a descriptor-pool slot: recreating
one per new character would exhaust the pool within minutes.

Coverage textures use one byte per texel (R8), sampled as white RGB plus alpha.
A 1024×1024 sheet uses 1 MiB instead of 4 MiB, with one quarter of the upload bytes.
The RGBA texture API remains available for images.

AuraUI batches panels, widgets and text together (Sandbox's whole sidebar is one
draw call); see **[14-auraui-toolkit.md](14-auraui-toolkit.md)**.

Next: **[08-input.md](08-input.md)**.
