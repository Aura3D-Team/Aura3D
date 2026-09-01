#include "aura/UI/AuraUI.h"

#include "aura/Core/AuraFont/FontAtlas.h"
#include "aura/Renderer/IRenderer.h"
#include "aura/Utils/AlignedVector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ranges>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace aura3d::ui {

/**
 * @brief An axis-aligned rectangle in window pixels, (0,0) at the top-left.
 */
struct Rect {
    glm::vec2 min{0.0f}; //! Top-left corner, inclusive.
    glm::vec2 max{0.0f}; //! Bottom-right corner, exclusive.

    [[nodiscard]] constexpr float width() const noexcept { return max.x - min.x; }
    [[nodiscard]] constexpr float height() const noexcept { return max.y - min.y; }
    [[nodiscard]] constexpr glm::vec2 size() const noexcept { return max - min; }

    [[nodiscard]] constexpr bool contains(glm::vec2 p) const noexcept
    {
        return p.x >= min.x && p.x < max.x && p.y >= min.y && p.y < max.y;
    }

    /// This rectangle shrunk by @p amount on all four sides.
    [[nodiscard]] constexpr Rect inset(float amount) const noexcept
    {
        return Rect{{min.x + amount, min.y + amount}, {max.x - amount, max.y - amount}};
    }
};


/**
 * @brief Per-widget state that outlives a frame -- and forgets widgets that
 *        stop coming back.
 *
 * Immediate mode has nothing to destruct. A widget that is no longer submitted
 * simply never returns, so its caret, its scroll offset or its open flag would
 * otherwise sit in the map for the life of the Context. A UI whose labels come
 * from data ("Entity 8817") mints a fresh id every time that data changes, and
 * that is an unbounded leak, not a cache.
 *
 * So every lookup stamps the frame it happened on, and sweep() drops what has
 * gone unasked-for. Two rules keep that from costing anything a UI would
 * notice:
 *
 *  - nothing is swept at all until a map holds more entries than a UI
 *    plausibly *authors*, so a fixed widget set keeps its state for the whole
 *    run, exactly as it did before any of this existed; and
 *  - above that, an entry still has to go untouched for a long time, because
 *    "not submitted" is the normal state of a widget behind a collapsed header
 *    or an unselected tab -- and those must not lose their state for being out
 *    of sight for a few seconds.
 *
 * Entries in active use are stamped every frame, so a scene tree with ten
 * thousand nodes never loses one; only ids nothing asks for any more go.
 */
template <typename T>
class RetainedMap {
public:
    //! Below this many entries a map is authored, not generated, and is left
    //! entirely alone.
    static constexpr size_t kSweepThreshold = 256;

    //! How often the sweep runs, in frames, once a map is above that.
    static constexpr u64 kSweepInterval = 256;

    //! How long an entry survives with nothing asking for it. ~17 seconds at
    //! 60 Hz, deliberately generous: see the class comment.
    static constexpr u64 kMaxIdleFrames = 1024;

    /// One entry, and whether this very call is what created it.
    struct Slot {
        T& value;
        bool inserted;
    };

    /// Finds @p id's entry -- constructing it from @p args if it has none --
    /// and marks it live as of @p frame.
    template <typename... Args>
    [[nodiscard]] Slot touch(u32 id, u64 frame, Args&&... args)
    {
        auto entry = _entries.find(id);

        const bool inserted = entry == _entries.end();
        if (inserted)
        {
            entry = _entries.try_emplace(id, T{std::forward<Args>(args)...}, frame).first;
        }

        entry->second.touched = frame;

        return Slot{entry->second.value, inserted};
    }

    /// Drops whatever has gone idle, if this map is big enough to be worth
    /// looking at. Cheap enough to call unconditionally, every frame.
    void sweep(u64 frame)
    {
        if (_entries.size() <= kSweepThreshold || frame % kSweepInterval != 0)
            return;

        std::erase_if(_entries, [frame](const auto& entry) {
            return frame - entry.second.touched > kMaxIdleFrames;
        });
    }

private:
    struct Entry {
        T value;
        u64 touched = 0;
    };

    std::unordered_map<u32, Entry> _entries;
};

/**
 * @brief Open-addressed u32 -> u32 counter table that never deallocates.
 *
 * Backs the duplicate-label disambiguation in _idFor(). Every widget inserts
 * one entry and every beginPanel() drops the lot, which as a node-based map
 * cost one malloc and one free per widget per frame -- the UI's only
 * steady-state allocation. Stamping each slot with a generation turns clear()
 * into an integer bump and keeps the buckets warm across frames.
 *
 * Entries are never erased individually, so probing needs no tombstones.
 */
class LabelCounts
{
public:
    void clear() noexcept
    {
        //! A wrapped generation would make stale slots read as live again, so
        //! the one tick that can alias pays for a real wipe.
        if (++_generation == 0)
        {
            std::ranges::fill(_slots, Slot{});
            _generation = 1;
        }

        _live = 0;
    }

    /// How many times @p key has been seen this generation, without claiming it.
    [[nodiscard]] u32 peek(u32 key) const noexcept
    {
        if (_slots.empty())
            return 0;

        const Slot& slot = _slots[_probe(key)];
        return slot.generation == _generation ? slot.count : 0u;
    }

    /// Claims the next ordinal for @p key, returning what it was before.
    [[nodiscard]] u32 bump(u32 key)
    {
        //! Kept under a 3/4 load so probe runs stay short. Growth stops once
        //! the widest panel has been seen once, so this allocates only during
        //! warm-up.
        if ((_live + 1) * 4 > _slots.size() * 3)
            _grow();

        Slot& slot = _slots[_probe(key)];

        if (slot.generation != _generation)
        {
            slot = Slot{.key = key, .generation = _generation, .count = 0};
            ++_live;
        }

        return slot.count++;
    }

private:
    struct Slot {
        u32 key = 0;
        u32 generation = 0; //! 0 is never a live generation, so this reads empty.
        u32 count = 0;
    };

    static constexpr usize kInitialSlots = 64; //! Power of two; masked, not modulo.

    /// Index of @p key's slot, or of the first free one it probed through.
    [[nodiscard]] usize _probe(u32 key) const noexcept
    {
        const usize mask = _slots.size() - 1;

        //! The keys are already hashBytes() output, so they are well mixed;
        //! masking the low bits is enough.
        usize index = key & mask;

        while (_slots[index].generation == _generation && _slots[index].key != key)
            index = (index + 1) & mask;

        return index;
    }

    void _grow()
    {
        std::vector<Slot> live;
        live.reserve(_live);

        for (const Slot& slot : _slots)
            if (slot.generation == _generation)
                live.push_back(slot);

        _slots.assign(std::max(kInitialSlots, _slots.size() * 2), Slot{});

        for (const Slot& slot : live)
            _slots[_probe(slot.key)] = slot;
    }

    std::vector<Slot> _slots;
    u32 _generation = 1;
    u32 _live = 0;
};

namespace {

constexpr size_t kVerticesPerQuad = 4;
constexpr size_t kIndicesPerQuad = 6;

//! How far below a panel its clip rectangle reaches. A panel's height is not
//! known while its content is emitted, and auto-height panels never need it.
constexpr float kUnboundedBelow = 1.0e6f;

constexpr float kScrollStep = 48.0f;

constexpr u32 kHashOffsetBasis = 2166136261u;

//! Stride between successive widget ids within one panel; the golden-ratio
//! constant, so consecutive counter values scatter rather than cluster.
constexpr u32 kIdStride = 0x9e3779b9u;

//! Bytes an inputFloat() accepts, one short of its formatting buffer.
constexpr size_t kNumericMaxBytes = 31;

//! A rounded corner costs one axis-aligned span per pixel row of its cap (see
//! Impl::_roundedQuad), which is only cheap while the radius stays small.
//! WidgetStyle::rounding is clamped here as well as to half the shorter side,
//! so "very round" is a pill rather than a bill.
constexpr float kMaxRounding = 24.0f;

[[nodiscard]] constexpr u32 hashBytes(std::string_view text, u32 seed) noexcept
{
    constexpr u32 kPrime = 16777619u;

    u32 hash = seed;
    for (const char c : text)
    {
        hash ^= static_cast<u32>(static_cast<u8>(c));
        hash *= kPrime;
    }
    return hash;
}

[[nodiscard]] constexpr float lerp(float a, float b, float t) noexcept
{
    return a + (b - a) * t;
}

//! The overlap of two rectangles. Empty (zero or negative extent) when they
//! don't overlap, which every consumer treats as "nothing survives".
[[nodiscard]] constexpr Rect intersect(const Rect& a, const Rect& b) noexcept
{
    return Rect{{std::max(a.min.x, b.min.x), std::max(a.min.y, b.min.y)},
                {std::min(a.max.x, b.max.x), std::min(a.max.y, b.max.y)}};
}

[[nodiscard]] constexpr bool isContinuationByte(char c) noexcept
{
    return (static_cast<u8>(c) & 0xC0u) == 0x80u;
}

[[nodiscard]] constexpr size_t previousBoundary(std::string_view text, size_t offset) noexcept
{
    if (offset == 0)
        return 0;

    --offset;
    while (offset > 0 && isContinuationByte(text[offset]))
        --offset;

    return offset;
}

[[nodiscard]] constexpr size_t nextBoundary(std::string_view text, size_t offset) noexcept
{
    const size_t size = text.size();
    if (offset >= size)
        return size;

    ++offset;
    while (offset < size && isContinuationByte(text[offset]))
        ++offset;

    return offset;
}

[[nodiscard]] constexpr bool isWordSeparator(char c) noexcept
{
    return c == ' ' || c == '\t';
}

[[nodiscard]] constexpr size_t previousWord(std::string_view text, size_t offset) noexcept
{
    while (offset > 0 && isWordSeparator(text[previousBoundary(text, offset)]))
        offset = previousBoundary(text, offset);

    while (offset > 0 && !isWordSeparator(text[previousBoundary(text, offset)]))
        offset = previousBoundary(text, offset);

    return offset;
}

[[nodiscard]] constexpr size_t nextWord(std::string_view text, size_t offset) noexcept
{
    const size_t size = text.size();

    while (offset < size && !isWordSeparator(text[offset]))
        offset = nextBoundary(text, offset);

    while (offset < size && isWordSeparator(text[offset]))
        offset = nextBoundary(text, offset);

    return offset;
}

//! Appends @p codepoint to @p out as UTF-8. Surrogates and out-of-range values
//! are dropped rather than encoded, so @p out stays valid UTF-8.
void appendUtf8(std::string& out, char32_t codepoint)
{
    if (codepoint > 0x10FFFFu || (codepoint >= 0xD800u && codepoint <= 0xDFFFu))
        return;

    const auto emit = [&out](u32 byte) { out.push_back(static_cast<char>(byte)); };

    if (codepoint < 0x80u)
    {
        emit(codepoint);
    }
    else if (codepoint < 0x800u)
    {
        emit(0xC0u | (codepoint >> 6));
        emit(0x80u | (codepoint & 0x3Fu));
    }
    else if (codepoint < 0x10000u)
    {
        emit(0xE0u | (codepoint >> 12));
        emit(0x80u | ((codepoint >> 6) & 0x3Fu));
        emit(0x80u | (codepoint & 0x3Fu));
    }
    else
    {
        emit(0xF0u | (codepoint >> 18));
        emit(0x80u | ((codepoint >> 12) & 0x3Fu));
        emit(0x80u | ((codepoint >> 6) & 0x3Fu));
        emit(0x80u | (codepoint & 0x3Fu));
    }
}

//! Appends @p value to @p out in the fixed three-decimal form every numeric
//! widget displays.
void appendFloat(std::string& out, float value)
{
    std::array<char, 64> formatted{};

    const int written = std::snprintf(formatted.data(), formatted.size(), "%.3f",
                                      static_cast<double>(value));
    if (written <= 0)
        return;

    out.append(formatted.data(), std::min(static_cast<size_t>(written), formatted.size() - 1));
}

/**
 * @brief Walks @p text's glyphs left to right, the one place UTF-8 decoding,
 *        glyph lookup and kerning live.
 *
 * @param visit Called as @c visit(glyph, penX, offsetAfter) for each glyph
 *        that exists in @p atlas: @c penX is the pen position it is drawn at
 *        relative to the text's origin (kerning already applied) and
 *        @c offsetAfter the byte offset just past its codepoint.
 * @return The pen's total advance, i.e. the text's width in pixels.
 */
/**
 * @brief Moves @c [split,end) to sit at @p first, sliding @c [first,split) up.
 *
 * @p end rather than the container's size: the vertex buffer keeps its
 * high-water mark across frames, so only a prefix of it is live. @p scratch
 * parks the tail while the head slides and is owned by the caller so that
 * steady-state frames allocate nothing.
 */
template <typename T, typename Scratch>
void rotateTailToFront(T* data, size_t first, size_t split, size_t end, Scratch& scratch)
{
    if (split == first || split == end)
        return;

    scratch.assign(data + split, data + end);
    std::move_backward(data + first, data + split, data + end);
    std::copy(scratch.begin(), scratch.end(), data + first);
}

template <typename Visit>
float walkGlyphs(FontAtlas& atlas, std::string_view text, float scale, Visit&& visit)
{
    float penX = 0.0f;
    char32_t previous = 0;

    size_t offset = 0;
    while (offset < text.size())
    {
        const char32_t codepoint = decodeUtf8(text, offset);
        if (codepoint == 0)
            break;

        const GlyphInfo* glyph = atlas.glyph(codepoint);
        if (!glyph)
            continue;

        if (previous != 0)
            penX += atlas.kerning(previous, codepoint) * scale;

        visit(*glyph, penX, offset);

        penX += glyph->advance * scale;
        previous = codepoint;
    }

    return penX;
}

} // namespace

/**
 * @brief Every piece of the UI's state, and every operation on it.
 *
 * Context is a handle; this is the object. Splitting them keeps AuraUI.h down
 * to the API -- see the note on Context::Impl there -- and means the layout
 * cursors, the retained maps and the glyph atlas can change without recompiling
 * anything that merely draws a button.
 *
 * The methods above @c private: are the ones Context forwards to, one for one,
 * and are documented in the header rather than repeated here.
 */
class Context::Impl {
public:
    Impl(IRenderer* renderer, const ContextDesc& desc, Theme& theme);
    ~Impl();

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    void attachInput(wma::IWindowManager& windowManager);
    void newFrame() noexcept;
    void newFrame(const Input& input) noexcept;
    void render();

    [[nodiscard]] bool beginPanel(std::string_view title, glm::vec2 defaultPosition, float width);
    void endPanel();

    void label(std::string_view text, const Style& patch);
    bool button(std::string_view text, const Style& patch);
    bool checkbox(std::string_view text, bool& value, const Style& patch);
    bool sliderFloat(std::string_view text, float& value, float min, float max,
                     const Style& patch);
    bool inputText(std::string_view label, std::string& value, size_t maxBytes,
                   const Style& patch);
    bool inputFloat(std::string_view label, float& value, const Style& patch);
    bool dropdown(std::string_view label, int& index, std::span<const std::string_view> items,
                  const Style& patch);
    bool radioButton(std::string_view label, int& value, int buttonValue, const Style& patch);
    [[nodiscard]] bool collapsingHeader(std::string_view label, bool defaultOpen,
                                        const Style& patch);
    [[nodiscard]] bool treeNode(std::string_view label, bool defaultOpen, const Style& patch);
    void treePop() noexcept;
    bool selectable(std::string_view label, bool selected, const Style& patch);

    [[nodiscard]] bool beginScroll(std::string_view id, float height, const Style& patch);
    void endScroll();
    [[nodiscard]] bool beginTabBar(std::string_view id);
    [[nodiscard]] bool tabItem(std::string_view label, const Style& patch);
    void endTabBar() noexcept;
    void tooltip(std::string_view text, const Style& patch);

    void sameLine() noexcept;
    void setNextItemWidth(float width) noexcept;
    void separator(const Style& patch);
    void spacing(float pixels) noexcept;

    void setKeyboardFocusHere() noexcept { _focus.requestNext = true; }

    [[nodiscard]] bool isCapturingKeyboard() const noexcept { return _capture.keyboard; }
    [[nodiscard]] bool isCapturingTextInput() const noexcept { return _capture.textInput; }
    [[nodiscard]] bool isCapturingMouse() const noexcept { return _capture.mouse; }

    void beginDisabled(bool disabled) { _disabledStack.push_back(disabled || isDisabled()); }

    void endDisabled() noexcept
    {
        if (!_disabledStack.empty())
            _disabledStack.pop_back();
    }

    [[nodiscard]] bool isDisabled() const noexcept
    {
        return !_disabledStack.empty() && _disabledStack.back();
    }

    [[nodiscard]] glm::vec2 measureText(std::string_view text);

private:
    /**
     * @brief One widget's identity, geometry, resolved style, and everything
     *        this frame's input did to it.
     *
     * This is what a base class would be if immediate mode had objects to
     * derive from. There is no widget to inherit -- a widget exists for the
     * length of one call -- so the shared part is a value every widget is
     * handed, and _widget() below is its constructor and destructor.
     */
    struct Item {
        u32 id = 0;
        Rect rect{};

        //! The Part's entry in the theme with the call's Style patch applied.
        //! Held by value: a widget reads its colours and metrics from here
        //! rather than reaching for the theme, and nothing has to keep a
        //! resolved style alive for exactly as long as the widget using it.
        WidgetStyle style{};

        bool valid = false;     //! False outside a panel: the widget draws nothing.
        bool hovered = false;   //! Cursor is over the widget right now.
        bool held = false;      //! Widget owns the ongoing press.
        bool clicked = false;   //! Press was released over the widget this frame.
        bool focused = false;   //! Widget holds keyboard focus this frame.
        bool activated = false; //! Clicked, or Enter/Space while focused.

        explicit operator bool() const noexcept { return valid; }
    };

    /// How one widget differs from a plain full-width row.
    struct ItemSpec {
        //! Row height. Zero takes the Part's, or Metrics::rowHeight.
        float height = 0.0f;

        //! Narrows the row to leave room for a label drawn *after* it, as
        //! inputText() and dropdown() do.
        bool trailingLabel = false;
    };

    /**
     * @brief The skeleton every interactive widget runs.
     *
     * Reserves the row, resolves identity and style, hit-tests, runs the focus
     * machinery, then hands @p body an Item and draws the focus ring after it.
     * What is left in a widget is only what makes it that widget -- and no
     * widget can forget the guard, the ring, or the tab order, because none of
     * them writes those.
     *
     * @return Whatever @p body returned, or false outside a panel.
     */
    template <typename Body>
    bool _widget(Part part, std::string_view label, const Style& patch, const ItemSpec& spec,
                 Body&& body)
    {
        const Item item = _item(part, label, spec, patch);
        if (!item)
            return false;

        const bool result = body(item);
        _ring(item);

        return result;
    }

    template <typename Body>
    bool _widget(Part part, std::string_view label, const Style& patch, Body&& body)
    {
        return _widget(part, label, patch, ItemSpec{}, std::forward<Body>(body));
    }

    /// Reserves the next row and resolves identity, style, hit-testing and
    /// focus for one widget. Use _widget() rather than calling this directly;
    /// the begin/end widgets, which have no single body, are the exception.
    [[nodiscard]] Item _item(Part part, std::string_view label, const ItemSpec& spec,
                             const Style& patch);

    /// Runs the hot/active state machine for @p id over @p rect without
    /// entering it into the tab order. For the parts that are not rows: title
    /// bars, scrollbar thumbs, tabs, open dropdown entries.
    [[nodiscard]] Item _behaviour(u32 id, const Rect& rect, const WidgetStyle& style) noexcept;

    /// Enters @p item into this frame's tab order, applies click-to-focus and
    /// fills in its focused/activated flags. Called in submission order, which
    /// is what defines the tab order -- there is no widget tree to walk.
    void _focusItem(Item& item) noexcept;

    //! The value snapshot Escape reverts to is taken by inputText() on the
    //! frame it observes the gain, not here: only the field knows what its own
    //! revertible state is.
    void _setFocus(u32 id) noexcept;
    void _clearFocus(u32 id) noexcept;

    //! True when @p key was pressed or repeated this frame with no modifier but
    //! Shift (Shift+Left is still Left; Ctrl+A is not plain A).
    [[nodiscard]] bool _keyPressed(wma::Key key) const noexcept;

    //! Everything both newFrame() overloads do once _input holds the frame's
    //! events: age the retained maps, then reset the frame's own state.
    void _beginFrame() noexcept;

    /// Reserves the next full-width row inside the current panel.
    [[nodiscard]] Rect _nextRow(float height) noexcept;

    /// Identity a widget keeps across frames: its label, its panel, and how
    /// many widgets in the panel have already used that label, so same-labelled
    /// widgets don't collide. Consumes one occurrence, so call it exactly once
    /// per widget.
    [[nodiscard]] u32 _idFor(std::string_view text);

    //! The id _idFor() would hand out next, without consuming the occurrence.
    //! For inputFloat(), which needs its state before delegating to inputText().
    [[nodiscard]] u32 _peekId(std::string_view text) const noexcept;

    //! The one retained value widgets that need exactly one share: open/closed
    //! for headers, tree nodes and dropdowns, selected index for tab bars.
    //! Ids cannot collide across kinds, so one map serves all of them.
    [[nodiscard]] u32& _state(u32 id, u32 initial);

    /// Flips @p item's retained open flag when it is activated and hands the
    /// flag back, so a caller that also closes it -- a drop-down picking an
    /// entry -- can. The whole of what a header, a tree node and a drop-down
    /// have in common.
    [[nodiscard]] u32& _toggle(const Item& item, bool defaultOpen);

    /// True when the cursor is inside @p bounds and inside the active clip.
    [[nodiscard]] bool _hovering(const Rect& bounds) const noexcept;

    /// @p part's style as the theme currently has it.
    [[nodiscard]] const WidgetStyle& _look(Part part) const noexcept { return _theme[part]; }

    /// As above with @p patch applied over it. An empty patch -- the common
    /// case, since the parameter defaults to one -- costs the check and no copy.
    [[nodiscard]] WidgetStyle _look(Part part, const Style& patch) const
    {
        return patch.empty() ? _theme[part] : patch.over(_theme[part]);
    }

    /// Row height @p style asks for, falling back to the shared metric.
    [[nodiscard]] float _rowHeight(const WidgetStyle& style) const noexcept
    {
        return style.height.value_or(_theme.metrics.rowHeight);
    }

    /// Baseline-to-baseline distance at the current text scale, in pixels.
    [[nodiscard]] float _lineHeight() const noexcept;

    //! Byte offset of the caret nearest @p x within @p text drawn at @p originX.
    [[nodiscard]] size_t _caretFromX(std::string_view text, float originX, float x);

    //! X offset of the caret at byte @p offset, relative to the text's origin.
    [[nodiscard]] float _xFromCaret(std::string_view text, size_t offset);

    /// Appends a solid quad, clipped to the active clip rectangle.
    void _quad(const Rect& bounds, const glm::vec4& color);

    /**
     * @brief A solid rectangle with rounded corners.
     *
     * Built from axis-aligned spans -- one for the middle, one per pixel row of
     * each cap -- so every piece goes through the same clipped quad path as
     * everything else and the UI stays a single batch. Radii are small, which
     * is what makes that affordable; @ref kMaxRounding keeps them that way.
     */
    void _roundedQuad(const Rect& bounds, float radius, const glm::vec4& color);

    /// Draws one component's background: its border, then its fill inside it.
    /// Every filled surface in the UI goes through here.
    void _surface(const Rect& bounds, const WidgetStyle& style, const glm::vec4& fill,
                  const glm::vec4& border);

    /// _surface() for a widget, picking the colours for its state. @p active is
    /// whatever "active" means to that widget: held, focused, open, selected.
    void _paint(const Item& item, bool active);

    /// Appends one textured quad, trimmed to @c _clip on the CPU rather than
    /// with a scissor rectangle (drawBatch2D() is one batch with no per-command
    /// state, so a scissor would mean one draw call per clip change).
    void _texturedQuad(Rect bounds, glm::vec2 uvMin, glm::vec2 uvMax, glm::vec4 color);

    /// Text, top-left at @p origin.
    void _text(std::string_view text, glm::vec2 origin, const glm::vec4& color);

    /// Text at @p x, centred vertically in @p bounds.
    void _textAt(std::string_view text, float x, const Rect& bounds, const glm::vec4& color);

    /// Text inside @p bounds, inset and aligned as @p style says. The one call
    /// that turns WidgetStyle::align into pixels.
    void _label(std::string_view text, const Rect& bounds, const WidgetStyle& style);

    /// Draws the focus outline, if @p item has focus. Called by _widget().
    void _ring(const Item& item);

    /// Draws the tick box a checkbox or radio button shows at the left of its
    /// row, filled when @p on, and returns it so the caller can place a label
    /// beside it. Shape, size and mark all come from the item's style, which is
    /// the entire difference between a check box and a radio dot.
    [[nodiscard]] Rect _marker(const Item& item, bool on);

    /// Draws the open/closed glyph a header or tree node carries, and returns
    /// the x its label starts at.
    [[nodiscard]] float _leadingGlyph(const Item& item, std::string_view glyph);

    /// Moves the panel's background in front of its content, now that
    /// endPanel() knows how tall it is. See the comment on the definition.
    void _insertPanelBackground();

    /// Replays _overlayQuads into the batch; called at the end of render().
    void _flushOverlays();

    /// Pushes the atlas' pending dirty rectangle to the GPU, if any.
    void _uploadAtlasChanges();

    /// Extends @ref _quadIndices to cover @p quads. Only ever grows.
    void _growQuadIndices(size_t quads);

    /// Claims the next four vertices of @ref _vertices, growing it if needed.
    [[nodiscard]] gfx::Vertex2D* _quadSlot();

    IRenderer* _renderer;
    std::unique_ptr<FontAtlas> _atlas;
    TextureHandle _atlasTexture;
    glm::vec2 _solidUv{0.0f}; //! Opaque atlas texel every solid quad samples.
    bool _usable = false;     //! False when the atlas or its texture is missing.

    //! The Context's own public member, not a copy: an edit to gui.theme has to
    //! reach the very next widget, and a copy here would silently shadow it.
    Theme& _theme;

    //! One entry per open beginDisabled(), each already folded with the level
    //! outside it, so nesting needs no counting and isDisabled() is a peek.
    std::vector<bool> _disabledStack;

    //! Retained across frames and over-aligned, like TextOverlay's: memcpy'd
    //! into mapped device memory / glBufferSubData every frame, and a
    //! 32-byte-aligned base keeps every 32-byte Vertex2D individually aligned.
    static_assert(sizeof(gfx::Vertex2D) == 32,
                  "Vertex2D must stay 32 bytes for the aligned batch storage "
                  "below to align every vertex, not merely the array's base.");

    /*
     * Sized to the high-water mark and never shrunk; _vertexCount is how much
     * of it this frame has filled. A vector's push_back() re-checks capacity
     * per element, which on the emission path is four checks and four size
     * bumps per quad for a bound the caller already knows -- _quadSlot() takes
     * one check for the four.
     */
    AlignedVector<gfx::Vertex2D> _vertices;
    size_t _vertexCount = 0;
    /*
     * The index stream is not data. Every quad is four vertices and six
     * indices, so index[6q + k] = 4q + {0,1,2,2,3,0}[k]: a function of the
     * slot, never of what is in it. Built once to the high-water mark and
     * reused verbatim, which takes six pushes per quad out of the emission
     * path and makes _insertPanelBackground's renumbering of it unnecessary --
     * rotating whole quads changes which quad sits in a slot, and the slot's
     * indices are the same either way.
     */
    AlignedVector<u32> _quadIndices;

    //! Scratch for _insertPanelBackground()'s rotate. A member rather than a
    //! local so its capacity survives the frame and the move never allocates.
    AlignedVector<gfx::Vertex2D> _vertexScratch;

    //! Frames since construction. Only RetainedMap reads it, and only to
    //! decide what nothing has asked for in a while.
    u64 _frame = 0;

    Input _input{};        //! Latched at newFrame(), constant for the frame.
    Input _pendingInput{}; //! Written by attachInput()'s callbacks.

    //! Non-owning, set by attachInput(). The mouse is polled for its position;
    //! the window drives isCapturingTextInput(), so a soft keyboard is raised
    //! only while a field is focused. Both must outlive this object.
    wma::MouseListener* _mouse = nullptr;
    wma::IWindowManager* _window = nullptr;

    /// One attachInput() subscription, per device. Kept so ~Context() can
    /// withdraw them (every wma callback captures @c this). A vector because
    /// attachInput() may run more than once -- ui::InputRouter calls it per
    /// UI-enabled mode.
    struct Attachment {
        wma::InputContextId keys{};
        wma::InputContextId pointer{};
        wma::InputContextId touch{};
    };

    std::vector<Attachment> _attachments;

    /// The pointer, and which widget it currently owns.
    struct PointerState {
        //! Resolved one frame late: the last widget to claim the cursor wins,
        //! and that is the topmost, since panels draw back to front. Resolving
        //! within the frame would favor whichever was submitted first.
        u32 hot = 0;     //! Under the cursor, from last frame's claims.
        u32 nextHot = 0; //! Claimed this frame; becomes @c hot next frame.

        //! Whether @c nextHot was claimed by deferred geometry. Deferred is
        //! what draws on top, so once it claims the cursor nothing submitted
        //! after it may take the claim back. See _behaviour().
        bool nextHotOverlay = false;

        u32 active = 0; //! Owns the ongoing press, if any.

        //! Whether the active widget was submitted last frame. One that
        //! vanishes mid-press never observes the release that would otherwise
        //! clear @c active, and would swallow every later click.
        bool activeSubmitted = false;

        bool previousDown = false; //! Button state as of the previous frame.
        bool pressed = false;      //! Button went down this frame.

        //! A finger is down, so the cursor comes from the touch stream rather
        //! than the (stale) polled mouse position.
        bool touchActive = false;

        //! Cursor-to-corner offset captured when a panel drag begins, so the
        //! panel follows the grab point rather than snapping its corner to it.
        glm::vec2 dragOffset{0.0f};

        //! Advances the press edge, the hot claim and the active widget into
        //! the frame about to be built.
        void beginFrame(bool mouseDown) noexcept
        {
            pressed = mouseDown && !previousDown;
            previousDown = mouseDown;

            hot = nextHot;
            nextHot = 0;
            nextHotOverlay = false;

            if (!activeSubmitted)
                active = 0;
            activeSubmitted = false;
        }
    };

    PointerState _pointer{};

    /// Keyboard focus, and this frame's tab order. @c current persists across
    /// frames like panel positions do; the rest is traversal bookkeeping
    /// _focusItem() builds as widgets are emitted, since immediate mode
    /// discovers tab order only in submission order.
    struct FocusState {
        u32 current = 0;
        //! Focus at the end of the previous frame. A widget compares against
        //! this to detect the frame it *gains* focus, when a text field
        //! snapshots its value for Escape to revert to.
        u32 lastFrame = 0;

        u32 previous = 0; //! Focusable before @c current: what Shift+Tab takes.
        u32 first = 0;    //! First of the frame, so Tab from the last wraps.
        u32 last = 0;     //! Last of the frame, so Shift+Tab from the first wraps.

        //! Set by Tab (or setKeyboardFocusHere()) and by Shift+Tab; the next
        //! focusable submitted claims focus and clears the request.
        bool requestNext = false;
        bool requestPrev = false;
        //! Shift+Tab landed on the frame's first focusable, which has nothing
        //! before it; render() wraps focus to the last one instead.
        bool wrapToLast = false;

        bool submitted = false; //! Whether @c current was emitted this frame.
        //! Whether @c current has been passed in submission order; Tab only
        //! grants focus after it. See _focusItem().
        bool passed = false;

        //! Drops focus held by a widget that stopped being emitted, then clears
        //! the traversal bookkeeping for the frame about to be built.
        void beginFrame() noexcept
        {
            //! Same reasoning as activeSubmitted above: a widget no longer
            //! emitted never observes anything again.
            if (current != 0 && !submitted)
                current = 0;
            submitted = false;

            //! After the vanish check, so traversal never navigates relative to
            //! a widget that is no longer being emitted.
            lastFrame = current;

            //! Tab grants focus to the first focusable submitted *after* the
            //! current one; with nothing focused, the frame's first takes it.
            passed = current == 0;

            previous = 0;
            first = 0;
            last = 0;
        }
    };

    FocusState _focus{};

    /// What the UI consumed. Each flag pairs a build-time accumulator with the
    /// value isCapturingMouse() and friends publish, so those queries give the
    /// same answer anywhere in the game loop.
    struct CaptureState {
        bool mouse = false;
        bool keyboard = false;
        bool textInput = false;

        bool mouseThisFrame = false;
        bool textInputThisFrame = false;
    };

    CaptureState _capture{};

    /// What a text field keeps between frames.
    struct TextState {
        size_t caret = 0;  //! Byte offset, always on a UTF-8 boundary.
        size_t anchor = 0; //! Selection anchor; equal to @c caret selects nothing.

        //! Horizontal scroll within the field, so a caret past the right edge
        //! stays visible without the string being truncated.
        float scrollX = 0.0f;

        //! The value as it was when the field took focus, for Escape to revert.
        std::string original;
    };

    RetainedMap<TextState> _textStates;

    //! An inputFloat()'s live text, kept as typed rather than reformatted from
    //! the bound float. Its own map, not a TextState field: inputFloat() hands
    //! this string to inputText() as the value to edit, and a value that
    //! aliased the very TextState inputText() mutates would be an invariant
    //! waiting to be broken.
    RetainedMap<std::string> _numericBuffers;

    //! Runs one frame of editing on @p value for the focused field. Declared
    //! here rather than with the other helpers because it takes a TextState,
    //! which is defined just above.
    bool _editText(TextState& state, std::string& value, size_t maxBytes);

    /// What a scroll region keeps between frames.
    struct ScrollState {
        float offset = 0.0f;
        //! Measured at the previous endScroll(): a region's content height is
        //! not known until it has been emitted once.
        float contentHeight = 0.0f;
    };

    RetainedMap<ScrollState> _scrollStates;
    RetainedMap<u32> _widgetValues; //! See _state().

    /// What a panel keeps between frames. Whether defaultPosition still
    /// applies is not stored: RetainedMap reports it as Slot::inserted.
    struct PanelState {
        glm::vec2 position{0.0f};
    };

    RetainedMap<PanelState> _panels;

    /*
     * How many widgets in the panel being built have already used each label
     * hash. Counting occurrences of the label rather than position in the panel
     * is what keeps a widget's id -- and every retained value stored under it:
     * open flags, carets, scroll offsets, keyboard focus -- alive across a
     * frame where an `if` above it started or stopped emitting. Positional ids
     * silently re-key everything below such a branch.
     *
     * Cleared per panel rather than destroyed, so the steady state allocates
     * nothing.
     */
    LabelCounts _labelCounts;

    //! The panel between beginPanel() and endPanel(). One object answers both
    //! "is a panel being built" and "which".
    struct CurrentPanel {
        u32 id = 0;
        bool open = false;
        Rect bounds{};        //! Grows downwards as widgets are added.
        float cursorY = 0.0f; //! Top of the next row, in window pixels.

        //! Where this panel's geometry starts. endPanel() emits the background
        //! at the end and rotates it back to here. See _insertPanelBackground().
        size_t vertexStart = 0;

        float indent = 0.0f; //! Left inset, grown by each open treeNode().
        u32 treeDepth = 0;   //! Open treeNode()s, so treePop() undoes its own indent.

        //! The row in progress, and the widget most recently placed in it.
        //! sameLine() sets @c packNext so the next widget reuses the row rather
        //! than starting one; tooltip() reads @c lastWidget for its anchor.
        float rowTop = 0.0f;
        float rowHeight = 0.0f;
        Rect lastWidget{};
        bool packNext = false;

        //! Width setNextItemWidth() asked for, consumed by the next row. Zero
        //! means "fill the remaining width".
        float nextWidth = 0.0f;
    };

    CurrentPanel _panel{};

    /// One open beginScroll() region.
    struct ScrollFrame {
        u32 id = 0;
        Rect viewport{};         //! Visible area, in window pixels.
        Rect savedClip{};        //! Clip to restore at endScroll().
        float contentTop = 0.0f; //! cursorY at entry, for measuring content.
        float savedIndent = 0.0f;
    };

    //! Open regions, innermost last. A vector rather than one member so they
    //! can nest, which a list inside a settings pane naturally does.
    std::vector<ScrollFrame> _scrollStack;

    /// The tab bar between beginTabBar() and endTabBar().
    struct TabBar {
        u32 id = 0;
        u32 index = 0; //! Which tab within the bar is next.
        bool open = false;
        //! Left edge of the next tab. Tabs are fitted to their labels and
        //! packed left to right, so the row is walked rather than divided.
        float cursorX = 0.0f;

        //! The bar's own row, held here rather than read from the panel: the
        //! selected tab's *contents* are emitted between one tabItem() and the
        //! next, so by the time the second one runs the panel's cursor has
        //! moved past them and every tab after the first would be drawn beside
        //! whatever the first tab last emitted.
        float rowTop = 0.0f;
        float rowHeight = 0.0f;
    };

    TabBar _tabBar{};

    //! Geometry that must draw over everything else: an open dropdown list, a
    //! tooltip. Emitted mid-panel but drawn on top of later widgets -- with no
    //! depth test, deferring to render() is what puts them last without a
    //! second batch. The clip in force is carried along so a deferred quad is
    //! trimmed exactly as it would have been in place.
    struct DeferredQuad {
        Rect bounds{};
        glm::vec2 uvMin{0.0f};
        glm::vec2 uvMax{0.0f};
        glm::vec4 color{1.0f};
        Rect clip{};
    };

    std::vector<DeferredQuad> _overlayQuads;
    bool _deferring = false;

    //! Content clip for the current panel. Bottom edge left effectively
    //! unbounded: a panel's height isn't known until endPanel(), and never
    //! needs to be, since auto-height panels grow to fit their content.
    Rect _clip{};

    //! Reused caption buffer (e.g. "Exposure: 1.250"). Cleared rather than
    //! rebuilt each frame, so the steady state never reallocates.
    std::string _caption;
};

Context::Impl::Impl(IRenderer* renderer, const ContextDesc& desc, Theme& theme)
    : _renderer(renderer)
    , _theme(theme)
{
    FontAtlasDesc atlasDesc{};
    atlasDesc.width = desc.atlasSize;
    atlasDesc.height = desc.atlasSize;
    atlasDesc.pixelHeight = desc.pixelHeight;

    if (!desc.fontPath.empty())
    {
        auto loaded = FontAtlas::fromFile(desc.fontPath, atlasDesc);
        if (loaded)
            _atlas = std::move(*loaded);
        else
            INK_WARN << loaded.error() << "; falling back to the embedded bitmap font";
    }

    if (!_atlas)
        _atlas = FontAtlas::builtinBitmap(atlasDesc);

    const auto solidUv = _atlas->solidTexelUv();
    if (!solidUv)
    {
        INK_ERROR << "AuraUI: the glyph atlas is too small to reserve a solid texel";
        return;
    }
    _solidUv = *solidUv;

    _atlasTexture = _renderer->createDynamicTexture(_atlas->width(), _atlas->height());
    if (!isValidHandle(_atlasTexture))
    {
        INK_ERROR << "AuraUI: could not allocate the glyph atlas texture";
        return;
    }

    _usable = true;
}

Context::Impl::~Impl()
{
    if (!_window)
        return;

    wma::KeyboardListener& keyboard = _window->getKeyboardListener();
    wma::MouseListener& mouse = _window->getMouseListener();
    wma::TouchListener& touch = _window->getTouchListener();

    for (const Attachment& attachment : _attachments)
    {
        keyboard.setKeyEventAction(wma::KeyEventCallback{}, attachment.keys);
        keyboard.setTextInputAction(wma::TextInputCallback{}, attachment.keys);

        mouse.removeButtonAction(wma::MouseButton::WMALeft, attachment.pointer);

        touch.setDownAction(wma::TouchInputCallback{}, attachment.touch);
        touch.setMoveAction(wma::TouchInputCallback{}, attachment.touch);
        touch.setUpAction(wma::TouchInputCallback{}, attachment.touch);
    }
}

void Context::Impl::attachInput(wma::IWindowManager& windowManager)
{
    _window = &windowManager;
    _mouse = &windowManager.getMouseListener();

    wma::KeyboardListener& keyboard = windowManager.getKeyboardListener();
    wma::TouchListener& touch = windowManager.getTouchListener();

    _attachments.push_back(Attachment{keyboard.getResolvedContext(),
                                      _mouse->getResolvedContext(),
                                      touch.getResolvedContext()});

    _mouse->addButtonAction(wma::MouseButton::WMALeft,
                            wma::MouseAction{[this]() { _pendingInput.mouseDown = true; },
                                             [this]() { _pendingInput.mouseDown = false; }});

    keyboard.setKeyEventAction(wma::KeyEventCallback::from(
        [this](const wma::WMAKeyEvent& event) {
            if (event.isPressOrRepeat())
                _pendingInput.keys.push_back(KeyPress{event.key, event.mods});
        }));

    keyboard.setTextInputAction(wma::TextInputCallback::from(
        [this](wma::Codepoint codepoint) { appendUtf8(_pendingInput.text, codepoint); }));

    const auto trackFinger = [this](const wma::WMATouchPoint& point) {
        _pendingInput.mouse = {static_cast<float>(point.x), static_cast<float>(point.y)};
    };

    touch.setDownAction(wma::TouchInputCallback::from(
        [this, trackFinger](const wma::WMATouchPoint& point) {
            trackFinger(point);
            _pendingInput.mouseDown = true;
            _pointer.touchActive = true;
        }));

    touch.setMoveAction(wma::TouchInputCallback::from(trackFinger));

    touch.setUpAction(wma::TouchInputCallback::from(
        [this](const wma::WMATouchPoint&) {
            _pendingInput.mouseDown = false;
            _pointer.touchActive = false;
        }));
}

void Context::Impl::newFrame() noexcept
{
    if (_mouse && !_pointer.touchActive)
    {
        const wma::WMAMousePosition position = _mouse->getCurrentPosition();
        _pendingInput.mouse = {static_cast<float>(position.x), static_cast<float>(position.y)};

        _pendingInput.scroll += static_cast<float>(_mouse->consumeScrollDelta().yOffset);
    }

    //! Swapped, not copied. _input takes this frame's events and hands its own
    //! spent buffers back, so the key vector and the text string reuse their
    //! capacity instead of reallocating every time someone holds a key down.
    std::swap(_input, _pendingInput);

    //! The swap also handed back the *previous* frame's cursor position and
    //! button state, which are levels rather than events: carry the current
    //! ones forward, since nothing will re-deliver them.
    _pendingInput.mouse = _input.mouse;
    _pendingInput.mouseDown = _input.mouseDown;
    _pendingInput.clearFrameEvents();

    _beginFrame();

    if (_window && _window->isTextInputEnabled() != _capture.textInput)
        _window->setTextInputEnabled(_capture.textInput);
}

void Context::Impl::newFrame(const Input& input) noexcept
{
    //! Copied, unavoidably: this overload does not own what it is given.
    _input = input;
    _beginFrame();
}

void Context::Impl::_beginFrame() noexcept
{
    ++_frame;

    //! Before any widget runs, so a stamp taken this frame is never mistaken
    //! for one that has gone stale.
    _panels.sweep(_frame);
    _textStates.sweep(_frame);
    _numericBuffers.sweep(_frame);
    _scrollStates.sweep(_frame);
    _widgetValues.sweep(_frame);

    _capture.mouse = _capture.mouseThisFrame || _pointer.active != 0;
    _capture.mouseThisFrame = false;
    _capture.textInputThisFrame = false;

    _pointer.beginFrame(_input.mouseDown);
    _focus.beginFrame();

    for (const KeyPress& press : _input.keys)
    {
        if (press.key == wma::KEY_TAB)
            (press.mods.shift ? _focus.requestPrev : _focus.requestNext) = true;
    }

    _vertexCount = 0;
    _overlayQuads.clear();
    _deferring = false;

    _panel = CurrentPanel{};
    _labelCounts.clear();
    _scrollStack.clear();
    _tabBar.open = false;
    _clip = Rect{};

    //! Scoped to a frame's submission, so an unbalanced endDisabled() cannot
    //! leak into the next one.
    _disabledStack.clear();
}

void Context::Impl::render()
{
    //! Tab past the last focusable, and Shift+Tab before the first, both wrap:
    //! the request outlives the frame's widgets and is resolved here.
    if (_focus.requestNext && _focus.first != 0)
        _setFocus(_focus.first);

    if ((_focus.requestPrev || _focus.wrapToLast) && _focus.last != 0)
        _setFocus(_focus.last);

    _focus.requestNext = false;
    _focus.requestPrev = false;
    _focus.wrapToLast = false;

    _capture.keyboard = _focus.current != 0;
    _capture.textInput = _capture.textInputThisFrame;

    //! Deferred geometry was tinted when it was recorded, if it was recorded
    //! inside a beginDisabled(); replaying it must not tint it twice.
    _disabledStack.clear();
    _flushOverlays();

    const size_t quads = _vertexCount / kVerticesPerQuad;

    if (!_usable || quads == 0)
        return;

    _growQuadIndices(quads);
    _uploadAtlasChanges();

    _renderer->drawBatch2D(std::span{_vertices}.first(_vertexCount),
                           std::span{_quadIndices}.first(quads * kIndicesPerQuad),
                           _atlasTexture);
}

void Context::Impl::_flushOverlays()
{
    if (_overlayQuads.empty())
        return;

    const Rect savedClip = _clip;
    _deferring = false;

    for (const DeferredQuad& quad : _overlayQuads)
    {
        _clip = quad.clip;
        _texturedQuad(quad.bounds, quad.uvMin, quad.uvMax, quad.color);
    }

    _clip = savedClip;
    _overlayQuads.clear();
}

// -----------------------------------------------------------------------------
// Identity, style, interaction and focus
// -----------------------------------------------------------------------------

u32 Context::Impl::_peekId(std::string_view text) const noexcept
{
    const u32 label = hashBytes(text, _panel.id);

    return label ^ (_labelCounts.peek(label) * kIdStride);
}

u32 Context::Impl::_idFor(std::string_view text)
{
    const u32 label = hashBytes(text, _panel.id);

    return label ^ (_labelCounts.bump(label) * kIdStride);
}

u32& Context::Impl::_state(u32 id, u32 initial)
{
    return _widgetValues.touch(id, _frame, initial).value;
}

u32& Context::Impl::_toggle(const Item& item, bool defaultOpen)
{
    u32& open = _state(item.id, defaultOpen ? 1u : 0u);

    if (item.activated)
        open ^= 1u;

    return open;
}

bool Context::Impl::_hovering(const Rect& bounds) const noexcept
{
    return bounds.contains(_input.mouse) && _clip.contains(_input.mouse);
}

Context::Impl::Item Context::Impl::_behaviour(u32 id, const Rect& rect,
                                              const WidgetStyle& style) noexcept
{
    Item item;
    item.id = id;
    item.rect = rect;
    item.style = style;
    item.valid = true;

    //! Laid out and drawn, but never hit-tested: inside a beginDisabled() a
    //! widget cannot be clicked, and cannot swallow the press meant for
    //! whatever is behind it. _focusItem() skips it for the same reason, which
    //! between them is the whole of the feature.
    if (isDisabled())
        return item;

    /*
     * An open dropdown list is emitted mid-panel but drawn over the widgets
     * that follow it, so plain submission order would hand the cursor to
     * whatever it covers: the entry claims hot, the widget underneath is
     * submitted later and overwrites the claim, and the next press lands on
     * the wrong one. Deferred geometry is exactly what draws on top, so its
     * claim stands for the rest of the frame -- and the widgets beneath do not
     * light up as hovered either.
     */
    const bool overlayOwnsCursor = _pointer.nextHotOverlay && !_deferring;

    item.hovered = !overlayOwnsCursor && _hovering(rect);

    if (item.hovered)
    {
        _pointer.nextHot = id;
        _pointer.nextHotOverlay = _deferring;
    }

    if (_pointer.active == id)
    {
        _pointer.activeSubmitted = true;
        item.held = true;

        if (!_input.mouseDown)
        {
            item.clicked = item.hovered;
            item.held = false;
            _pointer.active = 0;
        }
    }
    //! Accepting a press from a widget hovered *now* covers the first frame the
    //! cursor arrives, when nothing has claimed hot yet. Requiring hot == 0
    //! keeps it safe: with panels overlapping, the topmost already claimed it.
    else if (_pointer.pressed && (_pointer.hot == id || (_pointer.hot == 0 && item.hovered)))
    {
        _pointer.active = id;
        _pointer.activeSubmitted = true;
        item.held = true;
    }

    return item;
}

void Context::Impl::_focusItem(Item& item) noexcept
{
    if (isDisabled())
        return;

    const u32 id = item.id;

    if (_focus.first == 0)
        _focus.first = id;
    _focus.last = id;

    if (_focus.requestNext && _focus.passed)
    {
        _focus.requestNext = false;
        _setFocus(id);
    }
    else if (_focus.requestPrev && id == _focus.lastFrame)
    {
        _focus.requestPrev = false;
        if (_focus.previous != 0)
            _setFocus(_focus.previous);
        else
            _focus.wrapToLast = true;
    }

    if (id == _focus.lastFrame)
        _focus.passed = true;

    _focus.previous = id;

    item.focused = _focus.current == id;
    if (item.focused)
        _focus.submitted = true;

    //! After the focused test, so a widget does not observe its own click as a
    //! keyboard focus it already had -- a text field would then skip taking the
    //! snapshot Escape reverts to.
    if (item.clicked)
        _setFocus(id);

    item.activated = item.clicked ||
                     (item.focused && (_keyPressed(wma::KEY_ENTER) ||
                                       _keyPressed(wma::KEY_KP_ENTER) ||
                                       _keyPressed(wma::KEY_SPACE)));
}

Context::Impl::Item Context::Impl::_item(Part part, std::string_view label,
                                        const ItemSpec& spec, const Style& patch)
{
    if (!_panel.open)
        return Item{};

    const WidgetStyle style = _look(part, patch);

    //! Taken even while disabled: the id is what every retained value hangs
    //! off, and a widget must not lose its caret or its open flag for having
    //! spent a few frames greyed out.
    const u32 id = _idFor(label);

    Rect rect = _nextRow(spec.height > 0.0f ? spec.height : _rowHeight(style));

    if (spec.trailingLabel && !label.empty())
    {
        rect.max.x = std::max(rect.min.x,
                              rect.max.x - measureText(label).x - _theme.metrics.padding);
    }

    Item item = _behaviour(id, rect, style);
    _focusItem(item);

    return item;
}

void Context::Impl::_setFocus(u32 id) noexcept
{
    if (_focus.current == id)
        return;

    _focus.current = id;
    _focus.submitted = true;
}

void Context::Impl::_clearFocus(u32 id) noexcept
{
    if (_focus.current == id)
        _focus.current = 0;
}

bool Context::Impl::_keyPressed(wma::Key key) const noexcept
{
    return std::ranges::any_of(_input.keys, [key](const KeyPress& press) {
        return press.key == key && press.mods.onlyShiftOrNone();
    });
}

// -----------------------------------------------------------------------------
// Layout
// -----------------------------------------------------------------------------

Rect Context::Impl::_nextRow(float height) noexcept
{
    const Metrics& metrics = _theme.metrics;

    const float left = _panel.bounds.min.x + metrics.padding + _panel.indent;

    //! Inside a scroll region the content stops short of the scrollbar, whether
    //! or not one is currently needed, so rows don't reflow as it appears.
    const float right = _scrollStack.empty()
                            ? _panel.bounds.max.x - metrics.padding
                            : _scrollStack.back().viewport.max.x - metrics.scrollbarWidth -
                                  metrics.itemSpacing;

    const float x = _panel.packNext ? _panel.lastWidget.max.x + metrics.itemSpacing : left;

    if (!_panel.packNext)
    {
        _panel.rowTop = _panel.cursorY;
        _panel.rowHeight = height;
        _panel.cursorY += height + metrics.itemSpacing;
    }
    _panel.packNext = false;

    const float wanted = _panel.nextWidth > 0.0f ? _panel.nextWidth : right - x;
    _panel.nextWidth = 0.0f;

    _panel.lastWidget = Rect{{x, _panel.rowTop},
                             {std::min(x + wanted, right), _panel.rowTop + _panel.rowHeight}};

    return _panel.lastWidget;
}

void Context::Impl::sameLine() noexcept
{
    if (_panel.open && _panel.rowHeight > 0.0f)
        _panel.packNext = true;
}

void Context::Impl::setNextItemWidth(float width) noexcept
{
    if (_panel.open)
        _panel.nextWidth = std::max(width, 0.0f);
}

void Context::Impl::spacing(float pixels) noexcept
{
    if (_panel.open)
        _panel.cursorY += pixels;
}

void Context::Impl::separator(const Style& patch)
{
    if (!_panel.open)
        return;

    constexpr float kThickness = 1.0f;

    const WidgetStyle style = _look(Part::Separator, patch);

    const Rect row = _nextRow(_theme.metrics.itemSpacing * 2.0f + kThickness);
    const float y = std::round((row.min.y + row.max.y) * 0.5f);

    _surface({{row.min.x, y}, {row.max.x, y + kThickness}}, style, style.surface.normal,
             style.border.normal);
}

// -----------------------------------------------------------------------------
// Panels
// -----------------------------------------------------------------------------

bool Context::Impl::beginPanel(std::string_view title, glm::vec2 defaultPosition, float width)
{
    if (!_usable || _panel.open)
        return false;

    const u32 id = hashBytes(title, kHashOffsetBasis);

    //! `firstSeen` is what a `placed` flag used to be: the one frame on which
    //! defaultPosition still applies.
    const auto [state, firstSeen] = _panels.touch(id, _frame);
    if (firstSeen)
        state.position = defaultPosition;

    const WidgetStyle& titleStyle = _look(Part::TitleBar);
    const float titleHeight = _rowHeight(titleStyle);

    //! Hit-test where the bar *was*, draw where it ends up: reusing one
    //! rectangle for both leaves the title trailing the body by a frame of
    //! mouse movement, which there is a regression test for.
    const Rect grabBar{state.position, state.position + glm::vec2{width, titleHeight}};

    _clip = grabBar;

    if (_behaviour(hashBytes("##title", id), grabBar, titleStyle).held)
    {
        if (_pointer.pressed)
            _pointer.dragOffset = _input.mouse - state.position;

        state.position = _input.mouse - _pointer.dragOffset;

        wma::IWindowManager* manager = _renderer->getWindowManager();
        if (const wma::WindowDetails* window = manager ? manager->getWindowDetails() : nullptr)
        {
            //! One row of the panel always stays on screen, so a panel dragged
            //! off an edge can be dragged back.
            const float margin = titleHeight;
            const float minX = margin - width;

            state.position.x = std::clamp(state.position.x, minX,
                                          std::max(minX, static_cast<float>(window->width) - margin));
            state.position.y = std::clamp(state.position.y, 0.0f,
                                          std::max(0.0f, static_cast<float>(window->height) - margin));
        }
    }

    const Rect titleBar{state.position, state.position + glm::vec2{width, titleHeight}};

    _panel = CurrentPanel{};
    _labelCounts.clear();
    _panel.id = id;
    _panel.bounds = titleBar;
    _panel.open = true;
    _panel.cursorY = titleBar.max.y + _theme.metrics.padding;

    //! Everything from here to endPanel() is this panel's geometry; the
    //! background is spliced in front of it once its height is known.
    _panel.vertexStart = _vertexCount;

    _clip = Rect{state.position, {titleBar.max.x, state.position.y + kUnboundedBelow}};

    _surface(titleBar, titleStyle, titleStyle.surface.normal, titleStyle.border.normal);
    _label(title, titleBar, titleStyle);

    _clip.min.y = titleBar.max.y;

    return true;
}

void Context::Impl::endPanel()
{
    if (!_panel.open)
        return;

    const Metrics& metrics = _theme.metrics;

    const float bottom = std::max(_panel.cursorY - metrics.itemSpacing + metrics.padding,
                                  _panel.bounds.min.y + _rowHeight(_look(Part::TitleBar)));

    _panel.bounds.max.y = bottom;

    _insertPanelBackground();

    if (_panel.bounds.contains(_input.mouse))
        _capture.mouseThisFrame = true;

    _panel.open = false;
}

/*
 * A panel's height is not known until its content has been emitted, but its
 * background has to be *behind* that content. Rather than reserving a quad up
 * front and rewriting its bottom edge -- which pins the background to being
 * exactly one rectangle, and quietly breaks the moment it is rounded or given
 * a border -- the background is built last and rotated into place.
 *
 * Renumbering runs before the rotation, while an index still says which side
 * of the split it came from.
 */
void Context::Impl::_insertPanelBackground()
{
    const size_t vertexSplit = _vertexCount;

    const WidgetStyle& style = _look(Part::Panel);

    const Rect savedClip = std::exchange(_clip, _panel.bounds);
    _surface(_panel.bounds, style, style.surface.normal, style.border.normal);
    _clip = savedClip;

    if (_vertexCount == vertexSplit)
        return;

    /*
     * Not std::rotate: for random-access iterators libstdc++ picks the
     * gcd-cycle algorithm, which strides through the buffer and misses cache on
     * nearly every step. The background is a handful of quads against a panel's
     * worth of content, so lifting it into scratch, sliding the content up by
     * that much and dropping it back in is two sequential passes over the small
     * part and one memmove over the large one.
     *
     * Only the vertices move. The indices used to be rotated and renumbered
     * alongside them, which is what _quadIndices made unnecessary.
     */
    rotateTailToFront(_vertices.data(), _panel.vertexStart, vertexSplit, _vertexCount,
                      _vertexScratch);
}

// -----------------------------------------------------------------------------
// Widgets
//
// Every one of them is _widget() -- the row, the identity, the hit-test, the
// focus ring -- plus the few lines that make it that widget. Nothing below
// decides a colour, a radius or a height for itself: all of that arrives on the
// Item, from the Part's entry in the theme.
// -----------------------------------------------------------------------------

void Context::Impl::label(std::string_view text, const Style& patch)
{
    if (!_panel.open)
        return;

    const WidgetStyle style = _look(Part::Label, patch);
    const Rect row = _nextRow(_rowHeight(style));

    _surface(row, style, style.surface.normal, style.border.normal);
    _label(text, row, style);
}

bool Context::Impl::button(std::string_view text, const Style& patch)
{
    return _widget(Part::Button, text, patch, [&](const Item& it) {
        _paint(it, it.held);
        _label(text, it.rect, it.style);

        return it.activated;
    });
}

bool Context::Impl::checkbox(std::string_view text, bool& value, const Style& patch)
{
    return _widget(Part::Checkbox, text, patch, [&](const Item& it) {
        if (it.activated)
            value = !value;

        const Rect box = _marker(it, value);
        _textAt(text, box.max.x + it.style.padding, it.rect, it.style.text);

        return it.activated;
    });
}

bool Context::Impl::radioButton(std::string_view label, int& value, int buttonValue,
                                const Style& patch)
{
    return _widget(Part::Radio, label, patch, [&](const Item& it) {
        const bool takes = it.activated && value != buttonValue;
        if (it.activated)
            value = buttonValue;

        const Rect box = _marker(it, value == buttonValue);
        _textAt(label, box.max.x + it.style.padding, it.rect, it.style.text);

        return takes;
    });
}

bool Context::Impl::selectable(std::string_view label, bool selected, const Style& patch)
{
    return _widget(Part::Selectable, label, patch, [&](const Item& it) {
        _paint(it, selected);
        _label(label, it.rect, it.style);

        return it.activated;
    });
}

bool Context::Impl::sliderFloat(std::string_view text, float& value, float min, float max,
                                const Style& patch)
{
    if (!(max > min))
        return false;

    return _widget(Part::Slider, text, patch, [&](const Item& it) {
        //! Focus on the press, not the click: a drag that ends off the track
        //! still leaves the slider ready for arrow keys.
        if (it.held && _pointer.pressed)
            _setFocus(it.id);

        const float clamped = std::clamp(value, min, max);
        bool changed = clamped != value;
        value = clamped;

        if (it.focused)
        {
            for (const KeyPress& press : _input.keys)
            {
                const bool back = press.key == wma::KEY_LEFT || press.key == wma::KEY_DOWN;
                const bool forward = press.key == wma::KEY_RIGHT || press.key == wma::KEY_UP;

                if ((!back && !forward) || !press.mods.onlyShiftOrNone())
                    continue;

                const float step = (max - min) * (press.mods.shift ? 0.10f : 0.01f);

                value = std::clamp(back ? value - step : value + step, min, max);
                changed = true;
            }
        }

        if (it.held)
        {
            const float t =
                std::clamp((_input.mouse.x - it.rect.min.x) / it.rect.width(), 0.0f, 1.0f);

            if (const float next = lerp(min, max, t); next != value)
            {
                value = next;
                changed = true;
            }
        }

        _paint(it, false);

        if (const float filled = (value - min) / (max - min); filled > 0.0f)
        {
            //! Drawn as the whole track in the accent colour, clipped to the
            //! filled part: the fill then keeps the track's rounded left end
            //! and stays square where the value cuts it off.
            const Rect fill{it.rect.min,
                            {it.rect.min.x + filled * it.rect.width(), it.rect.max.y}};

            const Rect savedClip = std::exchange(_clip, intersect(_clip, fill));
            _roundedQuad(it.rect, it.style.rounding, it.style.accent);
            _clip = savedClip;
        }

        _caption.assign(text);
        _caption += ": ";
        appendFloat(_caption, value);

        _label(_caption, it.rect, it.style);

        return changed;
    });
}

bool Context::Impl::inputText(std::string_view label, std::string& value, size_t maxBytes,
                              const Style& patch)
{
    return _widget(Part::TextField, label, patch, ItemSpec{.trailingLabel = true}, [&](const Item& it) {
        const Rect row = it.rect;
        const WidgetStyle& style = it.style;

        if (it.held && _pointer.pressed)
            _setFocus(it.id);
        else if (_pointer.pressed && !it.hovered && it.focused)
            _clearFocus(it.id);

        TextState& state = _textStates.touch(it.id, _frame).value;

        //! The frame focus is gained: snapshot the value for Escape, and put
        //! the caret at the end as every platform's field does.
        if (it.focused && _focus.lastFrame != it.id)
        {
            state.original = value;
            state.caret = value.size();
            state.anchor = state.caret;
        }

        bool changed = false;

        if (it.focused)
        {
            if (_keyPressed(wma::KEY_ESCAPE))
            {
                value = state.original;
                changed = true;
                _clearFocus(it.id);
            }
            else if (_keyPressed(wma::KEY_ENTER) || _keyPressed(wma::KEY_KP_ENTER))
            {
                _clearFocus(it.id);
            }
            else
            {
                changed = _editText(state, value, maxBytes);
            }

            if (_focus.current == it.id)
                _capture.textInputThisFrame = true;
        }

        _paint(it, it.focused);

        const float inset = style.padding;
        const float visibleWidth = std::max(row.width() - inset * 2.0f, 1.0f);
        const float caretX = _xFromCaret(value, state.caret);

        //! Follow the caret first, then refuse to scroll past either end of the
        //! text, so a short string is never pushed off its own field.
        state.scrollX = std::clamp(state.scrollX, caretX - visibleWidth, caretX);
        state.scrollX = std::clamp(state.scrollX, 0.0f,
                                   std::max(0.0f, measureText(value).x - visibleWidth));

        const Rect savedClip = std::exchange(_clip, intersect(_clip, row));

        const glm::vec2 textOrigin{row.min.x + inset - state.scrollX,
                                   row.min.y + (row.height() - _lineHeight()) * 0.5f};

        if (it.focused && state.caret != state.anchor)
        {
            const float from = _xFromCaret(value, std::min(state.caret, state.anchor));
            const float to = _xFromCaret(value, std::max(state.caret, state.anchor));

            _quad({{textOrigin.x + from, row.min.y + 2.0f},
                   {textOrigin.x + to, row.max.y - 2.0f}},
                  style.accent);
        }

        _text(value, textOrigin, style.text);

        if (it.focused)
        {
            const float x = textOrigin.x + caretX;
            _quad({{x, row.min.y + 2.0f}, {x + 1.0f, row.max.y - 2.0f}}, style.text);
        }

        _clip = savedClip;

        //! Placing the caret needs textOrigin, so it happens after drawing; the
        //! move only shows next frame, which is imperceptible while dragging.
        if (it.held)
        {
            state.caret = _caretFromX(value, textOrigin.x, _input.mouse.x);
            if (_pointer.pressed)
                state.anchor = state.caret;
        }

        _textAt(label, row.max.x + _theme.metrics.padding, row, style.text);

        return changed;
    });
}

bool Context::Impl::inputFloat(std::string_view label, float& value, const Style& patch)
{
    if (!_panel.open)
        return false;

    //! Peeked, not consumed: inputText() below derives the same id from the
    //! same label, which is exactly the key this buffer is stored under.
    const u32 id = _peekId(label);

    auto [buffer, inserted] = _numericBuffers.touch(id, _frame);

    //! Reformatted only while unfocused. Rewriting it as it is typed would
    //! turn "1." back into "1", so the decimal point could never be entered.
    if (inserted || _focus.current != id)
    {
        buffer.clear();
        appendFloat(buffer, value);
    }

    if (!inputText(label, buffer, kNumericMaxBytes, patch))
        return false;

    const char* begin = buffer.c_str();
    char* end = nullptr;
    const float parsed = std::strtof(begin, &end);

    if (end == begin || !std::isfinite(parsed) || parsed == value)
        return false;

    value = parsed;
    return true;
}

bool Context::Impl::dropdown(std::string_view label, int& index,
                             std::span<const std::string_view> items, const Style& patch)
{
    if (items.empty())
        return false;

    index = std::clamp(index, 0, static_cast<int>(items.size()) - 1);

    return _widget(Part::Dropdown, label, patch, ItemSpec{.trailingLabel = true}, [&](const Item& it) {
        const Rect row = it.rect;
        const WidgetStyle& style = it.style;

        u32& open = _toggle(it, false);

        bool changed = false;

        if (it.focused)
        {
            if (_keyPressed(wma::KEY_DOWN) && index + 1 < static_cast<int>(items.size()))
            {
                ++index;
                changed = true;
            }
            else if (_keyPressed(wma::KEY_UP) && index > 0)
            {
                --index;
                changed = true;
            }
        }

        _paint(it, open != 0u);

        _label(items[static_cast<size_t>(index)], row, style);
        _textAt("v", row.max.x - style.padding - measureText("v").x, row, style.text);
        _textAt(label, row.max.x + _theme.metrics.padding, row, style.text);

        if (open == 0u)
            return changed;

        const WidgetStyle& entryStyle = _look(Part::DropdownItem);
        const float itemHeight = _rowHeight(entryStyle);

        const Rect list{{row.min.x, row.max.y},
                        {row.max.x, row.max.y + itemHeight * static_cast<float>(items.size())}};

        if (list.contains(_input.mouse))
            _capture.mouseThisFrame = true;

        //! Clipped to the list rather than to the panel, and deferred, so the
        //! open list draws over whatever follows it.
        const Rect savedClip = std::exchange(_clip, list);
        _deferring = true;

        //! The list is a small panel floating over the UI, and is styled as one.
        const WidgetStyle& panelStyle = _look(Part::Panel);
        _surface(list, panelStyle, panelStyle.surface.normal, panelStyle.border.normal);

        for (size_t i = 0; i < items.size(); ++i)
        {
            const Rect entryRow{{list.min.x, list.min.y + itemHeight * static_cast<float>(i)},
                                {list.max.x, list.min.y + itemHeight * static_cast<float>(i + 1)}};

            const Item hit =
                _behaviour(hashBytes(items[i], it.id) ^ 0x51ed270bu, entryRow, entryStyle);

            _paint(hit, static_cast<int>(i) == index);
            _label(items[i], entryRow, entryStyle);

            if (hit.clicked)
            {
                changed = changed || static_cast<int>(i) != index;
                index = static_cast<int>(i);
                open = 0u;
            }
        }

        _deferring = false;
        _clip = savedClip;

        if (_pointer.pressed && !list.contains(_input.mouse) && !row.contains(_input.mouse))
            open = 0u;

        return changed;
    });
}

bool Context::Impl::collapsingHeader(std::string_view label, bool defaultOpen,
                                     const Style& patch)
{
    return _widget(Part::Header, label, patch, [&](const Item& it) {
        const bool open = _toggle(it, defaultOpen) != 0u;

        //! Hover outranks open: a header stays legible as a header, and its
        //! state is already spelled out by the glyph.
        _paint(it, false);
        _textAt(label, _leadingGlyph(it, open ? "-" : "+"), it.rect, it.style.text);

        return open;
    });
}

bool Context::Impl::treeNode(std::string_view label, bool defaultOpen, const Style& patch)
{
    return _widget(Part::TreeNode, label, patch, [&](const Item& it) {
        const bool open = _toggle(it, defaultOpen) != 0u;

        _paint(it, false);
        _textAt(label, _leadingGlyph(it, open ? "v" : ">"), it.rect, it.style.text);

        if (open)
        {
            _panel.indent += _theme.metrics.indent;
            ++_panel.treeDepth;
        }

        return open;
    });
}

void Context::Impl::treePop() noexcept
{
    if (_panel.treeDepth == 0)
        return;

    --_panel.treeDepth;
    _panel.indent = std::max(0.0f, _panel.indent - _theme.metrics.indent);
}

bool Context::Impl::beginScroll(std::string_view id, float height, const Style& patch)
{
    if (!_panel.open)
        return false;

    const Metrics& metrics = _theme.metrics;
    const WidgetStyle viewStyle = _look(Part::ScrollView, patch);

    const u32 widgetId = _idFor(id);
    const Rect viewport = _nextRow(std::max(height, metrics.rowHeight));

    ScrollState& state = _scrollStates.touch(widgetId, _frame).value;

    const float maxOffset = std::max(0.0f, state.contentHeight - viewport.height());

    if (_input.scroll != 0.0f && _hovering(viewport))
    {
        state.offset -= _input.scroll * kScrollStep;
        _input.scroll = 0.0f;
        _capture.mouseThisFrame = true;
    }

    state.offset = std::clamp(state.offset, 0.0f, maxOffset);

    _surface(viewport, viewStyle, viewStyle.surface.normal, viewStyle.border.normal);

    if (maxOffset > 0.0f)
    {
        const WidgetStyle& trackStyle = _look(Part::ScrollTrack);
        const WidgetStyle& thumbStyle = _look(Part::ScrollThumb);

        const Rect track{{viewport.max.x - metrics.scrollbarWidth, viewport.min.y}, viewport.max};

        const float thumbHeight =
            std::max(track.height() * viewport.height() / state.contentHeight, 16.0f);
        const float travel = track.height() - thumbHeight;
        const float thumbTop = track.min.y + travel * (state.offset / maxOffset);

        const Item drag = _behaviour(hashBytes("##scrollbar", widgetId), track, thumbStyle);
        if (drag.held)
        {
            //! The grab point is the thumb's centre, so the thumb lands under
            //! the cursor rather than jumping by half its height.
            const float t = std::clamp((_input.mouse.y - track.min.y - thumbHeight * 0.5f) /
                                           std::max(travel, 1.0f),
                                       0.0f, 1.0f);
            state.offset = t * maxOffset;
        }

        _surface(track, trackStyle, trackStyle.surface.normal, trackStyle.border.normal);

        const Rect thumb{{track.min.x, thumbTop}, {track.max.x, thumbTop + thumbHeight}};
        _surface(thumb, thumbStyle, thumbStyle.surface.pick(drag.held, drag.hovered),
                 thumbStyle.border.pick(drag.held, drag.hovered));
    }

    _scrollStack.push_back(ScrollFrame{widgetId, viewport, _clip,
                                       viewport.min.y - state.offset, _panel.indent});

    _clip = intersect(_clip, viewport);
    _panel.cursorY = _scrollStack.back().contentTop;

    return true;
}

void Context::Impl::endScroll()
{
    if (_scrollStack.empty())
        return;

    const ScrollFrame frame = _scrollStack.back();
    _scrollStack.pop_back();

    _scrollStates.touch(frame.id, _frame).value.contentHeight =
        _panel.cursorY - frame.contentTop;

    _clip = frame.savedClip;
    _panel.indent = frame.savedIndent;

    //! The region as a whole becomes the row just emitted, so sameLine() packs
    //! beside it rather than beside its last inner widget.
    _panel.cursorY = frame.viewport.max.y + _theme.metrics.itemSpacing;
    _panel.rowTop = frame.viewport.min.y;
    _panel.rowHeight = frame.viewport.height();
    _panel.lastWidget = frame.viewport;

    if (frame.viewport.contains(_input.mouse))
        _capture.mouseThisFrame = true;
}

bool Context::Impl::beginTabBar(std::string_view id)
{
    if (!_panel.open || _tabBar.open)
        return false;

    _tabBar.id = _idFor(id);
    _tabBar.index = 0;
    _tabBar.open = true;

    const float height = _rowHeight(_look(Part::Tab));

    //! Tabs are placed by hand rather than through _nextRow(): they are fitted
    //! to their labels and packed left to right, so the row is walked.
    _panel.rowTop = _panel.cursorY;
    _panel.rowHeight = height;
    _panel.cursorY += height + _theme.metrics.itemSpacing;

    _tabBar.rowTop = _panel.rowTop;
    _tabBar.rowHeight = height;
    _tabBar.cursorX = _panel.bounds.min.x + _theme.metrics.padding + _panel.indent;

    return true;
}

bool Context::Impl::tabItem(std::string_view label, const Style& patch)
{
    if (!_tabBar.open)
        return false;

    const WidgetStyle style = _look(Part::Tab, patch);

    const u32 index = _tabBar.index++;
    u32& selected = _state(_tabBar.id, 0u);

    const float width = measureText(label).x + style.padding * 2.0f;

    const Rect tab{{_tabBar.cursorX, _tabBar.rowTop},
                   {_tabBar.cursorX + width, _tabBar.rowTop + _tabBar.rowHeight}};

    _tabBar.cursorX += width + 2.0f;

    Item it = _behaviour(hashBytes(label, _tabBar.id), tab, style);
    _focusItem(it);

    if (it.activated)
        selected = index;

    const bool active = selected == index;

    _paint(it, active);
    _label(label, tab, style);
    _ring(it);

    return active;
}

void Context::Impl::endTabBar() noexcept
{
    _tabBar.open = false;
}

void Context::Impl::tooltip(std::string_view text, const Style& patch)
{
    if (!_panel.open || !_hovering(_panel.lastWidget))
        return;

    constexpr float kCursorGap = 12.0f;

    const WidgetStyle style = _look(Part::Tooltip, patch);

    const float pad = style.padding;
    const glm::vec2 corner = _input.mouse + kCursorGap;

    const Rect box{corner, corner + measureText(text) + pad * 2.0f};

    const Rect savedClip = std::exchange(_clip, box);
    _deferring = true;

    _surface(box, style, style.surface.normal, style.border.normal);
    _text(text, {box.min.x + pad, box.min.y + pad}, style.text);

    _deferring = false;
    _clip = savedClip;
}

// -----------------------------------------------------------------------------
// Text editing
// -----------------------------------------------------------------------------

bool Context::Impl::_editText(TextState& state, std::string& value, size_t maxBytes)
{
    bool changed = false;

    state.caret = std::min(state.caret, value.size());
    state.anchor = std::min(state.anchor, value.size());

    //! Erases between the caret and @p to, in either direction, and collapses
    //! the selection there. False when that range was already empty.
    const auto eraseTo = [&](size_t to) {
        const size_t from = std::min(state.caret, to);
        const size_t until = std::max(state.caret, to);
        if (from == until)
            return false;

        value.erase(from, until - from);
        state.caret = from;
        state.anchor = from;
        return true;
    };

    const auto deleteSelection = [&]() { return eraseTo(state.anchor); };

    const auto moveCaret = [&state](size_t to, bool selecting) {
        state.caret = to;
        if (!selecting)
            state.anchor = to;
    };

    for (const KeyPress& press : _input.keys)
    {
        //! Where the caret lands one step away, a character at a time or -- with
        //! Ctrl -- a word. The arrows move to it; the delete keys erase to it.
        const auto step = [&](bool forward) {
            if (press.mods.ctrl)
                return forward ? nextWord(value, state.caret) : previousWord(value, state.caret);

            return forward ? nextBoundary(value, state.caret)
                           : previousBoundary(value, state.caret);
        };

        const bool selecting = press.mods.shift;

        switch (press.key)
        {
            case wma::KEY_LEFT:  moveCaret(step(false), selecting); break;
            case wma::KEY_RIGHT: moveCaret(step(true), selecting); break;
            case wma::KEY_HOME:  moveCaret(0, selecting); break;
            case wma::KEY_END:   moveCaret(value.size(), selecting); break;

            //! A selection takes priority over the character beside the caret:
            //! deleting it must not also delete a neighbour.
            case wma::KEY_BACKSPACE:
                if (deleteSelection())
                    changed = true;
                else if (state.caret > 0 && eraseTo(step(false)))
                    changed = true;
                break;

            case wma::KEY_DELETE:
                if (deleteSelection())
                    changed = true;
                else if (state.caret < value.size() && eraseTo(step(true)))
                    changed = true;
                break;

            case wma::KEY_A: //! Ctrl+A selects all; plain A is ordinary text.
                if (press.mods.ctrl)
                {
                    state.anchor = 0;
                    state.caret = value.size();
                }
                break;

            default: break;
        }
    }

    if (!_input.text.empty())
    {
        if (deleteSelection())
            changed = true;

        //! Dropped whole rather than truncated: half a UTF-8 sequence would
        //! leave the value invalid.
        if (value.size() + _input.text.size() <= maxBytes)
        {
            value.insert(state.caret, _input.text);
            state.caret += _input.text.size();
            state.anchor = state.caret;
            changed = true;
        }
    }

    return changed;
}

size_t Context::Impl::_caretFromX(std::string_view text, float originX, float x)
{
    if (!_atlas)
        return 0;

    size_t best = 0;
    float bestDistance = std::abs(originX - x);

    walkGlyphs(*_atlas, text, _theme.metrics.textScale,
               [&](const GlyphInfo& glyph, float penX, size_t offsetAfter) {
                   const float trailingEdge =
                       originX + penX + glyph.advance * _theme.metrics.textScale;

                   if (const float distance = std::abs(trailingEdge - x); distance < bestDistance)
                   {
                       bestDistance = distance;
                       best = offsetAfter;
                   }
               });

    return best;
}

float Context::Impl::_xFromCaret(std::string_view text, size_t offset)
{
    return measureText(text.substr(0, std::min(offset, text.size()))).x;
}

// -----------------------------------------------------------------------------
// Geometry
// -----------------------------------------------------------------------------

glm::vec2 Context::Impl::measureText(std::string_view text)
{
    if (!_atlas || text.empty())
        return {0.0f, 0.0f};

    const float width =
        walkGlyphs(*_atlas, text, _theme.metrics.textScale, [](const GlyphInfo&, float, size_t) {});

    return {width, _lineHeight()};
}

float Context::Impl::_lineHeight() const noexcept
{
    return _atlas ? _atlas->lineHeight() * _theme.metrics.textScale : 0.0f;
}

void Context::Impl::_quad(const Rect& bounds, const glm::vec4& color)
{
    _texturedQuad(bounds, _solidUv, _solidUv, color);
}

void Context::Impl::_roundedQuad(const Rect& bounds, float radius, const glm::vec4& color)
{
    const float limit = std::min({bounds.width(), bounds.height()}) * 0.5f;
    const float r = std::min({radius, limit, kMaxRounding});

    if (r < 1.0f)
    {
        _quad(bounds, color);
        return;
    }

    /*
     * Nine-slice against an atlas coverage mask: four corner quads plus three
     * solid spans, seven quads whatever the radius. The scanline version this
     * replaces cost 1 + 2*ceil(r) quads -- at Sandbox's theme that was 59% of
     * all the UI's geometry, spent describing a shape that is one number --
     * and, being a stack of hard-edged rows, it could not antialias, which is
     * why panels default to square corners. The mask carries exact per-texel
     * coverage, so these curves are smooth.
     *
     * Truncated, not rounded: the cell is drawn `cap` pixels wide, and a cap
     * wider than half the shorter side would overlap its opposite number and
     * double-blend the seam on a translucent surface.
     */
    const auto cell = static_cast<u32>(r);
    const FontAtlas::UvRect* mask = _atlas ? _atlas->cornerMask(cell) : nullptr;

    if (mask == nullptr)
    {
        //! Atlas full, or a radius past what it will place. A square corner is
        //! wrong by a few pixels; dropping the surface is wrong by all of it.
        _quad(bounds, color);
        return;
    }

    const float cap = static_cast<float>(cell);

    const float left = bounds.min.x;
    const float right = bounds.max.x;
    const float top = bounds.min.y;
    const float bottom = bounds.max.y;

    const float innerLeft = left + cap;
    const float innerRight = right - cap;
    const float innerTop = top + cap;
    const float innerBottom = bottom - cap;

    const glm::vec2 uvOut = mask->min; //! The curve's outside edge.
    const glm::vec2 uvIn = mask->max;

    //! One cell serves all four corners: swapping the UV bounds on an axis
    //! mirrors it, because _texturedQuad assigns them to corners in a fixed
    //! order. Clockwise from the top left.
    _texturedQuad({{left, top}, {innerLeft, innerTop}}, uvOut, uvIn, color);
    _texturedQuad({{innerRight, top}, {right, innerTop}},
                  {uvIn.x, uvOut.y}, {uvOut.x, uvIn.y}, color);
    _texturedQuad({{innerRight, innerBottom}, {right, bottom}}, uvIn, uvOut, color);
    _texturedQuad({{left, innerBottom}, {innerLeft, bottom}},
                  {uvOut.x, uvIn.y}, {uvIn.x, uvOut.y}, color);

    //! The bar between the caps spans the full width; the other two only reach
    //! between the corners. Any of the three is empty when the radius meets in
    //! the middle -- a pill or a circle -- and _texturedQuad drops it.
    _quad({{left, innerTop}, {right, innerBottom}}, color);
    _quad({{innerLeft, top}, {innerRight, innerTop}}, color);
    _quad({{innerLeft, innerBottom}, {innerRight, bottom}}, color);
}

void Context::Impl::_surface(const Rect& bounds, const WidgetStyle& style, const glm::vec4& fill,
                             const glm::vec4& border)
{
    const bool outlined = style.borderWidth > 0.0f && border.a > 0.0f;

    if (outlined)
        _roundedQuad(bounds, style.rounding, border);

    if (fill.a <= 0.0f)
        return;

    //! The fill is laid *over* the border rather than punched out of it: a
    //! rounded ring would have to be built span by span for a difference
    //! nothing can see, since a control's fill is opaque where it overlaps.
    _roundedQuad(outlined ? bounds.inset(style.borderWidth) : bounds,
                 outlined ? style.rounding - style.borderWidth : style.rounding, fill);
}

void Context::Impl::_paint(const Item& item, bool active)
{
    const WidgetStyle& style = item.style;

    _surface(item.rect, style, style.surface.pick(active, item.hovered),
             style.border.pick(active, item.hovered));
}

void Context::Impl::_texturedQuad(Rect bounds, glm::vec2 uvMin, glm::vec2 uvMax, glm::vec4 color)
{
    const glm::vec2 size = bounds.size();
    if (size.x <= 0.0f || size.y <= 0.0f)
        return;

    //! One place, so nothing can be drawn inside a beginDisabled() and come out
    //! at full strength -- surfaces, borders, glyphs, tick marks and all.
    if (isDisabled())
        color.a *= _theme.metrics.disabledAlpha;

    if (color.a <= 0.0f)
        return;

    if (_deferring)
    {
        _overlayQuads.push_back(DeferredQuad{bounds, uvMin, uvMax, color, _clip});
        return;
    }

    const Rect visible = intersect(bounds, _clip);
    if (visible.width() <= 0.0f || visible.height() <= 0.0f)
        return;

    /*
     * Quads are axis-aligned, so trimming the rectangle and moving the UVs by
     * the same fractions is exact: the surviving texels are pixel-identical to
     * what a scissor rectangle would have produced.
     *
     * The untrimmed case is the overwhelming majority -- only the quads
     * straddling a scroll view's edge are ever cut -- and it is also the one
     * where the arithmetic below is the identity. Taking it early skips four
     * dependent float divisions per quad, which at Sandbox's ~1300 quads is
     * the single largest saving in the emission path.
     */
    glm::vec2 uv0 = uvMin;
    glm::vec2 uv1 = uvMax;

    if (visible.min != bounds.min || visible.max != bounds.max)
    {
        const glm::vec2 inverse{1.0f / size.x, 1.0f / size.y};

        uv0 = {lerp(uvMin.x, uvMax.x, (visible.min.x - bounds.min.x) * inverse.x),
               lerp(uvMin.y, uvMax.y, (visible.min.y - bounds.min.y) * inverse.y)};
        uv1 = {lerp(uvMin.x, uvMax.x, (visible.max.x - bounds.min.x) * inverse.x),
               lerp(uvMin.y, uvMax.y, (visible.max.y - bounds.min.y) * inverse.y)};
    }

    //! Top-left, top-right, bottom-right, bottom-left. No indices: this quad's
    //! six are implied by its position in the buffer -- see _quadIndices.
    gfx::Vertex2D* out = _quadSlot();

    out[0] = {{visible.min.x, visible.min.y}, {uv0.x, uv0.y}, color};
    out[1] = {{visible.max.x, visible.min.y}, {uv1.x, uv0.y}, color};
    out[2] = {{visible.max.x, visible.max.y}, {uv1.x, uv1.y}, color};
    out[3] = {{visible.min.x, visible.max.y}, {uv0.x, uv1.y}, color};
}

void Context::Impl::_text(std::string_view text, glm::vec2 origin, const glm::vec4& color)
{
    if (!_atlas || text.empty())
        return;

    //! One growth for the whole run rather than one per glyph.
    if (const size_t needed = _vertexCount + text.size() * kVerticesPerQuad;
        needed > _vertices.size())
    {
        _vertices.resize(needed);
    }

    const float scale = _theme.metrics.textScale;

    walkGlyphs(*_atlas, text, scale, [&](const GlyphInfo& glyph, float penX, size_t) {
        if (glyph.size.x <= 0.0f || glyph.size.y <= 0.0f)
            return;

        const glm::vec2 topLeft{origin.x + penX + glyph.bearing.x * scale,
                                origin.y + glyph.bearing.y * scale};

        _texturedQuad({topLeft, topLeft + glyph.size * scale}, glyph.uvMin, glyph.uvMax, color);
    });
}

void Context::Impl::_textAt(std::string_view text, float x, const Rect& bounds,
                            const glm::vec4& color)
{
    _text(text, {x, bounds.min.y + (bounds.height() - _lineHeight()) * 0.5f}, color);
}

void Context::Impl::_label(std::string_view text, const Rect& bounds, const WidgetStyle& style)
{
    float x = bounds.min.x + style.padding;

    if (style.align != Align::Left)
    {
        const float slack = bounds.width() - style.padding * 2.0f - measureText(text).x;
        x += style.align == Align::Center ? slack * 0.5f : slack;
    }

    _textAt(text, x, bounds, style.text);
}

void Context::Impl::_ring(const Item& item)
{
    if (!item.focused)
        return;

    constexpr float kThickness = 1.0f;

    const Rect& bounds = item.rect;
    const glm::vec4& color = item.style.accent;

    _quad({bounds.min, {bounds.max.x, bounds.min.y + kThickness}}, color);
    _quad({{bounds.min.x, bounds.max.y - kThickness}, bounds.max}, color);
    _quad({bounds.min, {bounds.min.x + kThickness, bounds.max.y}}, color);
    _quad({{bounds.max.x - kThickness, bounds.min.y}, bounds.max}, color);
}

Rect Context::Impl::_marker(const Item& item, bool on)
{
    const WidgetStyle& style = item.style;
    const Rect& row = item.rect;

    const float size = std::max(row.height() * style.markScale, 4.0f);
    const float top = row.min.y + (row.height() - size) * 0.5f;

    const Rect box{{row.min.x, top}, {row.min.x + size, top + size}};

    _surface(box, style, style.surface.pick(false, item.hovered),
             style.border.pick(false, item.hovered));

    if (on)
    {
        //! The mark keeps the box's shape -- a square inside a check box, a dot
        //! inside a radio button -- because both come from the same rounding.
        const float inset = std::max(size * style.markInset, 2.0f);
        _roundedQuad(box.inset(inset), style.rounding - inset, style.accent);
    }

    return box;
}

float Context::Impl::_leadingGlyph(const Item& item, std::string_view glyph)
{
    const WidgetStyle& style = item.style;
    const float x = item.rect.min.x + style.padding;

    _textAt(glyph, x, item.rect, style.text);

    return x + measureText(glyph).x + style.padding;
}

gfx::Vertex2D* Context::Impl::_quadSlot()
{
    if (_vertexCount + kVerticesPerQuad > _vertices.size())
    {
        //! Doubling, floored at a panel's worth: the buffer settles at the
        //! busiest frame's size within a few frames and never grows again.
        _vertices.resize(std::max({_vertices.size() * 2,
                                   _vertexCount + kVerticesPerQuad,
                                   size_t{1024}}));
    }

    gfx::Vertex2D* slot = _vertices.data() + _vertexCount;
    _vertexCount += kVerticesPerQuad;

    return slot;
}

void Context::Impl::_growQuadIndices(size_t quads)
{
    const size_t have = _quadIndices.size() / kIndicesPerQuad;
    if (quads <= have)
        return;

    _quadIndices.resize(quads * kIndicesPerQuad);

    for (size_t quad = have; quad < quads; ++quad)
    {
        const auto base = static_cast<u32>(quad * kVerticesPerQuad);
        u32* out = _quadIndices.data() + quad * kIndicesPerQuad;

        out[0] = base + 0;
        out[1] = base + 1;
        out[2] = base + 2;
        out[3] = base + 2;
        out[4] = base + 3;
        out[5] = base + 0;
    }
}

void Context::Impl::_uploadAtlasChanges()
{
    const auto pending = _atlas->takeDirtyUpload();
    if (!pending)
        return;

    _renderer->updateTextureRegion(_atlasTexture, pending->region.x, pending->region.y,
                                   pending->region.width, pending->region.height,
                                   pending->rgba.data());
}

// -----------------------------------------------------------------------------
// Context: a handle in front of Impl
//
// One line each, in the header's order. Nothing decides anything here -- if a
// forwarder ever grows a body, that logic belongs in Impl with the state it
// reads.
// -----------------------------------------------------------------------------

Context::Context(IRenderer* renderer, const ContextDesc& desc)
    : _impl(std::make_unique<Impl>(renderer, desc, theme))
{
}

//! Out of line, and has to be: Impl is incomplete everywhere but this file.
Context::~Context() = default;

void Context::attachInput(wma::IWindowManager& windowManager)
{
    _impl->attachInput(windowManager);
}

void Context::newFrame() noexcept { _impl->newFrame(); }
void Context::newFrame(const Input& input) noexcept { _impl->newFrame(input); }
void Context::render() { _impl->render(); }

bool Context::beginPanel(std::string_view title, glm::vec2 defaultPosition, float width)
{
    return _impl->beginPanel(title, defaultPosition, width);
}

void Context::endPanel() { _impl->endPanel(); }

void Context::label(std::string_view text, const Style& style) { _impl->label(text, style); }

bool Context::button(std::string_view text, const Style& style)
{
    return _impl->button(text, style);
}

bool Context::checkbox(std::string_view text, bool& value, const Style& style)
{
    return _impl->checkbox(text, value, style);
}

bool Context::sliderFloat(std::string_view text, float& value, float min, float max,
                          const Style& style)
{
    return _impl->sliderFloat(text, value, min, max, style);
}

bool Context::inputText(std::string_view label, std::string& value, size_t maxBytes,
                        const Style& style)
{
    return _impl->inputText(label, value, maxBytes, style);
}

bool Context::inputFloat(std::string_view label, float& value, const Style& style)
{
    return _impl->inputFloat(label, value, style);
}

bool Context::dropdown(std::string_view label, int& index,
                       std::span<const std::string_view> items, const Style& style)
{
    return _impl->dropdown(label, index, items, style);
}

bool Context::radioButton(std::string_view label, int& value, int buttonValue,
                          const Style& style)
{
    return _impl->radioButton(label, value, buttonValue, style);
}

bool Context::collapsingHeader(std::string_view label, bool defaultOpen, const Style& style)
{
    return _impl->collapsingHeader(label, defaultOpen, style);
}

bool Context::treeNode(std::string_view label, bool defaultOpen, const Style& style)
{
    return _impl->treeNode(label, defaultOpen, style);
}

void Context::treePop() noexcept { _impl->treePop(); }

bool Context::selectable(std::string_view label, bool selected, const Style& style)
{
    return _impl->selectable(label, selected, style);
}

bool Context::beginScroll(std::string_view id, float height, const Style& style)
{
    return _impl->beginScroll(id, height, style);
}

void Context::endScroll() { _impl->endScroll(); }
bool Context::beginTabBar(std::string_view id) { return _impl->beginTabBar(id); }

bool Context::tabItem(std::string_view label, const Style& style)
{
    return _impl->tabItem(label, style);
}

void Context::endTabBar() noexcept { _impl->endTabBar(); }

void Context::tooltip(std::string_view text, const Style& style)
{
    _impl->tooltip(text, style);
}

void Context::sameLine() noexcept { _impl->sameLine(); }
void Context::setNextItemWidth(float width) noexcept { _impl->setNextItemWidth(width); }
void Context::separator(const Style& style) { _impl->separator(style); }
void Context::spacing(float pixels) noexcept { _impl->spacing(pixels); }
void Context::setKeyboardFocusHere() noexcept { _impl->setKeyboardFocusHere(); }

bool Context::isCapturingKeyboard() const noexcept { return _impl->isCapturingKeyboard(); }
bool Context::isCapturingTextInput() const noexcept { return _impl->isCapturingTextInput(); }
bool Context::isCapturingMouse() const noexcept { return _impl->isCapturingMouse(); }

void Context::beginDisabled(bool disabled) { _impl->beginDisabled(disabled); }
void Context::endDisabled() noexcept { _impl->endDisabled(); }
bool Context::isDisabled() const noexcept { return _impl->isDisabled(); }

glm::vec2 Context::measureText(std::string_view text) { return _impl->measureText(text); }

StyleGuard::StyleGuard(Context& ui, Part part, const Style& style)
    : _ui(ui), _part(part), _saved(ui.theme[part])
{
    //! Applied over the saved style rather than replacing it, so a scope that
    //! wants one different colour says only that -- the same fall-through a
    //! widget's own Style parameter gets.
    _ui.theme[_part] = style.over(_saved);
}

StyleGuard::~StyleGuard()
{
    _ui.theme[_part] = _saved;
}

} // namespace aura3d::ui
