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

A loop that would rather not present an unchanged frame at all splits it:

```cpp
ui.update(deltaSeconds);          // input, focus, animation -- always
bool drew = false;
if (ui.needsDraw()) {             // the tree changed, or the renderer lost its frame
    renderer->beginFrame();
    if (renderer->frameBegun()) { // false while the swapchain is rebuilt
        renderer->beginRenderPass();
        ui.draw();
        renderer->endRenderPass();
        drew = true;
    }
    renderer->endFrame();
}
if (!drew)
    renderer->getWindowManager()->waitEvents(16);
```

A pass that draws nothing has nothing to block on, so it waits for events. Keep
the timeout finite: a frame that could not begin is retried only when the wait
returns.

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
| `ScrollView` | Measures its child unbounded on the scrolling axes; wheel, draggable bars, finger drags with a fling, `scrollIntoView()` |

A container draws nothing by default — every palette leaves `Part::Container`
transparent. Give it a surface by styling it:

```cpp
card.style().fill(theme.palette().window).rounded(10.0f);
card.layout().padding = Thickness::all(20.0f);
```

## Widgets

| Widget | State | Signals |
|---|---|---|
| `Label` | `text`, `setFontSize` (0 follows `Metrics::fontSize`), `setWrap`, `setAlign` | — |
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
auto& menu = root.overlay().open<Menu>({.anchor = button.visualBounds(),
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
| `transition` | `enter` (after its first placement, before it is first drawn) and `exit` hooks for this overlay; unset ones fall back to `OverlayLayer::setDefaultTransition()` |
| `onClosed` | Runs once it is gone, so an opener can forget its id |

Every placement flips to its opposite when the surface has no room and is then
clamped on to it, so a drop-down at the bottom of the window opens upwards
instead of off-screen.

**Closing is deferred to the next frame.** A menu item that closes the menu it
lives in is the normal case, not an edge case, and destroying the widget inside
its own click handler would pull the ground out from under the dispatch still
running through it. `close()` takes effect visually at once and structurally at
the next `update()` — unless an `exit` hook runs: the overlay then stays drawn
and inert, and `onClosed` waits, until the hook calls its `done`.

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

At shared soft-wrap boundaries, `CaretAffinity::Downstream` (the default) places
the caret on the next line; `Upstream` keeps it on the preceding line.
`byteAt()` can return affinity alongside the byte offset. Selection endpoints
are resolved on each selected line, including at UTF-8 wrap boundaries.

Layout never touches a font. It asks the shaper for a `ShapedText` — positioned
glyph quads plus the line structure — and every other question is answered from
that one object.

```cpp
const ShapedText& shaped = label.shaped();

const usize byte = shaped.byteAt(localPoint);      // hit test -> byte offset
const Rect caret = shaped.caretRect(byte);         // byte offset -> pixels
shaped.selectionRects(begin, end, out);            // one rect per line
```

`AtlasTextShaper` rasterizes each cached size separately and packs the results
into shared R8 sheets. Size views keep stable UVs; overflow can open another
sheet without invalidating retained text. `maxPages` caps cached raster sizes
and physical sheets; additional sizes use the nearest cached size.
Caret arithmetic goes through `ui::utf8`, so nothing can leave a caret inside a
multi-byte character.

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

### Touch

A finger is the same press, move and release as a mouse, with `event.kind ==
PointerKind::Touch` and no hover. `UIView` reports it that way; driving a `UIRoot`
yourself, pass the kind:

```cpp
root.pointerDown(at, PointerButton::Left, {}, PointerKind::Touch);
```

| Behaviour | Where |
|---|---|
| A drag past the slop scrolls the nearest `ScrollView` that can move that way, from anywhere in its content; the button under the finger is cancelled, not clicked | `ScrollView` |
| A quick flick coasts and slows to a stop; a finger down stops it | `ScrollView` |
| A tap focuses a `TextField` when the finger lifts, so a drag that starts on one scrolls the page and leaves the keyboard down | `TextField` |
| A mouse drag over scrolling content is left to the widget under it | |

The slop and the coasting are `ScrollPhysics`: `theme.metrics.scroll` for every view, `scrollView.setPhysics()` for one
(`resetPhysics()` goes back to the theme's).

```cpp
root.theme().metrics.scroll.touchSlop = 12.0f;                      // every view
list.setPhysics(ScrollPhysics{.flingMaxSpeed = 9000.0f, .flingFriction = 1.5f});   // this one
```

A gesture that starts without a move, such as a long press that picks something up, has no moment to take the press from
`onPointerIntercept()`. It captures through the root instead, which also keeps scroll views from taking the finger back:

```cpp
root()->claimPointer(this);   // from a timer: the press is now this widget's
```

A widget that wants a gesture of its own says which kinds of press it is interested in, and takes the gesture
from the press, before the pressed widget sees the moves. A press nobody is interested in costs nothing:

```cpp
bool interceptsPointer(PointerKind kind) const noexcept override { return kind == PointerKind::Touch; }

bool onPointerIntercept(const PointerEvent& e) override {
    if (e.kind != PointerKind::Touch || glm::distance(e.position, e.pressPosition) < 8.0f)
        return false;
    capturePointer();           // cancels the widget that had the press
    return hasPointerCapture(); // later moves arrive in onPointerMove
}
```

### Zoom

```cpp
ui.zoomChanged.connect([](f32 zoom) { save(zoom); });
ui.zoomStep(+1);                                         // 100 % -> 110 %; setZoom(1.25f) for an exact value

// Ctrl + wheel: the app's own scroll action; UIView polls the wheel, so this displaces nothing.
mouse.setScrollAction(wma::MouseAction{[&](const wma::WMAMouseScroll &scroll)
{
    if (keyboard.modifiers().ctrl && scroll.yOffset != 0.0)
        ui.zoomStep(scroll.yOffset > 0.0 ? 1 : -1);
}});
```

The wheel still reaches the widgets, so a `ScrollView` under the pointer scrolls too.

The ladder is 50, 67, 80, 90, 100, 110, 125, 150, 175, 200, 250 and 300 %. Layout is in smaller units and drawn larger,
and text is rasterized again at the new size. Every size and scale takes a rasterization size of its own, so raise
`UIViewDesc::maxFontSizes` for an application with many font sizes or a wide zoom range.

### The pointer image

`Widget::cursor()` names the image shown while the pointer is over a widget; the first widget from the hovered one
up that returns something other than `Default` wins, and a disabled one shows the arrow.

| Widget | Pointer |
|---|---|
| Every `Control` (button, check box, slider, drop-down…) | `Pointer` |
| `TextField` | `Text` |
| Window caption buttons | arrow: `control.setCursor(wma::SystemCursor::Default)` opts a `Control` out |
| Yours | override `cursor()`; a captured widget decides while it holds the pointer |

```cpp
wma::SystemCursor cursor() const noexcept override { return wma::SystemCursor::Grabbing; }
```

### Timing

| Parameter | Where | Default |
|---|---|---|
| Hover, press and check transitions | `theme.metrics.hoverSeconds` | 0.11 s |
| Tab underline travel | `theme.metrics.indicatorSeconds` | 0.16 s |
| Double click: gap, and how far the pointer may move | `root.setDoubleClickSeconds()` · `setDoubleClickSlop()` | 0.4 s · 5 px |
| Caret blink, on then off | `root.setCaretBlinkSeconds()` | 0.53 s |
| Tooltip dwell | `root.setTooltipDelay()` | 0.5 s |

Zero hover seconds makes a control change at once; zero double-click seconds turns double clicks off.

### Clipboard and undo

`TextField` copies, cuts, pastes and undoes from the keyboard: Ctrl+C, X, V, Z and Y (or Ctrl+Shift+Z), and
Ctrl+Insert, Shift+Delete and Shift+Insert. The same are calls, for a menu item or a button:

```cpp
field.copy();  field.cut();  field.paste();
field.undo();  field.redo();
const std::string picked = field.selectedText();
```

Where the text goes is `UIRoot::clipboard()`:

| Installed | Reaches |
|---|---|
| `UIView`, over a window that has one | The system clipboard (`WindowClipboard`) |
| Nothing | A clipboard in the process: fields of one application share it, nothing else sees it |
| Yours: `root.setClipboard(&mine)` | Whatever `IClipboard::text()` and `setText()` do; null puts the in-process one back |

A passphrase field (`setObscured`) is never copied or cut. A cut the clipboard did not take does not delete. A paste
has its line breaks turned to spaces and its control characters and invalid UTF-8 removed, and is cut short, at a
character, to what still fits `setMaxBytes()`. A run of typing, or of deleting, is one undo step; moving the caret ends
it, and assigning to `text` from outside forgets the history.

An application shortcut on one of these keys wins over the field's, as for every modified key.

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

From outside a widget, `UIRoot::animate()` drives any value; the step decides what moves:

```cpp
panel.setOpacity(0.0f);
root.animate(&panel, Tween{.seconds = 0.2f, .curve = easing::outCubic,
                           .step = [&panel](f32 t) { panel.setOpacity(t); panel.setTranslation({0, 12 * (1 - t)}); },
                           .done = [] { /* arrived */ }});
```

| | |
|---|---|
| `step(t)` | `0` at once, then every `update()`, the last time exactly `1` |
| Stops | `cancelAnimation(id)`, or the target destroyed: no further step, no `done` |
| `setOpacity()` / `setTranslation()` | Paint only, subtree-wide; layout keeps the slot. Hit testing and pointer coordinates follow the translation |
| `visualBounds()` | Where it is drawn — what an overlay anchors to |

Opacity multiplies each primitive; it is not a composited layer, so a child over its parent's fill lets the fill
show through while both fade.

AuraUI animates none of its own overlays. The application gives drop-downs, submenus and tooltips their motion in
one place — `apps/AuraUIDemo` fades them and slides them away from the anchor:

```cpp
root.overlay().setDefaultTransition({
    .enter = [&root](Widget& o, const OverlayDesc&) {
        root.animate(&o, Tween{.seconds = 0.18f, .step = [&o](f32 t) { o.setOpacity(t); }});
    },
    .exit = [&root](Widget& o, const OverlayDesc&, std::function<void()> done) {
        root.animate(&o, Tween{.seconds = 0.11f, .step = [&o](f32 t) { o.setOpacity(1 - t); },
                               .done = std::move(done)});
    }});
```

## Window chrome

`DefaultWindowDecoration` takes everything it draws and grabs from a `WindowChrome`; the defaults are the look it has
always had.

```cpp
auto chrome = decoration.chrome();
chrome.titleHeight = 40.0f;
chrome.buttonWidth = 52.0f;
chrome.frame = {0.10f, 0.11f, 0.14f, 1.0f};
chrome.close = ColorSet::flat({0.8f, 0.2f, 0.2f, 1.0f});
decoration.setChrome(chrome);          // applies at once
```

| Field | Meaning |
|---|---|
| `titleHeight` `buttonWidth` `titleInset` `titleGap` `titleFontSize` | The bar |
| `iconSize` | The caption icons' square; strokes scale with it |
| `border` `resizeReach` `cornerReach` | The painted resize border, and how far in the edges and corners grab |
| `frame` `titleText` `icon` `minimize` `maximize` `close` | Colours; the buttons take a `ColorSet` for rest, hover and press |

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

Open overlays follow the content, back to front, so a menu or a drop-down list is read where it sits. A modal one
is `Role::Window` with `info.modal` set, which is the bridge's cue to hide the rest. An overlay that is closing, even
while its exit still draws, is not in the tree; neither is a tooltip, whose text is the owner's `description`.

## DPI

Layout is in logical pixels at every scale: a 48-unit button is 48 units on a
200% display. Only two things use the ratio — the shaper rasterizes at
`pixelSize * scale`, and the backend multiplies vertex positions by it.
`UIView` sets it to the framebuffer-to-window ratio times
`IWindowManager::getContentScale()`, and sizes the tree to the window over the
content scale, so an Android phone at 2.625x gets a 411-unit-wide tree and
positions are divided by 2.625 on the way in. Driving a `UIRoot` yourself, set
it with `root.setScale()`.

## What it costs

| Property | How |
|---|---|
| Shared draw calls | Surfaces, borders, icons and font sizes on the same atlas sheet share one batch. Images and switches between overflow sheets add batches |
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

A floating surface — a menu, a list — styles itself from `Part::Popup`, not from a tooltip's entry: it starts out
looking the same (`Palette::overlay`), and restyling one leaves the other alone.

Register the theme entry it reads in `Theme::applyPalette` and add a `Part` for
it if none fits — see `engine/src/UI/Core/Theme.cpp` for how the existing
Parts are laid down from a `Palette`.
