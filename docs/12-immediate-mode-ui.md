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
| `inputText(label, std::string&, maxBytes)` | `true` on any frame the text changed |
| `inputFloat(label, float&)` | `true` on any frame the value changed |
| `dropdown(label, int&, items)` | `true` on the frame the selection changed |
| `radioButton(label, int&, buttonValue)` | `true` on the frame this button took the selection |
| `selectable(label, selected)` | `true` on the frame the row is clicked |
| `collapsingHeader(label, defaultOpen)` | `true` when open; emit the contents inside |
| `treeNode(label, defaultOpen)` | `true` when expanded; pair with `treePop()` only then |
| `treePop()` | — |
| `beginScroll(id, height)` | `true` when open; pair with `endScroll()` only then |
| `endScroll()` | — |
| `beginTabBar(id)` / `tabItem(label)` / `endTabBar()` | `tabItem` is `true` for the selected tab |
| `tooltip(text)` | — (applies to the widget just submitted) |
| `sameLine()` | — |
| `setNextItemWidth(px)` | — |
| `separator()` | — |
| `spacing(pixels)` | — |
| `beginDisabled(bool)` / `endDisabled()` | — (widgets between them draw greyed and take no input) |
| `measureText(text)` | pixel size of one line at the current text scale |
| `isCapturingMouse()` | `true` when the UI is using the mouse |
| `isCapturingKeyboard()` | `true` while any widget holds keyboard focus |
| `isCapturingTextInput()` | `true` while a text field is focused |
| `setKeyboardFocusHere()` | focuses the next focusable widget submitted |
| `theme` | public `ui::Theme` — one `WidgetStyle` per `Part`, plus `theme.metrics` |

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

A click meant for a slider must not also spin the camera. `isCapturingMouse()`
answers that:

```cpp
if (!gui.isCapturingMouse())
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

Every component is styled separately. `gui.theme` is a public member: a table
of one `ui::WidgetStyle` per `ui::Part` — `Part::Button`, `Part::Slider`,
`Part::Tab`, nineteen of them — plus the `ui::Metrics` they all share in
`gui.theme.metrics`. A widget reads its Part's entry as it is submitted, so an
edit lands on the very next widget, including mid-panel.

### One WidgetStyle

| Field | Meaning |
|---|---|
| `surface` | background fill, as a `ColorSet{normal, hovered, active}` |
| `border` | outline colour, same three states |
| `text` | label colour |
| `accent` | slider fill, tick mark, caret, selection, focus ring |
| `rounding` | corner radius in px; past half the shorter side it's a pill |
| `borderWidth` | outline thickness; `0` draws none |
| `padding` | text inset from the component's own edges |
| `align` | `Align::Left` / `Center` / `Right` |
| `height` | `std::optional<float>` — row height, when not `Metrics::rowHeight` |
| `markScale`, `markInset` | checkbox and radio only: tick box size, mark inset |

What "active" means is the widget's business: a button's is held, a text
field's is focused, a header's is open.

### ui::Style — a patch, not a replacement

Anywhere you style *something specific*, you pass a `ui::Style`: a sparse
override where **only the fields you set apply** and everything else falls
through to the Part's entry in the theme.

That fall-through is the point. A bare `WidgetStyle` is transparent by
default, so a "just make it red" override built from one would draw an
invisible button. A patch cannot:

```cpp
gui.button("Delete", ui::Style{}.fill(red));   // red; still the theme's
                                               // rounding, padding, height
```

The setters chain, and there is a field behind each if you want it directly:

| Setter | Sets |
|---|---|
| `.fill(color)` / `.fill(ColorSet)` | `surface` |
| `.outline(color, width = 1.0f)` | `border`, `borderWidth` |
| `.textColor(color)` | `text` |
| `.accentColor(color)` | `accent` |
| `.rounded(radius)` | `rounding` |
| `.pad(inset)` | `padding` |
| `.alignText(Align)` | `align` |
| `.rowHeight(px)` | `height` |
| `.mark(scale, inset)` | `markScale`, `markInset` |

### Three scopes

```cpp
// 1. The whole UI, from nine colours.
gui.theme.applyPalette(ui::Palette::light());

// 2. Every button, from here on.
gui.theme[ui::Part::Button].rounding = 10.0f;
gui.theme[ui::Part::Button].height   = 30.0f;

// 3. Every button in this scope.
{
    ui::StyleGuard scope(gui, ui::Part::Button,
                         ui::Style{}.fill({0.66f, 0.20f, 0.22f, 1.0f}));

    if (gui.button("Delete"))     deleteThing();
    if (gui.button("Delete all")) deleteEverything();
}

// ...and one widget: every widget takes an optional trailing Style.
if (gui.button("Remove every object", ui::Style{}.fill(danger)))
    objects.clear();
```

`StyleGuard` puts the Part back when it goes out of scope, and nests. A Style
passed to a widget applies to that call only and touches nothing else.

### Palettes

`ui::Palette` is nine colours — `window`, `titleBar`, `surface`,
`surfaceHovered`, `surfaceActive`, `accent`, `text`, `border`, `overlay` — and
`applyPalette` fans them out over every Part. `Palette::dark()` (the default)
and `Palette::light()` ship; anything else is a struct you fill in.

```cpp
ui::Palette brand = ui::Palette::dark();
brand.accent         = {0.90f, 0.40f, 0.10f, 1.0f};
brand.surfaceActive  = {0.75f, 0.33f, 0.08f, 1.0f};

gui.theme.applyPalette(brand);           // coherent base
gui.theme[ui::Part::Radio].rounding = 0.0f;   // then the exceptions
```

`applyPalette` discards per-Part edits made before it, which is the point:
apply the palette first, then the exceptions.

### Metrics

`gui.theme.metrics` holds what the whole UI shares: `rowHeight`, `itemSpacing`,
`padding` (panel border to content), `indent`, `textScale`, `scrollbarWidth`
and `disabledAlpha`.

For permanently larger text prefer raising `ContextDesc::pixelHeight`, which
rasterizes the glyphs bigger. `textScale` magnifies an already-rasterized
bitmap and gets soft well before it gets large.

Panels and their title bars are square by default. Rounding them works — the
background is built to fit — but with no antialiasing a large translucent
surface shows the corner as a stair rather than a curve.

## Disabling widgets

```cpp
gui.beginDisabled(!hasSelection);
if (gui.button("Delete selected"))
    deleteSelected();
gui.endDisabled();
```

Widgets between the two still lay out, still draw and still return their
value — they simply take no click and no <kbd>Tab</kbd>. Greying out rather
than hiding keeps the panel from reflowing as options come and go. It nests,
and `beginDisabled(false)` pushes a level that changes nothing, so a condition
needs no matching `if`.

## Building without it

`AURA_ENABLE_UI` (default `ON`) gates the module. Built with `OFF`, no
`aura3d::ui` symbol reaches the library and `engine/include/aura/UI/` is not
installed. `AURA_HAS_UI` is defined for consumers when it's on:

```cpp
#ifdef AURA_HAS_UI
#include "aura/UI/AuraUI.h"
#endif
```

## Keyboard, text and scrolling

`attachInput()` wires all of it up; nothing below needs extra plumbing.

**Text fields** read the platform's *committed text*, not the key stream, so
they are correct on every keyboard layout and through dead keys and IME —
typing `é` on a French layout, or composing it with a dead key, inserts one
character:

```cpp
static std::string name = "player";
if (gui.inputText("Name", name))
    renameThing(name);
```

Editing supports a caret and selection, <kbd>←</kbd>/<kbd>→</kbd> (hold
<kbd>Ctrl</kbd> to move by word, <kbd>Shift</kbd> to extend the selection),
<kbd>Home</kbd>/<kbd>End</kbd>, <kbd>Backspace</kbd>/<kbd>Delete</kbd>,
<kbd>Ctrl</kbd>+<kbd>A</kbd>, and typing over a selection to replace it. The
caret moves by whole characters, so multi-byte text is never split.
<kbd>Enter</kbd> commits and releases focus; <kbd>Escape</kbd> reverts the edit
and releases focus.

**Keyboard navigation** works across every widget. <kbd>Tab</kbd> and
<kbd>Shift</kbd>+<kbd>Tab</kbd> walk the focus ring in submission order and wrap
at either end; <kbd>Enter</kbd> or <kbd>Space</kbd> activates a focused button
or checkbox, and the arrow keys nudge a focused slider (<kbd>Shift</kbd> for a
coarse step). The focused widget is outlined in the style's accent colour.

Gate your own key handling on `isCapturingKeyboard()`, exactly as you gate the mouse:

```cpp
if (!gui.isCapturingKeyboard())
    handleMovementKeys();
```

**Scrolling regions** are the one thing auto-height panels cannot express —
content taller than a fixed height, clipped and reachable by wheel or scrollbar:

```cpp
// beginScroll() only ever returns false when called outside a panel and there
// is no "closed" state to skip, unlike treeNode()/collapsingHeader() below,
// so unlike those, checking its return value is optional. Every widget call
// (and endScroll() itself) already no-ops safely if that happens.
gui.beginScroll("Objects", 120.0f);
for (size_t i = 0; i < objects.size(); ++i)
    if (gui.selectable(objects[i].name, selected == i))
        selected = i;
gui.endScroll();
```

The wheel scrolls whichever region the cursor is over, so nested regions and the
panel behind them do not all move at once.

**Touch** drives the same cursor the mouse does — a tap is a click, a drag is a
drag — so panels work on Android and in a touch browser with no separate code
path. Multi-finger gestures stay available to your application on the same
`TouchListener`, since the UI's callbacks are additive rather than an exclusive
grab.

**On-screen keyboards.** `isCapturingTextInput()` is true exactly while a field is
focused, and `attachInput()` drives `IWindowManager::setTextInputEnabled()` from
it — so a soft keyboard appears on Android/iOS when a field is focused and
dismisses when it is not. Driving `newFrame(const Input&)` yourself means
forwarding that one call.

## Limits

Worth knowing before you reach for it:

- **Single-line text only.** No multi-line editor, and no clipboard: wma exposes
  no clipboard API yet, so <kbd>Ctrl</kbd>+<kbd>C</kbd>/<kbd>V</kbd> do nothing.
- **No docking.** Panels are free-floating and draggable; they do not snap or
  tab into each other.
- **Panel order is call order.** The last panel submitted draws on top and
  wins the cursor; there is no click-to-focus reordering.
- **One `Context` per thread, not re-entrant.** Same rule as the renderer it
  draws through.

One place to see all of it running: **`apps/Sandbox`** is this chapter's own
companion app, not just the "complete tool" example above. Beyond its own
"Scene" panel, it spawns/removes textured 3D objects, drives a live-animated
2D sprite, and runs every other widget in this chapter (text fields, a
drop-down, tabs, a scrolling list, tree nodes) through an "Inspector" panel.
Build and run it (`cd build/linux/release/apps/Sandbox && ./Sandbox`, then
press <kbd>Tab</kbd>) to see every pattern in this chapter at once, alongside
the camera, lighting and audio demos it already has. `tests/test_ui.cpp`
drives the module headlessly if you want its behaviour pinned down precisely
instead.

---

Next: **[13-auraui-internals.md](13-auraui-internals.md)** — how it works
inside.
