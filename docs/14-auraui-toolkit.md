# 14. AuraUI — The Widget Toolkit

`aura3d::ui` is a retained widget toolkit: a tree you build once, and mutate
afterwards through its own signals and properties, rather than one rebuilt
from scratch every frame.

```cpp
#include "aura/UI/UI.hpp"          // the whole toolkit
#include "aura/Renderer/IRenderer.h"  // only where you drive the render loop
```

## The shortest program

```cpp
UIView ui(*renderer);
ui.attachInput(*renderer->getWindowManager());

auto& page = ui.root().setContent<Column>();
page.layout().padding = Thickness::all(24.0f);
page.setSpacing(12.0f);

page.add<Label>("AuraShell").setFontSize(32.0f);
page.add<Button>("Launch").clicked.connect([] { launchApplication(); });

renderer->run([&] {
    renderer->beginRenderPass();
    ui.render(deltaSeconds);
    renderer->endRenderPass();
});
```

`render()` is the whole frame. Nothing above is re-run: the tree persists, and
only what changed is re-measured, re-recorded and re-submitted.

## Building a tree

`add<W>(args...)` constructs a child in place and hands it back, so
configuration reads top-down:

```cpp
auto& row = panel.add<Row>();
row.setSpacing(8.0f);

auto& ok = row.add<Button>("OK");
ok.layout().width = Length::fill();
```

| Call | Does |
|---|---|
| `add<W>(args...)` | Construct + append, returns `W&` |
| `insert<W>(index, args...)` | Same, at a position |
| `adopt(std::unique_ptr<Widget>)` | Take an already-built subtree |
| `detach(child)` | Remove and return it, intact — for moving a subtree |
| `remove(child)` / `clearChildren()` | Destroy |
| `find("name")` | First descendant with that `setName()`, depth first |

Widgets are owned by their parent and never move, so a callback capturing
`this` stays valid for as long as the widget is in the tree.

## Layout

Two passes, and the split is what makes intrinsic sizing work at all:

```
measure(Constraints) -> desiredSize()   // "how big would you like to be?"
arrange(Rect)        -> bounds()        // "this is what you get"
```

Placement lives in a plain struct, reached through `layout()`, which marks the
tree for re-layout on the way out:

```cpp
button.layout() = {.width = Length::fill(), .margin = Thickness::all(4.0f)};
label.layout().hAlign = Alignment::Center;
```

| Field | Meaning |
|---|---|
| `width`, `height` | `Length::automatic()` / `px(n)` / `percent(n)` / `fill(weight)` |
| `margin`, `padding` | Outside the widget; inside it |
| `hAlign`, `vAlign` | `Start` · `Center` · `End` · `Stretch` (default) |
| `minWidth` … `maxHeight` | Clamps, applied after the `Length` |
| `cell` | `{row, column, rowSpan, columnSpan}` — read by `Grid` |

`Stretch` yields to an explicit `Length`: a 120px button centred in a wide row
stays 120px wide.

### Containers

```cpp
auto& row = page.add<Row>();
row.add<Button>("OK").layout().width = Length::fill();
row.add<Button>("Cancel").layout().width = Length::fill();   // an even split
```

| Container | Behaviour |
|---|---|
| `Column` / `Row` | Stack along one axis; `fill` children share the leftover by weight |
| `Grid` | Tracks of `Length`; `setColumns({automatic(), fill()})`, `addAt<W>(row, col)` |
| `Stack` | Children overlaid, each aligned in the same rectangle |
| `Spacer` | `Spacer{}` eats the leftover; `Spacer{24.0f}` is a gap along the parent's axis |
| `ScrollView` | Measures its child unbounded on the scrolling axes; wheel, draggable bars, `scrollIntoView()` |

A container draws nothing by default — every palette leaves `Part::Container`
transparent. Give it a surface by styling it:

```cpp
card.style().fill(theme.palette().window).rounded(10.0f);
card.layout().padding = Thickness::all(20.0f);
```

## Widgets

| Widget | State | Signals |
|---|---|---|
| `Label` | `text`, `setFontSize`, `setWrap`, `setAlign` | — |
| `Image` | `setTexture`, `setFit`, `setTint`, `setRadius` | — |
| `Separator` | — | — |
| `Button` | `setText`, `label()` | `clicked` |
| `CheckBox` | `checked` | `checked.changed()` |
| `RadioButton` + `RadioGroup` | `checked`, `group.select(i)` | `group.selectionChanged` |
| `Slider` | `value`, `setRange`, `setStep` | `value.changed()` |
| `ProgressBar` | `value` (0–1) | — |
| `TextField` | `text`, `setPlaceholder`, `setMaxBytes`, `setReadOnly` | `submitted`, `text.changed()` |
| `Selectable` | `selected`, `setDetail` | `activated` |
| `Dropdown` | `selected`, `setItems`, `setPlaceholder` | `selected.changed()` |
| `Menu` | `addItem`, `addSeparator`, `addSubmenu` | per-item `activated` |
| `TabView` | `current`, `addTab` | `current.changed()` |
| `CollapsingHeader` | `expanded`, `content()` | `expanded.changed()` |
| `TreeNode` | `expanded`, `selected`, `addChild` | `activated` |

Public state is a `Property<T>`: assignable, readable, and observable, with the
change signal created only if someone asks for it.

```cpp
slider.value = 0.75f;
const f32 v = slider.value;
slider.value.changed().connect([](f32 v) { exposure = v; });
```

Assigning an equal value is a no-op, so code that writes the same number every
frame wakes nothing.

Properties can also follow each other, which removes the glue callback:

```cpp
ScopedConnection link = label.text.bindFrom(
    slider.value, [](f32 v) { return std::format("{:.2f}", v); });
```

`bind()` is the same-type form. Both take the current value immediately and
follow later changes for as long as the returned connection is alive — letting
it die is how a binding is undone.

### Composition, not inheritance

A `Button` owns a `Label`; a `CheckBox` owns a `Label` and draws a box beside
it. Subclass `Widget` to add a *behaviour* the toolkit lacks — not to vary
something it already parameterises, which is what `Style` is for.

```cpp
// The read-out is a sibling the slider writes into, not a slider feature.
auto& readout = form.addAt<Label>(3, 1, "1.00");
exposure.value.changed().connect([&readout](f32 v) { readout.text = format(v); });
```

## The overlay layer

Anything that has to escape its parent's rectangle — a drop-down list, a
tooltip, a context menu, a modal dialog — is the same problem: position it
against something, draw it over everything, hit-test it before everything,
dismiss it on a click elsewhere. That is solved once, in `UIRoot::overlay()`,
and every one of those widgets is a user of it rather than its own special
case.

```cpp
auto& menu = root.overlay().open<Menu>({.anchor = button.bounds(),
                                        .placement = Placement::Below});
menu.addItem("Rename", "F2").activated.connect([&] { rename(); });
menu.addSeparator();
menu.addSubmenu("Export", [&](Menu& sub) { sub.addItem("PNG"); });
```

| `OverlayDesc` | Meaning |
|---|---|
| `anchor` | Surface-space rectangle to position against |
| `placement` | `Below` · `Above` · `Right` · `Left` · `Over` · `Cursor` · `Center` |
| `offset` | Nudge applied after placement |
| `modal` | Swallows input to everything underneath |
| `dismissOnOutsideClick` / `dismissOnEscape` | Light dismissal; both on by default |
| `onClosed` | Runs once it is gone, so an opener can forget its id |

Every placement flips to its opposite when the surface has no room and is then
clamped on to it, so a drop-down at the bottom of the window opens upwards
instead of off-screen.

**Closing is deferred to the next frame.** A menu item that closes the menu it
lives in is the normal case, not an edge case, and destroying the widget inside
its own click handler would pull the ground out from under the dispatch still
running through it. `close()` takes effect visually at once and structurally at
the next `update()`.

While an overlay is open, Tab is trapped inside the topmost one, and Escape
closes it before the focused widget sees the key.

### Tooltips

```cpp
button.setTooltip("Compile the current project");
root.setTooltipDelay(0.5f);
```

Opened by the root in the overlay layer after the pointer rests, so a tooltip
is never clipped by an ancestor and never affects layout. Inherited by
descendants that set none of their own.

## Icons

The toolkit draws its chevrons and disclosure arrows from *coverage cells* in
the glyph atlas rather than from a shape primitive:

```cpp
icon::triangle(out, *shaper(), box, icon::Direction::Down, style.text);
```

`FontAtlas::convexMask()` rasterizes a convex polygon once, taking the exact
area of the shape inside each texel — the polygon clipped against the texel's
square, then the shoelace area of what survives. Not a point sample and not
supersampling, so a diagonal edge is as smooth as the arithmetic allows.
Everything after that is one textured quad, tinted by the vertex colour, in the
same batch as the text beside it. An icon therefore costs no new draw call, no
new command type and no backend case.

## Signals

```cpp
button.clicked.connect([] { save(); });                  // connect and forget

ScopedConnection sub =
    document.changed.connect([this](auto&) { refresh(); }); // dies with `this`
```

A slot may connect or disconnect during an emit: one connected mid-emit runs on
the *next* one, one disconnected is skipped. A `Connection` that outlives its
`Signal` is inert rather than dangling.

## Text

Layout never touches a font. It asks the shaper for a `ShapedText` — positioned
glyph quads plus the line structure — and every other question is answered from
that one object.

```cpp
const ShapedText& shaped = label.shaped();

const usize byte = shaped.byteAt(localPoint);      // hit test -> byte offset
const Rect caret = shaped.caretRect(byte);         // byte offset -> pixels
shaped.selectionRects(begin, end, out);            // one rect per line
```

`AtlasTextShaper` keeps one glyph page per rasterization size, so a 32px
heading and a 12px caption are both crisp rather than one being a scaled copy
of the other. Caret arithmetic goes through `ui::utf8`, so nothing can leave a
caret inside a multi-byte character.

What it does **not** do: ligatures, mark positioning, bidi, font fallback.
Those need HarfBuzz and a font database. `ITextShaper` is an interface so they
arrive as a second implementation rather than a rewrite of every widget.

## Input

Positions in, events out. Delivery is to the deepest widget under the pointer,
then up the parent chain until a handler returns `true`.

| Handler | Notes |
|---|---|
| `onPointerDown` / `Up` / `Move` | Return `true` to consume |
| `onWheel` | Decline at the end of travel so a parent scroll view takes over |
| `onKeyDown` / `Up`, `onTextInput` | Delivered to the focused widget first |
| `onPointerEnter` / `Leave` | Notifications; the whole ancestor chain gets them |
| `onFocusIn(reason)` / `Out` | `reason` separates a click from a Tab |

```cpp
widget.capturePointer();   // every pointer event comes here, wherever the cursor is
```

Capture is what makes a drag survive leaving the control. A disabled subtree is
inert *and* opaque: a click on a greyed-out button does not fall through to the
panel behind it.

### Focus and shortcuts

```cpp
root.addShortcut(Shortcut::withCtrl(wma::KEY_S), [] { save(); });
```

Tab and Shift+Tab walk the focusable widgets in tree order, wrapping. A
combination holding Ctrl/Alt/Super is checked *before* the focused widget, so
Ctrl+S saves while a field has focus; one without is checked only after the
widget declines, so a bare Delete still edits text.

Gate the application's own input on the root:

```cpp
if (!ui.root().capturesPointer()) camera.look(mouseDelta);
if (!ui.root().capturesKeyboard()) player.move(keys);
```

`UIView` drives `setTextInputEnabled()` itself, which is what raises and lowers
a soft keyboard on Android.

## Animation

```cpp
Transition<f32> _hover{0.0f};

void onPointerEnter() override { _hover.to(1.0f, 0.12f); setAnimating(true); }
bool onTick(f32 dt) override   { invalidatePaint(); return _hover.tick(dt); }
```

`setAnimating(true)` puts the widget on the root's tick list; returning `false`
from `onTick` takes it off. A UI with nothing animating does no per-frame work
at all. Curves live in `ui::easing` — plain function pointers, no allocation.

## Accessibility

Every widget fills this in; it is a virtual with a default, not an opt-in.

```cpp
AccessibilityInfo info{};
button.accessibility(info);          // role, name, enabled, focused, checked, value…

button.setAccessibleName("Launch the application");   // for an icon-only control

const AccessibilityNode tree = ui.root().accessibilityTree();
```

`accessibilityTree()` is a platform-independent snapshot — roles, states,
bounds, nesting — with purely visual nodes folded out. An AT-SPI or UI
Automation bridge consumes it, and so does a test.

## DPI

Layout is in logical pixels at every scale: a 48-unit button is 48 units on a
200% display. Only two things use the ratio — the shaper rasterizes at
`pixelSize * scale`, and the backend multiplies vertex positions by it.
`UIView` derives it from the framebuffer and window sizes; set it yourself with
`root.setScale()`.

## What it costs

| Property | How |
|---|---|
| One draw call | Solid rectangles sample the glyph atlas' opaque cell, so surfaces, borders and text share a texture. A second font size adds one batch — not one per widget |
| No steady-state allocation | Vertex, index and command storage is retained at its high-water mark |
| Rounded corners in constant geometry | A nine-slice against the atlas' antialiased corner mask: at most eleven quads whatever the radius |
| Clipping without state changes | Quads are trimmed on the CPU, exact for axis-aligned geometry |
| Nothing rebuilt that did not change | Layout runs only when invalidated; `root.paint()` declines to re-record an unchanged tree, and the backend re-submits what it already built |

`ui.drawCallCount()` reports the last frame's batches. It is exposed because a
change that quietly broke atlas sharing would still look perfectly correct on
screen.

## Driving it without a window

`UIRoot` knows nothing about a renderer or a platform — that is `UIView`'s job.
Feed one positions and key codes and take a `DrawList`, and the whole toolkit
runs headless, which is how `tests/test_aui_*.cpp` cover layout, input, text
and painting with no GPU:

```cpp
AtlasTextShaper shaper;
UIRoot root(shaper);
root.resize({400.0f, 300.0f});

auto& button = root.setContent<Column>().add<Button>("Launch");
root.update(0.0f);                       // layout

root.pointerDown(button.bounds().center());
root.pointerUp(button.bounds().center());   // clicked has fired

DrawList list;
root.paint(list);                        // inspect list.commands()
```

## The worked example

`apps/AuraUIDemo/main.cpp` is the component gallery: one page per widget
family — buttons, selection, text, containers, overlays, tree — each with
something live to poke at. It is the toolkit's showcase and its manual test at
once. Build it with `AURA_BUILD_UIDEMO` (on by default) and run it from its
output directory. `apps/Sandbox` is the other worked example: the same toolkit
driving a live 3D scene from a docked sidebar.

## Adding a widget

Four hooks, and you only need the ones you use:

```cpp
class Toggle final : public Control {
public:
    Toggle() { _part = Part::Checkbox; }
    Property<bool> on;

protected:
    glm::vec2 measureContent(const Constraints& available) override;   // intrinsic size
    void arrangeContent(const Rect& content) override;                 // place children
    void paint(DrawList& out) override;                                // draw self
    void activate() override { on = !on.get(); }                       // clicked, Space, Enter
};
```

`Control` already does hover and press transitions, capture-on-press,
fire-on-release-inside, Space/Enter activation and the focus ring. `measure()`
and `arrange()` themselves are non-virtual: the base class applies margin,
padding, `Length` and alignment, so a widget cannot get those wrong.

Register the theme entry it reads in `Theme::applyPalette` and add a `Part` for
it if none fits — see `engine/src/UI/Core/Theme.cpp` for how the existing
Parts are laid down from a `Palette`.
