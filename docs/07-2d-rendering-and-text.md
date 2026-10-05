# 7. 2D Rendering & Text

## Batches, lines and shapes

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

gfx::Canvas canvas;                                                    // keeps capacity across frames
canvas.line({16, 24}, {96, 24}, {1, 0, 0, 1}, 2);                      // 2 px on screen
canvas.line(r->canvasView(), {0, 0, 0}, {0, 2, 0}, {0, 1, 0, 1}, 2);   // world, 2 px at any depth
canvas.rect({16, 144}, {80, 48}, {1, 1, 0, 1});
canvas.triangle(glm::vec2{128, 144}, {176, 144}, {128, 192}, {1, 0, 1, 1});
r->drawBatch(canvas);                                                  // one draw call
canvas.clear();

// Any other shape: triangles through append(); indices count from the first vertex passed.
const std::array<gfx::BatchVertex, 4> corners{{{{200, 20, 0}, {}, {1, 0, 0, 1}},
                                               {{300, 20, 0}, {}, {0, 1, 0, 1}},
                                               {{300, 80, 0}, {}, {0, 0, 1, 1}},
                                               {{200, 80, 0}, {}, {1, 1, 0, 1}}}};
constexpr std::array<u32, 6> quad{0, 1, 2, 2, 3, 0};
canvas.append(corners, quad);                                          // a gradient rectangle
```

One pipeline draws every batch, in submission order with meshes: unlit
`texel * color`, straight alpha, depth-tested less-or-equal, no depth writes.
Draw opaque meshes first and UI last.

| `gfx::BatchSpace` | Positions |
|---|---|
| `Screen` (default) | Render-target pixels from the top-left; `z` is depth, `0` on the near plane (in front of everything), `1` on the far one. |
| `World` | Through the current model, view and projection. `drawMeshes()` leaves the model it was given. |

- `{}` as the texture samples white, so the vertex colour alone shows. The arrays may be reused on return.
- Every `drawBatch` is one draw call. Put what shares a texture and space in one `gfx::Canvas`.
- `line(r->canvasView(), ...)` projects with the camera current at that call; draw its batch in `Screen` space.
- Invalid shapes (nonfinite, degenerate, zero width, invisible) add nothing.
- To move shapes, either rebuild the canvas each frame (`clear()` keeps its memory), or keep it and draw it in `World` space under a transform: `setTransform({model, view, projection})`.

## `TextOverlay`: text on top of `drawBatch`

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
overlay.addText("Score: 1200", 10.0f, 10.0f);
overlay.addText("Warning!", 10.0f, 40.0f, glm::vec4(1, 0.3f, 0.3f, 1)); // per-call color
overlay.addFPS(10.0f, 60.0f); // smoothed "FPS: <n>  (<ms> ms)"; overlay.fps() reads it back
overlay.draw();               // everything queued, one draw call
```

**Font loading never fails visibly**: leave `fontPath` empty, or point it at
a file that can't be loaded, and the overlay falls back to the engine's
embedded bitmap font — which needs no asset on disk at all, so text keeps
working on WASM/Android with nothing staged. Check
`overlay.usingTrueType()` if you need to know which one is active.

The bitmap face is drawn at 16 px. From 13 to 16 px it keeps every glyph row
and only the line box changes, so those sizes look alike; smaller sizes give up
the least telling rows, larger ones repeat them. Load a `.ttf` for sizes that
must differ continuously.

**Text is UTF-8** (`std::string_view` in, decoded internally); `'\n'` starts
a new line; codepoints the font doesn't carry render as `?`.

```cpp
glm::vec2 size = overlay.measureText("Game Over", 2.0f); // pixel bounds at this scale
float lh = overlay.lineHeight(1.0f);                     // baseline-to-baseline distance
overlay.setColor({1, 1, 0, 1});                            // default color for future addText calls
```

`measureText` is not `const` — measuring a string rasterizes any glyph in it
that isn't cached yet, same as drawing it would.

### Why this is cheap: `FontAtlas`

Under the hood, `TextOverlay` owns a `FontAtlas` — one large GPU texture
(`atlasSize`² by default 2048×2048), allocated once via
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

`addText` walks the string once and appends one quad per glyph to a batch
reused across frames; `draw()` hands the frame's text to `drawBatch` as a
single draw call — however many strings and characters it holds.

AuraUI, the engine's widget toolkit, is the other consumer of this pipeline:
panels, buttons and sliders, drawn entirely through `drawBatch` and this
same glyph atlas — see **[14-auraui-toolkit.md](14-auraui-toolkit.md)**.

Next: **[08-input.md](08-input.md)**.
