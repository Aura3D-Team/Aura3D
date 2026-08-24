# 13. How AuraUI Works

This chapter is the inside of `aura3d::ui` — what happens between
`newFrame()` and `render()`, and why it's built the way it is. You don't need
any of it to use the UI; read
**[12-immediate-mode-ui.md](12-immediate-mode-ui.md)** for that. Read this if
you're adding a widget, tracking down a behaviour, or deciding whether the
design fits a change you have in mind.

The whole module is two files, and the split is strict:

```
engine/include/aura/UI/AuraUI.h    // the API, and nothing else
engine/src/UI/AuraUI.cpp           // Context::Impl -- all of the state
```

`Context` is a handle: a `unique_ptr<Impl>` and a forwarder per method. Every
piece of state below lives in `Context::Impl`, which is declared in the header
by name only and defined in the `.cpp`. That is worth one pointer hop per public
call (measured at well under 1% of a frame) and buys two things: a translation
unit that draws a checkbox parses the API and neither `FontAtlas.h` nor
`IRenderer.h` nor `<unordered_map>` -- 663 headers instead of 827, and about
half the compile time -- and changing how a widget remembers something
recompiles one file instead of every file that draws.

## What it's made of

AuraUI adds no rendering machinery. The engine already owned both halves of a
UI, and this is the layer between them:

```
   aura3d::ui::Context
        |
        |-- FontAtlas ................ glyph rasterization + packing
        |                              (Core/AuraFont — chapter 7)
        |
        '-- IRenderer::drawBatch2D ... window-pixel-space 2D geometry
                                       (implemented by every backend)
```

That is the entire dependency list, and it's why there is no per-backend UI
code. `drawBatch2D` is a pure-data call — vertices, indices, a texture handle
— so a `Context` never learns which backend it is talking to. Vulkan, OpenGL,
Metal and the software rasteriser all get identical geometry.

It also means the UI inherits chapter 7's guarantees for free: window-pixel
coordinates with `(0,0)` top-left, an orthographic projection the backend
derives itself, unlit shading, depth test off, straight alpha blending.

## The frame

```
newFrame()      latch input, resolve hot, reset layout, clear geometry
   |
   v
beginPanel()    hit-test the title bar, apply drag, emit background + title
   |            (remember the background's vertex index)
   v
widgets         each: reserve a row, run the state machine, append quads
   |
   v
endPanel()      patch the background's height, claim the mouse if hovered
   |
   v
render()        upload new glyphs, submit one drawBatch2D
```

Three kinds of state live across that boundary, and it's worth separating
them because they have very different lifetimes:

| State | Lifetime | Examples |
|---|---|---|
| Geometry | one frame | `_vertices`, `_indices` (cleared in `newFrame`) |
| Layout | one panel | `_panel.cursorY`, `_clip`, `_panel.widgetIndex` |
| Interaction | across frames | `_pointer.hot`, `_pointer.active` |
| Per-widget | until swept | `_panels`, `_textStates`, `_scrollStates`, `_widgetValues` |

The last two are the "retained" ones, and they are still only integers, floats
and a couple of strings — no widget objects, no tree. The fourth row is the one
with a lifetime question attached, which [Forgetting widgets](#forgetting-widgets)
answers.

## Widget identity

Immediate mode draws no objects to point at, so a widget has to be recognised
from one frame to the next by value alone. That value is `_idFor`:

```cpp
u32 Context::_peekId(std::string_view text) const noexcept
{
    return hashBytes(text, _panel.id) ^ (_panel.widgetIndex * kIdStride);
}

u32 Context::_idFor(std::string_view text) noexcept
{
    const u32 id = _peekId(text);
    ++_panel.widgetIndex;
    return id;
}
```

Three ingredients, each fixing a specific collision:

- **the label** — distinguishes widgets within a panel;
- **the panel id** (itself an FNV-1a of the panel title) — so `"Reset"` in two
  different panels are two different widgets;
- **a per-panel counter** — so `"Reset"` *twice in the same panel* are also
  distinct. Without it, pressing one would light up both, which is a
  surprisingly common layout.

The counter is why identity is positional as well as textual: reordering
widgets within a panel reassigns ids. In practice that only costs a frame of
hover state, because ids are consulted for interaction, never for storage.

## The interaction state machine

Two ids drive everything:

- **`_pointer.hot`** — the widget under the cursor.
- **`_pointer.active`** — the widget that owns an in-flight press.

`_behaviour(id, rect)` runs the machine for one widget and fills in an `Item`
— the widget's id, its rectangle, and every flag this frame's input produced
for it:

```cpp
struct Item {
    u32 id;  Rect rect;
    bool hovered, held, clicked;   // from _behaviour
    bool focused, activated;       // from _focusItem
};
```

`Item` is what every widget is written against, and `_item(label, height)` is
the one call that produces one: id, row, hit-test and focus in a single line.
Only the pieces that are not rows — title bars, scrollbar thumbs, tabs, open
dropdown entries — reach for `_behaviour` directly.

```
        cursor enters bounds
              |
              v
  (nothing) ---> _pointer.hot == id ---> press ---> _pointer.active == id
              ^                                          |
              |                                          | release
              '------------------------------------------'
                     clicked = (released while hovered)
```

The release rule is the one worth stating explicitly: a click is a press
**and** a release over the same widget. Pressing a button and dragging off it
before letting go cancels — and a release over a button that was never pressed
does nothing. Both are what a user expects, and neither falls out of a naive
"is the button down inside this rectangle" test. `tests/test_ui.cpp` pins all
four cases.

### Why `hot` is resolved a frame late

`newFrame` does this:

```cpp
hot = nextHot;
nextHot = 0;
```

Widgets *claim* `nextHot` as they're submitted, and the claim is unconditional
— so the **last** widget to claim it wins. Panels are drawn back to front, so
the last claimer is the topmost one, which is exactly the one the user is
pointing at.

Resolving within the same frame would instead hand the cursor to whichever
widget was submitted *first*, meaning a panel would happily respond to clicks
landing on the panel covering it. The cost of doing it properly is one frame of
latency on hover, which is imperceptible; the cost of not doing it is a UI that
misroutes clicks whenever two panels touch.

One wrinkle follows from that. On the very first frame the cursor arrives, no
one has claimed `hot` yet, so a press would be dropped:

```cpp
else if (_pointer.pressed && (_pointer.hot == id || (_pointer.hot == 0 && item.hovered)))
```

The second disjunct accepts a press from a widget that is hovered *now* when
nothing was hot before. Requiring `hot == 0` is what keeps it safe: if panels
overlap, the topmost has already claimed it, so it is non-zero and the covered
widget cannot steal the press.

### Not getting stuck

A widget that vanishes mid-press — its panel closed, an `if` stopped emitting
it — never gets to observe the release that would clear `active`. Without a
guard the UI would believe a press is in flight forever, and every later click
would be swallowed. So `_behaviour` sets `activeSubmitted` whenever the active
widget is seen, and `PointerState::beginFrame` clears `active` when it wasn't:

```cpp
if (!activeSubmitted)
    active = 0;
activeSubmitted = false;
```

`FocusState::beginFrame` drops a vanished widget's *keyboard* focus for exactly
the same reason.

## Forgetting widgets

Immediate mode has nothing to destruct. A panel closes, a branch stops emitting
a tree node, a list row scrolls out of the data — and the widget simply never
comes back. Its entry in `_widgetValues` or `_scrollStates` has nobody left to
delete it.

For a UI whose labels are written by hand that is harmless: a few dozen entries
for the life of the process. For one whose labels come from data it is a leak,
because every change of that data mints a fresh id:

```cpp
for (const Entity& e : scene)                     // "Entity 8817", "Entity 8818", ...
    if (gui.treeNode(std::format("Entity {}", e.id)))
```

So the five retained maps are `RetainedMap<T>`, which stamps every lookup with
the current frame and sweeps what has gone unasked-for. The sweep is the
dangerous half — dropping an entry silently resets whatever the user did to
that widget — so two rules keep it away from anything that would notice:

- **A map under 256 entries is never swept at all.** A UI small enough to have
  been *authored* keeps its state for the whole run, exactly as it did before
  any of this existed. This is the case almost every panel is in.
- **Above that, an entry still has to go untouched for 1024 frames** (~17s at
  60 Hz). "Not submitted" is the normal state of a widget behind a collapsed
  header or an unselected tab; those must not lose their state for being out of
  sight for a few seconds.

Entries in active use are stamped every frame, so the scene tree above keeps all
ten thousand of its nodes however long it runs — only ids nothing asks for any
more go. `tests/test_ui.cpp` pins both directions: that a submitted widget
survives a 1400-frame run past the sweep, that an abandoned one does not, and
that a small UI is never touched at all.

`RetainedMap::Slot::inserted` also replaced a flag. `PanelState` used to carry
`placed`, meaning "defaultPosition has been applied" — which is exactly "this
lookup is what created the entry", and the map already knew that.

## Geometry: one draw call

The whole UI leaves as a single `drawBatch2D`, however many panels and widgets
it contains. That is not an optimisation applied afterwards — it's the reason
two specific decisions were made.

### The solid texel

A UI mixes solid rectangles with text. Those normally come from different
textures — a white pixel for the rectangles, the glyph atlas for the text —
and a batch can carry exactly one texture. Interleaved rectangles and labels
would therefore force a batch break at every switch: roughly two draw calls
per widget.

So `FontAtlas` reserves one small always-opaque cell and hands back its UV:

```cpp
const auto solidUv = _atlas->solidTexelUv();
```

Every solid quad the UI draws points all four of its vertices at that UV.
Rectangles now sample the glyph atlas too, so no interleaving of rectangles
and glyphs can split the batch, and `_quad` is just a degenerate
`_texturedQuad`:

```cpp
void Context::_quad(const Rect& bounds, const glm::vec4& color)
{
    _texturedQuad(bounds, _solidUv, _solidUv, color);
}
```

The cell is 4×4 rather than 1×1 and the UV addresses its centre, so bilinear
filtering only ever reaches texels inside it — never a neighbouring glyph's.
It's claimed in the `Context` constructor while the atlas is still empty, where
the reservation cannot fail for want of room.

### Clipping on the CPU

Panel content is clipped by trimming quads in `_texturedQuad`, not with a
scissor rectangle. A scissor is per-draw state, so every clip change would cost
a draw call — which would give back everything the solid texel just bought.

Quads here are axis-aligned, which makes the trim exact. Clip the rectangle,
then move the UVs by the same fractions:

```cpp
const glm::vec2 uv0{lerp(uvMin.x, uvMax.x, (visible.min.x - bounds.min.x) / width),
                    lerp(uvMin.y, uvMax.y, (visible.min.y - bounds.min.y) / height)};
```

The surviving texels are untouched, so the result is pixel-identical to what a
scissor would have produced.

The clip rectangle's bottom edge is left effectively unbounded
(`kUnboundedBelow`), because a panel's height isn't known while its content is
being emitted — and never needs to be, since auto-height panels grow to fit
their content by construction. Only the horizontal edges actually reject
anything.

## Auto-height panels

A panel's background has to be drawn *behind* its content, so it must be
emitted first — but its height isn't known until `endPanel()`. Rather than
buffering the content or measuring it twice, `beginPanel` emits the background
at placeholder height and remembers where it landed:

```cpp
_panel.backgroundVertex = _vertices.size();
_quad(_panel.bounds, _style.panelBackground);
```

`endPanel` then rewrites the two bottom vertices in place:

```cpp
_vertices[_panel.backgroundVertex + 2].pos.y = bottom;
_vertices[_panel.backgroundVertex + 3].pos.y = bottom;
```

This is why the winding order in `_texturedQuad` is load-bearing and
commented as such: corners go top-left, top-right, bottom-right, bottom-left,
so indices 2 and 3 are exactly the pair that follows the content's height.

The same "hit-test where it was, draw where it is" split appears in the title
bar. Dragging is resolved *before* any geometry is emitted, so `grabBar` (the
pre-drag rectangle, used for hit-testing) and `titleBar` (post-drag, used for
drawing) are separate values. Reusing one for both leaves the title bar
trailing the panel body by a frame's worth of mouse movement — there's a
regression test for exactly that.

## Memory

`_vertices` and `_indices` are `AlignedVector`, cleared rather than freed each
frame, so a steady-state UI performs **no allocation at all**. The 32-byte
alignment matters because of what happens to the batch next: it is `memcpy`'d
into write-combined mapped device memory (Vulkan) or handed to
`glBufferSubData` (OpenGL), and both copy fastest from a source aligned to the
widest vector register. `gfx::Vertex2D` is exactly 32 bytes, so a 32-byte
aligned base aligns every vertex in the batch, not just the first — there's a
`static_assert` holding that invariant.

Glyphs reach the GPU the same way `TextOverlay`'s do: rasterized lazily on
first use, unioned into one dirty rectangle, and pushed as a single sub-image
upload in `render()` — strictly after the widgets ran (they're what rasterized
the new glyphs) and strictly before the draw that samples them.

## Adding a widget

Every widget has the same five-step shape. `button` is the smallest complete
example:

```cpp
bool Context::Impl::button(std::string_view text)
{
    if (!_panel.open)                                 // 1. refuse outside a panel
        return false;

    const Item it = _item(text, _style.rowHeight);    // 2. row + identity + input

    _quad(it.rect, _fill(it.held, it.hovered));       // 3. draw from that state

    if (it.focused)
        _focusRing(it.rect);

    _textCentered(text, it.rect);

    return it.activated;                              // 4. report
}
```

Keep to it and a new widget inherits clipping, batching, identity, keyboard
focus and the press/release semantics without doing anything itself. The parts
to be careful about:

- Call `_item` (or `_idFor`) **once** per widget — it consumes a per-panel
  counter slot, so calling it twice shifts every later widget's identity. Use
  `_peekId` when you need the id without consuming one, as `inputFloat` does.
- Report `it.activated`, not `it.clicked`, unless the widget genuinely has no
  keyboard behaviour: `activated` is "clicked, or Enter/Space while focused".
- Reach retained state through `_state(id, initial)` (or `_textStates.touch`,
  `_scrollStates.touch`, ...) and never through a bare `operator[]`: the touch
  is what tells the sweep the widget is still alive.
- A new public method needs a one-line forwarder on `Context`. If a forwarder
  ever grows a body, that logic belongs in `Impl`, beside the state it reads.
- Take edited values by reference and return *changed*, not *touched*, so
  callers can gate expensive work (chapter 12 has the pattern).
- Prefer the whole row as the hit target over the visual control. `checkbox`
  does this deliberately: a 16-pixel square is a needlessly small thing to ask
  anyone to hit.

There is a matching rule for drawing. Text is always `Style::text`, so
`_text`/`_textAt`/`_textCentered` take no colour; rectangles pick theirs with
`_fill(active, hovered)`; and one glyph walk, `walkGlyphs`, does all the UTF-8
decoding, glyph lookup and kerning that `measureText`, `_text` and
`_caretFromX` need. Reach for those rather than open-coding any of it.

## Testing it

`tests/test_ui.cpp` drives the module against a `RecordingRenderer` — an
`IRenderer` that records `drawBatch2D` calls and does nothing else. Because
everything the UI does reaches the outside world through that one call, a stub
is enough to test layout, hit-testing, the state machine and clipping end to
end, with no window, GPU or driver.

Two properties in there are worth keeping, because neither fails loudly in a
running application:

- **The UI is one batch.** If a change made rectangles stop sampling the atlas,
  the UI would still look perfectly correct while quietly costing a draw call
  per widget.
- **A click is a press and a release over the same widget.** The failure mode
  is a UI that feels subtly wrong rather than one that visibly breaks.

---

Back to the **[documentation index](README.md)**.
