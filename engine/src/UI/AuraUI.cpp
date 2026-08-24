#include "aura/UI/AuraUI.h"

#include "aura/Core/AuraFont/FontAtlas.h"
#include "aura/Renderer/IRenderer.h"
#include "aura/Utils/AlignedVector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <utility>

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

namespace {

constexpr size_t kVerticesPerQuad = 4;
constexpr size_t kIndicesPerQuad = 6;

//! How far below a panel its clip rectangle reaches. A panel's height is not
//! known while its content is emitted, and auto-height panels never need it.
constexpr float kUnboundedBelow = 1.0e6f;

constexpr float kScrollbarWidth = 10.0f;
constexpr float kScrollStep = 48.0f;

constexpr u32 kHashOffsetBasis = 2166136261u;

//! Stride between successive widget ids within one panel; the golden-ratio
//! constant, so consecutive counter values scatter rather than cluster.
constexpr u32 kIdStride = 0x9e3779b9u;

//! Bytes an inputFloat() accepts, one short of its formatting buffer.
constexpr size_t kNumericMaxBytes = 31;

//! Tick-box metrics: how far the box is inset from the row height, and how far
//! its fill is inset from the box. A radio button's is slightly smaller and its
//! dot slightly tighter, which is the only thing distinguishing the two.
constexpr float kCheckboxInset = 6.0f;
constexpr float kCheckboxFill = 0.25f;
constexpr float kRadioInset = 8.0f;
constexpr float kRadioFill = 0.30f;

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
    Impl(IRenderer* renderer, const ContextDesc& desc);
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

    void label(std::string_view text);
    bool button(std::string_view text);
    bool checkbox(std::string_view text, bool& value);
    bool sliderFloat(std::string_view text, float& value, float min, float max);
    bool inputText(std::string_view label, std::string& value, size_t maxBytes);
    bool inputFloat(std::string_view label, float& value);
    bool dropdown(std::string_view label, int& index, std::span<const std::string_view> items);
    bool radioButton(std::string_view label, int& value, int buttonValue);
    [[nodiscard]] bool collapsingHeader(std::string_view label, bool defaultOpen);
    [[nodiscard]] bool treeNode(std::string_view label, bool defaultOpen);
    void treePop() noexcept;
    bool selectable(std::string_view label, bool selected);

    [[nodiscard]] bool beginScroll(std::string_view id, float height);
    void endScroll();
    [[nodiscard]] bool beginTabBar(std::string_view id);
    [[nodiscard]] bool tabItem(std::string_view label);
    void endTabBar() noexcept;
    void tooltip(std::string_view text);

    void sameLine() noexcept;
    void setNextItemWidth(float width) noexcept;
    void separator();
    void spacing(float pixels) noexcept;

    void setKeyboardFocusHere() noexcept { _focus.requestNext = true; }

    [[nodiscard]] bool isCapturingKeyboard() const noexcept { return _capture.keyboard; }
    [[nodiscard]] bool isCapturingTextInput() const noexcept { return _capture.textInput; }
    [[nodiscard]] bool isCapturingMouse() const noexcept { return _capture.mouse; }

    [[nodiscard]] Style& style() noexcept { return _style; }
    [[nodiscard]] const Style& style() const noexcept { return _style; }

    [[nodiscard]] glm::vec2 measureText(std::string_view text);

private:
    /// One widget's identity, geometry, and everything this frame's input did
    /// to it. Every interactive widget is one of these plus some drawing.
    struct Item {
        u32 id = 0;
        Rect rect{};
        bool hovered = false;   //! Cursor is over the widget right now.
        bool held = false;      //! Widget owns the ongoing press.
        bool clicked = false;   //! Press was released over the widget this frame.
        bool focused = false;   //! Widget holds keyboard focus this frame.
        bool activated = false; //! Clicked, or Enter/Space while focused.
    };

    /// Reserves the next row and resolves identity, hit-testing and focus for
    /// one widget -- the opening line of every widget that takes input.
    /// @param trailingLabel Narrows the row to leave room for @p label drawn
    ///        after it, as inputText() and dropdown() do.
    [[nodiscard]] Item _item(std::string_view label, float height, bool trailingLabel = false);

    /// Runs the hot/active state machine for @p id over @p rect without
    /// entering it into the tab order. For the parts that are not rows: title
    /// bars, scrollbar thumbs, tabs, open dropdown entries.
    [[nodiscard]] Item _behaviour(u32 id, const Rect& rect) noexcept;

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

    /// Identity a widget keeps across frames: its label, its panel, and a
    /// per-panel counter, so same-labelled widgets don't collide. Consumes one
    /// counter slot, so call it exactly once per widget.
    [[nodiscard]] u32 _idFor(std::string_view text) noexcept;

    //! The id _idFor() would hand out next, without consuming the slot. For
    //! inputFloat(), which needs its state before delegating to inputText().
    [[nodiscard]] u32 _peekId(std::string_view text) const noexcept;

    //! The one retained value widgets that need exactly one share: open/closed
    //! for headers, tree nodes and dropdowns, selected index for tab bars.
    //! Ids cannot collide across kinds, so one map serves all of them.
    [[nodiscard]] u32& _state(u32 id, u32 initial);

    /// True when the cursor is inside @p bounds and inside the active clip.
    [[nodiscard]] bool _hovering(const Rect& bounds) const noexcept;

    /// Control colour for a widget's state; active outranks hovered.
    [[nodiscard]] const glm::vec4& _fill(bool active, bool hovered) const noexcept;

    /// Baseline-to-baseline distance at the current text scale, in pixels.
    [[nodiscard]] float _lineHeight() const noexcept;

    //! Byte offset of the caret nearest @p x within @p text drawn at @p originX.
    [[nodiscard]] size_t _caretFromX(std::string_view text, float originX, float x);

    //! X offset of the caret at byte @p offset, relative to the text's origin.
    [[nodiscard]] float _xFromCaret(std::string_view text, size_t offset);

    /// Appends a solid quad, clipped to the active clip rectangle.
    void _quad(const Rect& bounds, const glm::vec4& color);

    /// Appends one textured quad, trimmed to @c _clip on the CPU rather than
    /// with a scissor rectangle (drawBatch2D() is one batch with no per-command
    /// state, so a scissor would mean one draw call per clip change).
    void _texturedQuad(Rect bounds, glm::vec2 uvMin, glm::vec2 uvMax, const glm::vec4& color);

    /// Text is always Style::text -- the three appenders below take no colour.
    /// @{
    void _text(std::string_view text, glm::vec2 origin);            //! Top-left at @p origin.
    void _textAt(std::string_view text, float x, const Rect& bounds); //! Centred vertically in @p bounds.
    void _textCentered(std::string_view text, const Rect& bounds);  //! Centred on both axes.
    /// @}

    /// Draws a focus outline around @p bounds, for the keyboard-focused widget.
    void _focusRing(const Rect& bounds);

    /// Draws the tick box a checkbox or radio button shows at the left of its
    /// row, filled when @p on, and returns it so the caller can place a label
    /// beside it.
    /// @param rowInset How far the box is inset from the row height.
    /// @param fillScale The filled square's inset, as a fraction of the box.
    Rect _marker(const Rect& row, bool hovered, bool on, float rowInset, float fillScale);

    /// Replays _overlayQuads into the batch; called at the end of render().
    void _flushOverlays();

    /// Pushes the atlas' pending dirty rectangle to the GPU, if any.
    void _uploadAtlasChanges();

    IRenderer* _renderer;
    std::unique_ptr<FontAtlas> _atlas;
    TextureHandle _atlasTexture;
    glm::vec2 _solidUv{0.0f}; //! Opaque atlas texel every solid quad samples.
    bool _usable = false;     //! False when the atlas or its texture is missing.

    Style _style{};

    //! Retained across frames and over-aligned, like TextOverlay's: memcpy'd
    //! into mapped device memory / glBufferSubData every frame, and a
    //! 32-byte-aligned base keeps every 32-byte Vertex2D individually aligned.
    static_assert(sizeof(gfx::Vertex2D) == 32,
                  "Vertex2D must stay 32 bytes for the aligned batch storage "
                  "below to align every vertex, not merely the array's base.");

    AlignedVector<gfx::Vertex2D> _vertices;
    AlignedVector<u32> _indices;

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
        u32 active = 0;  //! Owns the ongoing press, if any.

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

    //! CurrentPanel::backgroundVertex when the background never reached the
    //! batch, so endPanel() knows there is nothing to patch.
    static constexpr size_t kNoVertex = static_cast<size_t>(-1);

    //! The panel between beginPanel() and endPanel(). One object answers both
    //! "is a panel being built" and "which".
    struct CurrentPanel {
        u32 id = 0;
        bool open = false;
        Rect bounds{};        //! Grows downwards as widgets are added.
        float cursorY = 0.0f; //! Top of the next row, in window pixels.
        u32 widgetIndex = 0;  //! Disambiguates widgets sharing a label.

        //! First vertex of the background quad, rewritten to the final height
        //! by endPanel(). See @ref kNoVertex.
        size_t backgroundVertex = kNoVertex;

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

Context::Impl::Impl::Impl(IRenderer* renderer, const ContextDesc& desc)
    : _renderer(renderer)
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

Context::Impl::Impl::~Impl()
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

    _vertices.clear();
    _indices.clear();
    _overlayQuads.clear();
    _deferring = false;

    _panel = CurrentPanel{};
    _scrollStack.clear();
    _tabBar.open = false;
    _clip = Rect{};
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

    _flushOverlays();

    if (!_usable || _indices.empty())
        return;

    _uploadAtlasChanges();

    _renderer->drawBatch2D(_vertices, _indices, _atlasTexture);
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
// Identity, interaction and focus
// -----------------------------------------------------------------------------

u32 Context::Impl::_peekId(std::string_view text) const noexcept
{
    return hashBytes(text, _panel.id) ^ (_panel.widgetIndex * kIdStride);
}

u32 Context::Impl::_idFor(std::string_view text) noexcept
{
    const u32 id = _peekId(text);
    ++_panel.widgetIndex;
    return id;
}

u32& Context::Impl::_state(u32 id, u32 initial)
{
    return _widgetValues.touch(id, _frame, initial).value;
}

bool Context::Impl::_hovering(const Rect& bounds) const noexcept
{
    return bounds.contains(_input.mouse) && _clip.contains(_input.mouse);
}

const glm::vec4& Context::Impl::_fill(bool active, bool hovered) const noexcept
{
    return active ? _style.controlActive : hovered ? _style.controlHovered : _style.control;
}

Context::Impl::Item Context::Impl::_behaviour(u32 id, const Rect& rect) noexcept
{
    Item item;
    item.id = id;
    item.rect = rect;
    item.hovered = _hovering(rect);

    if (item.hovered)
        _pointer.nextHot = id;

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

Context::Impl::Item Context::Impl::_item(std::string_view label, float height, bool trailingLabel)
{
    const u32 id = _idFor(label);

    Rect rect = _nextRow(height);
    if (trailingLabel && !label.empty())
        rect.max.x = std::max(rect.min.x, rect.max.x - measureText(label).x - _style.padding);

    Item item = _behaviour(id, rect);
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
    const float left = _panel.bounds.min.x + _style.padding + _panel.indent;

    //! Inside a scroll region the content stops short of the scrollbar, whether
    //! or not one is currently needed, so rows don't reflow as it appears.
    const float right = _scrollStack.empty()
                            ? _panel.bounds.max.x - _style.padding
                            : _scrollStack.back().viewport.max.x - kScrollbarWidth -
                                  _style.itemSpacing;

    const float x = _panel.packNext ? _panel.lastWidget.max.x + _style.itemSpacing : left;

    if (!_panel.packNext)
    {
        _panel.rowTop = _panel.cursorY;
        _panel.rowHeight = height;
        _panel.cursorY += height + _style.itemSpacing;
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

void Context::Impl::separator()
{
    if (!_panel.open)
        return;

    constexpr float kThickness = 1.0f;

    const Rect row = _nextRow(_style.itemSpacing * 2.0f + kThickness);
    const float y = std::round((row.min.y + row.max.y) * 0.5f);

    _quad({{row.min.x, y}, {row.max.x, y + kThickness}}, _style.separator);
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

    const float titleHeight = _style.rowHeight;

    //! Hit-test where the bar *was*, draw where it ends up: reusing one
    //! rectangle for both leaves the title trailing the body by a frame of
    //! mouse movement, which there is a regression test for.
    const Rect grabBar{state.position, state.position + glm::vec2{width, titleHeight}};

    _clip = grabBar;

    if (_behaviour(hashBytes("##title", id), grabBar).held)
    {
        if (_pointer.pressed)
            _pointer.dragOffset = _input.mouse - state.position;

        state.position = _input.mouse - _pointer.dragOffset;

        wma::IWindowManager* manager = _renderer->getWindowManager();
        if (const wma::WindowDetails* window = manager ? manager->getWindowDetails() : nullptr)
        {
            //! One row of the panel always stays on screen, so a panel dragged
            //! off an edge can be dragged back.
            const float margin = _style.rowHeight;
            const float minX = margin - width;

            state.position.x = std::clamp(state.position.x, minX,
                                          std::max(minX, static_cast<float>(window->width) - margin));
            state.position.y = std::clamp(state.position.y, 0.0f,
                                          std::max(0.0f, static_cast<float>(window->height) - margin));
        }
    }

    const Rect titleBar{state.position, state.position + glm::vec2{width, titleHeight}};

    _panel = CurrentPanel{};
    _panel.id = id;
    _panel.bounds = titleBar;
    _panel.open = true;
    _panel.cursorY = titleBar.max.y + _style.padding;

    _clip = Rect{state.position, {titleBar.max.x, state.position.y + kUnboundedBelow}};

    //! The background has to be behind the content, so it is emitted first at
    //! placeholder height and its bottom vertices rewritten by endPanel().
    const size_t background = _vertices.size();
    _quad(_panel.bounds, _style.panelBackground);
    _panel.backgroundVertex =
        _vertices.size() == background + kVerticesPerQuad ? background : kNoVertex;

    _quad(titleBar, _style.panelTitle);
    _textCentered(title, titleBar);

    _clip.min.y = titleBar.max.y;

    return true;
}

void Context::Impl::endPanel()
{
    if (!_panel.open)
        return;

    const float bottom = std::max(_panel.cursorY - _style.itemSpacing + _style.padding,
                                  _panel.bounds.min.y + _style.rowHeight);

    _panel.bounds.max.y = bottom;

    //! Corners wind top-left, top-right, bottom-right, bottom-left, so 2 and 3
    //! are exactly the pair that follows the content's height.
    if (_panel.backgroundVertex != kNoVertex)
    {
        _vertices[_panel.backgroundVertex + 2].pos.y = bottom;
        _vertices[_panel.backgroundVertex + 3].pos.y = bottom;
    }

    if (_panel.bounds.contains(_input.mouse))
        _capture.mouseThisFrame = true;

    _panel.open = false;
}

// -----------------------------------------------------------------------------
// Widgets
// -----------------------------------------------------------------------------

void Context::Impl::label(std::string_view text)
{
    if (!_panel.open)
        return;

    const Rect row = _nextRow(_style.rowHeight);
    _textAt(text, row.min.x, row);
}

bool Context::Impl::button(std::string_view text)
{
    if (!_panel.open)
        return false;

    const Item it = _item(text, _style.rowHeight);

    _quad(it.rect, _fill(it.held, it.hovered));

    if (it.focused)
        _focusRing(it.rect);

    _textCentered(text, it.rect);

    return it.activated;
}

bool Context::Impl::checkbox(std::string_view text, bool& value)
{
    if (!_panel.open)
        return false;

    const Item it = _item(text, _style.rowHeight);

    if (it.activated)
        value = !value;

    if (it.focused)
        _focusRing(it.rect);

    _textAt(text, _marker(it.rect, it.hovered, value, kCheckboxInset, kCheckboxFill).max.x +
                      _style.padding,
            it.rect);

    return it.activated;
}

bool Context::Impl::radioButton(std::string_view label, int& value, int buttonValue)
{
    if (!_panel.open)
        return false;

    const Item it = _item(label, _style.rowHeight);

    const bool takes = it.activated && value != buttonValue;
    if (it.activated)
        value = buttonValue;

    if (it.focused)
        _focusRing(it.rect);

    _textAt(label, _marker(it.rect, it.hovered, value == buttonValue, kRadioInset, kRadioFill)
                           .max.x +
                       _style.padding,
            it.rect);

    return takes;
}

bool Context::Impl::selectable(std::string_view label, bool selected)
{
    if (!_panel.open)
        return false;

    const Item it = _item(label, _style.rowHeight);

    if (selected)
        _quad(it.rect, _style.controlActive);
    else if (it.hovered)
        _quad(it.rect, _style.controlHovered);

    if (it.focused)
        _focusRing(it.rect);

    _textAt(label, it.rect.min.x + _style.padding * 0.5f, it.rect);

    return it.activated;
}

bool Context::Impl::sliderFloat(std::string_view text, float& value, float min, float max)
{
    if (!_panel.open || !(max > min))
        return false;

    const Item it = _item(text, _style.rowHeight);

    //! Focus on the press, not the click: a drag that ends off the track still
    //! leaves the slider ready for arrow keys.
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

    _quad(it.rect, _fill(false, it.hovered));

    if (const float filled = (value - min) / (max - min); filled > 0.0f)
    {
        _quad({it.rect.min, {it.rect.min.x + filled * it.rect.width(), it.rect.max.y}},
              _style.accent);
    }

    _caption.assign(text);
    _caption += ": ";
    appendFloat(_caption, value);

    if (it.focused)
        _focusRing(it.rect);

    _textCentered(_caption, it.rect);

    return changed;
}

bool Context::Impl::inputText(std::string_view label, std::string& value, size_t maxBytes)
{
    if (!_panel.open)
        return false;

    const Item it = _item(label, _style.rowHeight, /*trailingLabel=*/true);
    const Rect row = it.rect;

    if (it.held && _pointer.pressed)
        _setFocus(it.id);
    else if (_pointer.pressed && !it.hovered && it.focused)
        _clearFocus(it.id);

    TextState& state = _textStates.touch(it.id, _frame).value;

    //! The frame focus is gained: snapshot the value for Escape, and put the
    //! caret at the end as every platform's field does.
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

    _quad(row, _fill(it.focused, it.hovered));

    const float inset = _style.padding * 0.5f;
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

        _quad({{textOrigin.x + from, row.min.y + 2.0f}, {textOrigin.x + to, row.max.y - 2.0f}},
              _style.accent);
    }

    _text(value, textOrigin);

    if (it.focused)
    {
        const float x = textOrigin.x + caretX;
        _quad({{x, row.min.y + 2.0f}, {x + 1.0f, row.max.y - 2.0f}}, _style.text);
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

    _textAt(label, row.max.x + _style.padding, row);

    return changed;
}

bool Context::Impl::inputFloat(std::string_view label, float& value)
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

    if (!inputText(label, buffer, kNumericMaxBytes))
        return false;

    const char* begin = buffer.c_str();
    char* end = nullptr;
    const float parsed = std::strtof(begin, &end);

    if (end == begin || !std::isfinite(parsed) || parsed == value)
        return false;

    value = parsed;
    return true;
}

bool Context::Impl::dropdown(std::string_view label, int& index, std::span<const std::string_view> items)
{
    if (!_panel.open || items.empty())
        return false;

    index = std::clamp(index, 0, static_cast<int>(items.size()) - 1);

    const Item it = _item(label, _style.rowHeight, /*trailingLabel=*/true);
    const Rect row = it.rect;

    u32& open = _state(it.id, 0u);
    if (it.activated)
        open ^= 1u;

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

    _quad(row, _fill(open != 0u, it.hovered));

    _textAt(items[static_cast<size_t>(index)], row.min.x + _style.padding * 0.5f, row);
    _textAt("v", row.max.x - _style.padding * 1.5f, row);
    _textAt(label, row.max.x + _style.padding, row);

    if (it.focused)
        _focusRing(row);

    if (!open)
        return changed;

    const float itemHeight = _style.rowHeight;
    const Rect list{{row.min.x, row.max.y},
                    {row.max.x, row.max.y + itemHeight * static_cast<float>(items.size())}};

    if (list.contains(_input.mouse))
        _capture.mouseThisFrame = true;

    //! Clipped to the list rather than to the panel, and deferred, so the open
    //! list draws over whatever follows it.
    const Rect savedClip = std::exchange(_clip, list);
    _deferring = true;

    _quad(list, _style.panelBackground);

    for (size_t i = 0; i < items.size(); ++i)
    {
        const Rect entryRow{{list.min.x, list.min.y + itemHeight * static_cast<float>(i)},
                            {list.max.x, list.min.y + itemHeight * static_cast<float>(i + 1)}};

        const Item hit = _behaviour(hashBytes(items[i], it.id) ^ 0x51ed270bu, entryRow);

        if (hit.hovered)
            _quad(entryRow, _style.controlHovered);
        else if (static_cast<int>(i) == index)
            _quad(entryRow, _style.controlActive);

        _textAt(items[i], entryRow.min.x + _style.padding * 0.5f, entryRow);

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
}

bool Context::Impl::collapsingHeader(std::string_view label, bool defaultOpen)
{
    if (!_panel.open)
        return false;

    const Item it = _item(label, _style.rowHeight);

    u32& open = _state(it.id, defaultOpen ? 1u : 0u);
    if (it.activated)
        open ^= 1u;

    _quad(it.rect, it.hovered ? _style.controlHovered : _style.panelTitle);

    if (it.focused)
        _focusRing(it.rect);

    _textAt(open ? "-" : "+", it.rect.min.x + _style.padding * 0.5f, it.rect);
    _textAt(label, it.rect.min.x + _style.padding * 2.0f, it.rect);

    return open != 0u;
}

bool Context::Impl::treeNode(std::string_view label, bool defaultOpen)
{
    if (!_panel.open)
        return false;

    const Item it = _item(label, _style.rowHeight);

    u32& open = _state(it.id, defaultOpen ? 1u : 0u);
    if (it.activated)
        open ^= 1u;

    if (it.hovered)
        _quad(it.rect, _style.controlHovered);

    if (it.focused)
        _focusRing(it.rect);

    _textAt(open ? "v" : ">", it.rect.min.x, it.rect);
    _textAt(label, it.rect.min.x + _style.padding * 1.5f, it.rect);

    if (open)
    {
        _panel.indent += _style.padding * 1.5f;
        ++_panel.treeDepth;
    }

    return open != 0u;
}

void Context::Impl::treePop() noexcept
{
    if (_panel.treeDepth == 0)
        return;

    --_panel.treeDepth;
    _panel.indent = std::max(0.0f, _panel.indent - _style.padding * 1.5f);
}

bool Context::Impl::beginScroll(std::string_view id, float height)
{
    if (!_panel.open)
        return false;

    const u32 widgetId = _idFor(id);
    const Rect viewport = _nextRow(std::max(height, _style.rowHeight));

    ScrollState& state = _scrollStates.touch(widgetId, _frame).value;

    const float maxOffset = std::max(0.0f, state.contentHeight - viewport.height());

    if (_input.scroll != 0.0f && _hovering(viewport))
    {
        state.offset -= _input.scroll * kScrollStep;
        _input.scroll = 0.0f;
        _capture.mouseThisFrame = true;
    }

    state.offset = std::clamp(state.offset, 0.0f, maxOffset);

    _quad(viewport, {0.0f, 0.0f, 0.0f, 0.25f});

    if (maxOffset > 0.0f)
    {
        const Rect track{{viewport.max.x - kScrollbarWidth, viewport.min.y}, viewport.max};

        const float thumbHeight =
            std::max(track.height() * viewport.height() / state.contentHeight, 16.0f);
        const float travel = track.height() - thumbHeight;
        const float thumbTop = track.min.y + travel * (state.offset / maxOffset);

        const Item drag = _behaviour(hashBytes("##scrollbar", widgetId), track);
        if (drag.held)
        {
            //! The grab point is the thumb's centre, so the thumb lands under
            //! the cursor rather than jumping by half its height.
            const float t = std::clamp((_input.mouse.y - track.min.y - thumbHeight * 0.5f) /
                                           std::max(travel, 1.0f),
                                       0.0f, 1.0f);
            state.offset = t * maxOffset;
        }

        _quad(track, {1.0f, 1.0f, 1.0f, 0.06f});
        _quad({{track.min.x, thumbTop}, {track.max.x, thumbTop + thumbHeight}},
              _fill(drag.held, drag.hovered));
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
    _panel.cursorY = frame.viewport.max.y + _style.itemSpacing;
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

    //! Tabs are placed by hand rather than through _nextRow(): they are fitted
    //! to their labels and packed left to right, so the row is walked.
    _panel.rowTop = _panel.cursorY;
    _panel.rowHeight = _style.rowHeight;
    _panel.cursorY += _style.rowHeight + _style.itemSpacing;

    _tabBar.cursorX = _panel.bounds.min.x + _style.padding + _panel.indent;

    return true;
}

bool Context::Impl::tabItem(std::string_view label)
{
    if (!_tabBar.open)
        return false;

    const u32 index = _tabBar.index++;
    u32& selected = _state(_tabBar.id, 0u);

    const float width = measureText(label).x + _style.padding * 2.0f;

    const Rect tab{{_tabBar.cursorX, _panel.rowTop},
                   {_tabBar.cursorX + width, _panel.rowTop + _style.rowHeight}};

    _tabBar.cursorX += width + 2.0f;

    Item it = _behaviour(hashBytes(label, _tabBar.id), tab);
    _focusItem(it);

    if (it.activated)
        selected = index;

    const bool active = selected == index;

    _quad(tab, _fill(active, it.hovered));

    if (it.focused)
        _focusRing(tab);

    _textCentered(label, tab);

    return active;
}

void Context::Impl::endTabBar() noexcept
{
    _tabBar.open = false;
}

void Context::Impl::tooltip(std::string_view text)
{
    if (!_panel.open || !_hovering(_panel.lastWidget))
        return;

    constexpr float kCursorGap = 12.0f;

    const float pad = _style.padding * 0.5f;
    const glm::vec2 corner = _input.mouse + kCursorGap;

    const Rect box{corner, corner + measureText(text) + pad * 2.0f};

    const Rect savedClip = std::exchange(_clip, box);
    _deferring = true;

    _quad(box, {0.05f, 0.05f, 0.07f, 0.96f});
    _text(text, {box.min.x + pad, box.min.y + pad});

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

    walkGlyphs(*_atlas, text, _style.textScale,
               [&](const GlyphInfo& glyph, float penX, size_t offsetAfter) {
                   const float trailingEdge = originX + penX + glyph.advance * _style.textScale;

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
        walkGlyphs(*_atlas, text, _style.textScale, [](const GlyphInfo&, float, size_t) {});

    return {width, _lineHeight()};
}

float Context::Impl::_lineHeight() const noexcept
{
    return _atlas ? _atlas->lineHeight() * _style.textScale : 0.0f;
}

void Context::Impl::_quad(const Rect& bounds, const glm::vec4& color)
{
    _texturedQuad(bounds, _solidUv, _solidUv, color);
}

void Context::Impl::_texturedQuad(Rect bounds, glm::vec2 uvMin, glm::vec2 uvMax, const glm::vec4& color)
{
    const glm::vec2 size = bounds.size();
    if (size.x <= 0.0f || size.y <= 0.0f)
        return;

    if (_deferring)
    {
        _overlayQuads.push_back(DeferredQuad{bounds, uvMin, uvMax, color, _clip});
        return;
    }

    const Rect visible = intersect(bounds, _clip);
    if (visible.width() <= 0.0f || visible.height() <= 0.0f)
        return;

    //! Quads are axis-aligned, so trimming the rectangle and moving the UVs by
    //! the same fractions is exact: the surviving texels are pixel-identical to
    //! what a scissor rectangle would have produced.
    const glm::vec2 uv0{lerp(uvMin.x, uvMax.x, (visible.min.x - bounds.min.x) / size.x),
                        lerp(uvMin.y, uvMax.y, (visible.min.y - bounds.min.y) / size.y)};
    const glm::vec2 uv1{lerp(uvMin.x, uvMax.x, (visible.max.x - bounds.min.x) / size.x),
                        lerp(uvMin.y, uvMax.y, (visible.max.y - bounds.min.y) / size.y)};

    const auto base = static_cast<u32>(_vertices.size());

    //! Top-left, top-right, bottom-right, bottom-left. endPanel() rewrites
    //! vertices 2 and 3 of a panel background, so this order is load-bearing.
    _vertices.push_back({{visible.min.x, visible.min.y}, {uv0.x, uv0.y}, color});
    _vertices.push_back({{visible.max.x, visible.min.y}, {uv1.x, uv0.y}, color});
    _vertices.push_back({{visible.max.x, visible.max.y}, {uv1.x, uv1.y}, color});
    _vertices.push_back({{visible.min.x, visible.max.y}, {uv0.x, uv1.y}, color});

    _indices.push_back(base + 0);
    _indices.push_back(base + 1);
    _indices.push_back(base + 2);
    _indices.push_back(base + 2);
    _indices.push_back(base + 3);
    _indices.push_back(base + 0);
}

void Context::Impl::_text(std::string_view text, glm::vec2 origin)
{
    if (!_atlas || text.empty())
        return;

    _vertices.reserve(_vertices.size() + text.size() * kVerticesPerQuad);
    _indices.reserve(_indices.size() + text.size() * kIndicesPerQuad);

    const float scale = _style.textScale;

    walkGlyphs(*_atlas, text, scale, [&](const GlyphInfo& glyph, float penX, size_t) {
        if (glyph.size.x <= 0.0f || glyph.size.y <= 0.0f)
            return;

        const glm::vec2 topLeft{origin.x + penX + glyph.bearing.x * scale,
                                origin.y + glyph.bearing.y * scale};

        _texturedQuad({topLeft, topLeft + glyph.size * scale}, glyph.uvMin, glyph.uvMax,
                      _style.text);
    });
}

void Context::Impl::_textAt(std::string_view text, float x, const Rect& bounds)
{
    _text(text, {x, bounds.min.y + (bounds.height() - _lineHeight()) * 0.5f});
}

void Context::Impl::_textCentered(std::string_view text, const Rect& bounds)
{
    const glm::vec2 size = measureText(text);
    _textAt(text, bounds.min.x + (bounds.width() - size.x) * 0.5f, bounds);
}

void Context::Impl::_focusRing(const Rect& bounds)
{
    constexpr float kThickness = 1.0f;
    const glm::vec4& color = _style.accent;

    _quad({bounds.min, {bounds.max.x, bounds.min.y + kThickness}}, color);
    _quad({{bounds.min.x, bounds.max.y - kThickness}, bounds.max}, color);
    _quad({bounds.min, {bounds.min.x + kThickness, bounds.max.y}}, color);
    _quad({{bounds.max.x - kThickness, bounds.min.y}, bounds.max}, color);
}

Rect Context::Impl::_marker(const Rect& row, bool hovered, bool on, float rowInset, float fillScale)
{
    const float size = std::max(_style.rowHeight - rowInset, 4.0f);
    const float top = row.min.y + (row.height() - size) * 0.5f;

    const Rect box{{row.min.x, top}, {row.min.x + size, top + size}};

    _quad(box, _fill(false, hovered));

    if (on)
    {
        const float inset = std::max(size * fillScale, 2.0f);
        _quad({box.min + inset, box.max - inset}, _style.accent);
    }

    return box;
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
    : _impl(std::make_unique<Impl>(renderer, desc))
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

void Context::label(std::string_view text) { _impl->label(text); }
bool Context::button(std::string_view text) { return _impl->button(text); }
bool Context::checkbox(std::string_view text, bool& value) { return _impl->checkbox(text, value); }

bool Context::sliderFloat(std::string_view text, float& value, float min, float max)
{
    return _impl->sliderFloat(text, value, min, max);
}

bool Context::inputText(std::string_view label, std::string& value, size_t maxBytes)
{
    return _impl->inputText(label, value, maxBytes);
}

bool Context::inputFloat(std::string_view label, float& value)
{
    return _impl->inputFloat(label, value);
}

bool Context::dropdown(std::string_view label, int& index, std::span<const std::string_view> items)
{
    return _impl->dropdown(label, index, items);
}

bool Context::radioButton(std::string_view label, int& value, int buttonValue)
{
    return _impl->radioButton(label, value, buttonValue);
}

bool Context::collapsingHeader(std::string_view label, bool defaultOpen)
{
    return _impl->collapsingHeader(label, defaultOpen);
}

bool Context::treeNode(std::string_view label, bool defaultOpen)
{
    return _impl->treeNode(label, defaultOpen);
}

void Context::treePop() noexcept { _impl->treePop(); }

bool Context::selectable(std::string_view label, bool selected)
{
    return _impl->selectable(label, selected);
}

bool Context::beginScroll(std::string_view id, float height)
{
    return _impl->beginScroll(id, height);
}

void Context::endScroll() { _impl->endScroll(); }
bool Context::beginTabBar(std::string_view id) { return _impl->beginTabBar(id); }
bool Context::tabItem(std::string_view label) { return _impl->tabItem(label); }
void Context::endTabBar() noexcept { _impl->endTabBar(); }
void Context::tooltip(std::string_view text) { _impl->tooltip(text); }

void Context::sameLine() noexcept { _impl->sameLine(); }
void Context::setNextItemWidth(float width) noexcept { _impl->setNextItemWidth(width); }
void Context::separator() { _impl->separator(); }
void Context::spacing(float pixels) noexcept { _impl->spacing(pixels); }
void Context::setKeyboardFocusHere() noexcept { _impl->setKeyboardFocusHere(); }

bool Context::isCapturingKeyboard() const noexcept { return _impl->isCapturingKeyboard(); }
bool Context::isCapturingTextInput() const noexcept { return _impl->isCapturingTextInput(); }
bool Context::isCapturingMouse() const noexcept { return _impl->isCapturingMouse(); }

Style& Context::style() noexcept { return _impl->style(); }
const Style& Context::style() const noexcept { return _impl->style(); }

glm::vec2 Context::measureText(std::string_view text) { return _impl->measureText(text); }

} // namespace aura3d::ui
