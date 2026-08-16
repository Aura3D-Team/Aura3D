# 12. Immediate-Mode UI

Aura3D ships a small immediate-mode UI — panels, buttons, checkboxes,
sliders — in `aura3d::ui`. It's for the interfaces an engine actually needs:
debug overlays, tweak panels, level editors and standalone tools.

It is deliberately tiny. It vendors nothing and adds no per-backend code — it
speaks only the public `IRenderer` API, so the same UI code runs unchanged on
Vulkan, OpenGL, Metal and the software rasteriser, on desktop, Android and
WebAssembly. It builds on the 2D pipeline from
**[07-2d-rendering-and-text.md](07-2d-rendering-and-text.md)**.

This chapter is how to *use* it. For how it works inside — widget identity,
the interaction state machine, why the whole UI is one draw call — see
**[13-auraui-internals.md](13-auraui-internals.md)**.

```cpp
#include "aura/UI/AuraUI.h"
```

## Immediate mode in one minute

There is no widget tree, no `new Button(...)`, no callbacks to register and
later unregister. A widget is a function call that both draws itself and
returns what the user did to it:

```cpp
if (gui.button("Reload shaders"))
    reloadShaders();
```

Nothing persists between frames except a little interaction bookkeeping. The
UI is rebuilt from scratch every frame, which is the whole point: it cannot
disagree with the state it displays. A checkbox bound to `bool wireframe`
*is* `wireframe` — there is no second copy to fall out of sync, no
`setChecked()` you can forget to call.

The flip side is that a widget only exists on the frames you call it. Stop
calling `button()` and the button is gone; there is nothing to hide or
destroy.

## Setup

```cpp
ui::ContextDesc desc;
desc.pixelHeight = 16.0f;             // glyph rasterization size
// desc.fontPath = "assets/Inter.ttf"; // optional
ui::Context gui(r, desc);

gui.attachInput(*r->getWindowManager());
```

`Context` construction never throws and never half-fails. A font that can't be
read falls back to the engine's embedded bitmap font, so the UI still draws on
platforms where no asset was staged. If even the atlas texture can't be
allocated the context goes inert — every widget still returns sensible values,
nothing draws — rather than leaving you with a live object that crashes.

`attachInput` binds the left mouse button and polls the cursor position, so
`newFrame()` needs no arguments. It only binds the *button*; the cursor is
polled rather than bound, so it will not displace your camera's move callback.
It binds on the mouse listener's currently active input context — push a
dedicated context first if your app already uses the left button and the two
must not share (see [08-input.md](08-input.md)).

To feed input yourself — a test, a replay harness, a custom device — skip
`attachInput` and pass an `ui::Input`:

```cpp
gui.newFrame(ui::Input{.mouse = {x, y}, .mouseDown = leftHeld});
```

## The shape of a frame

```cpp
r->beginRenderPass();

// ... your scene, if any ...

gui.newFrame();

if (gui.beginPanel("Scene", {16.0f, 48.0f}, 260.0f))
{
    gui.label("Backend: VULKAN");
    gui.separator();
    gui.checkbox("Spin objects", spinning);
    gui.endPanel();
}

gui.render();

r->endRenderPass();
```

Three rules, and they are the only ones:

1. `newFrame()` before any widget — it latches input and clears last frame's
   geometry.
2. `render()` after the last widget, inside the render pass, **after** the
   scene's own draws. The UI composites over them with straight alpha and no
   depth test.
3. `endPanel()` only when `beginPanel()` returned `true`.

## A complete tool

Nothing here is a game: no camera, no meshes, no lighting. This is `Engine`
plus `aura3d::ui`, which is all a tool needs.

```cpp
#include <cstdio>
#include <string>
#include <vector>

#include "aura/Core/Engine.h"
#include "aura/UI/AuraUI.h"

using namespace aura3d;

/// The document this tool edits. Plain data — the UI never owns it.
struct Emitter {
    std::string name = "sparks";
    bool enabled = true;
    float rate = 120.0f;
    float lifetime = 1.5f;
    float spread = 0.35f;
};

int main()
{
    Engine engine("settings.json");
    IRenderer* r = engine.getRenderer();

    ui::ContextDesc desc;
    desc.pixelHeight = 16.0f;
    ui::Context gui(r, desc);
    gui.attachInput(*r->getWindowManager());

    std::vector<Emitter> emitters = {Emitter{}, Emitter{"smoke", false, 40.0f, 4.0f, 0.8f}};
    size_t selected = 0;
    bool showAdvanced = false;

    r->setClearColor(0.09f, 0.10f, 0.13f, 1.0f);

    r->run([&]() {
        r->beginRenderPass();
        gui.newFrame();

        if (gui.beginPanel("Emitters", {24.0f, 24.0f}, 200.0f))
        {
            for (size_t i = 0; i < emitters.size(); ++i)
            {
                // Selection is derived state, so it is drawn rather than
                // stored in a widget: the marker is part of the label.
                const std::string label =
                    (i == selected ? "> " : "  ") + emitters[i].name;

                if (gui.button(label))
                    selected = i;
            }

            gui.separator();

            if (gui.button("Add emitter"))
            {
                emitters.push_back(Emitter{"emitter " + std::to_string(emitters.size())});
                selected = emitters.size() - 1;
            }

            gui.endPanel();
        }

        Emitter& active = emitters[selected];

        if (gui.beginPanel("Properties", {240.0f, 24.0f}, 280.0f))
        {
            gui.label(active.name);
            gui.separator();

            gui.checkbox("Enabled", active.enabled);
            gui.sliderFloat("Rate", active.rate, 0.0f, 500.0f);
            gui.sliderFloat("Lifetime", active.lifetime, 0.1f, 10.0f);

            gui.separator();
            gui.checkbox("Advanced", showAdvanced);

            if (showAdvanced)
            {
                gui.sliderFloat("Spread", active.spread, 0.0f, 1.0f);
                gui.spacing(4.0f);

                char summary[64];
                std::snprintf(summary, sizeof(summary), "%.0f particles alive",
                              static_cast<double>(active.rate * active.lifetime));
                gui.label(summary);
            }

            gui.endPanel();
        }

        gui.render();
        r->endRenderPass();
    });

    return 0;
}
```

Four things in there are worth naming, because they are the patterns you'll
reuse:

- **The UI reads and writes your data directly.** `emitters` is a plain
  `std::vector`. There is no model, no binding layer, no observer. `active` is
  a reference straight into it, and `sliderFloat` edits it in place.
- **Panels appear and disappear by control flow.** `showAdvanced` gates three
  widgets with an ordinary `if`. That is the whole mechanism — no
  `setVisible()`, no layout invalidation.
- **The list is a loop.** Adding an emitter mutates the vector; the next frame
  draws one more button. Nothing has to be told the list changed.
- **Derived values are computed at draw time.** `particles alive` is recomputed
  every frame from the two sliders. There is no cache to invalidate, because
  the frame *is* the cache.

Build it like any other app — link `Aura3D::Aura3D` and drop a `settings.json`
beside the binary (see [01-getting-started.md](01-getting-started.md)).

## Widget reference

| Call | Returns |
|---|---|
| `beginPanel(title, defaultPos, width)` | `true` when open; pair with `endPanel()` only then |
| `endPanel()` | — |
| `label(text)` | — |
| `button(text)` | `true` on the frame the press is released over it |
| `checkbox(text, bool&)` | `true` on the frame the value changed |
| `sliderFloat(text, float&, min, max)` | `true` on any frame the value changed |
| `separator()` | — |
| `spacing(pixels)` | — |
| `measureText(text)` | pixel size of one line at the current text scale |
| `wantsMouse()` | `true` when the UI is using the mouse |
| `style()` | mutable `ui::Style` |

Panels are draggable by their title bar and remember their position between
frames, keyed by title. `defaultPosition` therefore places a panel the first
time it is seen and is ignored afterwards, rather than yanking a dragged panel
back every frame. Height follows the content; nothing needs sizing by hand.

Widgets called outside a `beginPanel`/`endPanel` pair draw nothing and report
no interaction, so a stray call is inert rather than corrupting.

## Patterns worth knowing

### Do expensive work only on change

Value widgets return whether the value *changed*, not whether it is being
touched. Use that to keep uploads and rebuilds off the frames that don't need
them:

```cpp
if (gui.sliderFloat("Light", intensity, 0.0f, 3.0f))
{
    light.intensity = intensity;
    r->setLight(light);       // only on frames the slider actually moved
}
```

### Labels are identity

A widget is recognised from frame to frame by a hash of its label and its
position in the panel. Two consequences:

- **Identical labels in one panel are still distinct.** A `Reset` beside
  another `Reset` works — a per-panel counter is folded into the hash, so they
  don't share interaction state.
- **A label that changes is a different widget.** That is usually harmless
  (a frame of hover state resets), but it means you shouldn't put a
  fast-changing value in the label of something being dragged. Put it in the
  panel title or a `label()` beside it instead.

### Formatting values

There is no `printf`-style widget. Format into a buffer and pass it — a
`std::string` and a `char[]` both convert to the `std::string_view` the
widgets take:

```cpp
char line[64];
std::snprintf(line, sizeof(line), "Draw calls: %d", drawCallCount);
gui.label(line);
```

### Sharing the mouse with a game

A click meant for a slider must not also spin the camera. `wantsMouse()`
answers that:

```cpp
if (!gui.wantsMouse())
    pickObjectUnderCursor();
```

It's true while the cursor is over any panel, and stays true while a widget
owns an in-flight press even if the drag has since left it. It reports the
most recently completed UI frame, so it is stable wherever in the loop you ask.

Mouse-look needs more than this. Free-look captures the cursor
(`setCursorEnabled(false)`), and a captured cursor has no position for the UI
to hit-test against — so the two are modes, not neighbours. `apps/Sandbox`
binds <kbd>Tab</kbd> to swap:

```cpp
keyboard.addKeyAction(wma::KEY_TAB, wma::KeyAction{[&]() {
    uiVisible = !uiVisible;
    mouse.setCursorEnabled(uiVisible);
}});
```

...and skips `newFrame`/`render` entirely while the UI is hidden.

## Styling

`gui.style()` returns a mutable `ui::Style`: colours for each widget state,
plus `rowHeight`, `itemSpacing`, `padding` and `textScale`. Nothing is
retained between frames, so a change takes effect on the very next widget —
including mid-panel:

```cpp
gui.style().accent = {0.9f, 0.4f, 0.1f, 1.0f};
```

For permanently larger text prefer raising `ContextDesc::pixelHeight`, which
rasterizes the glyphs bigger. `textScale` magnifies an already-rasterized
bitmap and gets soft well before it gets large.

## Building without it

`AURA_ENABLE_UI` (default `ON`) gates the module. Built with `OFF`, no
`aura3d::ui` symbol reaches the library and `engine/include/aura/UI/` is not
installed. `AURA_HAS_UI` is defined for consumers when it's on:

```cpp
#ifdef AURA_HAS_UI
#include "aura/UI/AuraUI.h"
#endif
```

## Limits

Worth knowing before you reach for it:

- **Mouse only.** wma delivers no text-input or character events, so there is
  no text field, and no keyboard navigation.
- **No scrolling or docking.** Panels are auto-height and free-floating.
- **Panel order is call order.** The last panel submitted draws on top and
  wins the cursor; there is no click-to-focus reordering.
- **One `Context` per thread, not re-entrant.** Same rule as the renderer it
  draws through.

Two places to see it running: `apps/Sandbox/main.cpp` has a single working
panel over a live 3D scene, and **`apps/UIPlayground`** is this chapter's own
companion app — the "complete tool" example above, runnable, with UI panels
spawning/removing textured objects and driving a live-animated sprite. Build
and run it (`cd build/linux/release/apps/UIPlayground && ./UIPlayground`) to
see every pattern in this chapter at once. `tests/test_ui.cpp` drives the
module headlessly if you want its behaviour pinned down precisely instead.

---

Next: **[13-auraui-internals.md](13-auraui-internals.md)** — how it works
inside.
