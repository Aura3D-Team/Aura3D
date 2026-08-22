#ifndef AURA_UI_AURAUI_H
#define AURA_UI_AURAUI_H

#pragma once

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <wma/wma.hpp>

#include "aura/Core/AuraFont/FontAtlas.h"
#include "aura/Renderer/IRenderer.h"
#include "aura/Utils/AlignedVector.h"

/**
 * @file AuraUI.h
 * @brief Aura3D's built-in immediate-mode user interface.
 *
 * Built on FontAtlas (glyph cache) and IRenderer::drawBatch2D() (2D batch
 * submission), speaking only the public IRenderer API -- no third-party
 * dependency, identical output on every backend and platform.
 *
 * Immediate mode: no retained widget tree, no callbacks. Widgets are
 * function calls that both draw and report what the user did, rebuilt from
 * scratch every frame:
 *
 * @code
 * ui.newFrame();
 * if (ui.beginPanel("Renderer", {16.0f, 16.0f}, 240.0f))
 * {
 *     ui.label("Backend: Vulkan");
 *     ui.checkbox("Wireframe", wireframe);
 *     ui.sliderFloat("Exposure", exposure, 0.0f, 4.0f);
 *     if (ui.button("Reload shaders"))
 *         reloadShaders();
 *     ui.endPanel();
 * }
 * ui.render();
 * @endcode
 */

namespace aura3d::ui {

/**
 * @brief An axis-aligned rectangle in window pixels, (0,0) at the top-left.
 */
struct Rect {
    glm::vec2 min{0.0f}; //! Top-left corner, inclusive.
    glm::vec2 max{0.0f}; //! Bottom-right corner, exclusive.

    [[nodiscard]] constexpr float width() const noexcept { return max.x - min.x; }
    [[nodiscard]] constexpr float height() const noexcept { return max.y - min.y; }

    [[nodiscard]] constexpr bool contains(glm::vec2 p) const noexcept
    {
        return p.x >= min.x && p.x < max.x && p.y >= min.y && p.y < max.y;
    }
};

/// Colours and metrics shared by every widget. Defaults are a neutral dark
/// theme. Mutate through Context::style(); takes effect on the next widget
/// drawn.
struct Style {
    glm::vec4 panelBackground{0.09f, 0.10f, 0.12f, 0.94f};
    glm::vec4 panelTitle{0.16f, 0.18f, 0.22f, 1.00f};
    glm::vec4 control{0.20f, 0.22f, 0.27f, 1.00f};
    glm::vec4 controlHovered{0.27f, 0.30f, 0.37f, 1.00f};
    glm::vec4 controlActive{0.20f, 0.45f, 0.75f, 1.00f};
    glm::vec4 accent{0.24f, 0.52f, 0.85f, 1.00f}; //! Slider fill, check mark.
    glm::vec4 text{0.92f, 0.93f, 0.95f, 1.00f};
    glm::vec4 separator{1.00f, 1.00f, 1.00f, 0.12f};

    float rowHeight = 22.0f;    //! Height of one widget row.
    float itemSpacing = 4.0f;   //! Vertical gap between rows.
    float padding = 8.0f;       //! Panel border to content, all four sides.
    float textScale = 1.0f;     //! Multiplier on the atlas rasterization size.
};

/// One key that went down (or auto-repeated) during a frame. Only presses and
/// repeats are carried (repeat is what makes a held Backspace keep deleting);
/// releases are not recorded.
struct KeyPress {
    wma::Key key = wma::KEY_UNKNOWN;
    wma::KeyModifiers mods{};
};

/**
 * @brief Per-frame input the UI reacts to.
 *
 * Fill this yourself and hand it to Context::newFrame(const Input&), or let
 * Context::attachInput() keep an internal copy fed from wma.
 *
 * Everything here is *per frame*: edges and accumulated deltas since the last
 * newFrame(), not a level to sample.
 */
struct Input {
    glm::vec2 mouse{0.0f};  //! Cursor position in window pixels.
    bool mouseDown = false; //! Primary (left) button held this frame.

    /// Wheel movement accumulated this frame, in notches. Positive scrolls
    /// content up.
    float scroll = 0.0f;

    /// Keys pressed or repeated this frame, in arrival order (order matters:
    /// Home then Shift+End must select the whole line).
    std::vector<KeyPress> keys;

    /// Text committed this frame, UTF-8. Layout-, dead-key- and IME-correct,
    /// since it comes from the platform's text input, not translated keycodes.
    std::string text;

    //! Drops the per-frame edges while keeping the levels (cursor position and
    //! button state), which is exactly what must survive into the next frame.
    void clearFrameEvents() noexcept
    {
        scroll = 0.0f;
        keys.clear();
        text.clear();
    }
};

/**
 * @brief Construction parameters for a @ref Context.
 */
struct ContextDesc {
    /// Path to a .ttf/.otf file. Empty, or a file that fails to load, falls
    /// back to the embedded bitmap font.
    std::string fontPath;

    float pixelHeight = 16.0f; //! Glyph rasterization size, in pixels.
    u32 atlasSize = 1024;      //! Edge length of the (square) glyph atlas.
};

/**
 * @class Context
 * @brief The immediate-mode UI itself: state between frames, and the widgets.
 *
 * newFrame() latches input and resets layout, widget calls accumulate
 * geometry, and render() submits it. Call render() between beginRenderPass()
 * and endRenderPass(), after the scene's own draws (the UI composites with
 * straight alpha and no depth test).
 *
 * The whole UI is **one** IRenderer::drawBatch2D() call regardless of panel
 * count: solid quads sample the opaque cell FontAtlas::solidTexelUv()
 * reserves, so rectangles and glyphs share one texture. Vertex/index buffers
 * are retained across frames, so a steady-state UI allocates nothing.
 *
 * @note Not thread-safe, and not re-entrant: one Context drives one UI on one
 *       thread, like the renderer it draws through.
 */
class Context {
public:
    /**
     * @brief Builds the UI, its glyph atlas and its atlas texture.
     *
     * Never throws. A font that fails to load degrades to the embedded bitmap
     * font; a renderer that can't allocate the atlas texture leaves the
     * context inert (widgets return sensible values, nothing draws) rather
     * than failing construction.
     *
     * @param renderer Renderer to draw through; must outlive this object.
     * @param desc Font and atlas parameters; see @ref ContextDesc.
     */
    explicit Context(IRenderer* renderer, const ContextDesc& desc = ContextDesc{});
    ~Context();

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&&) = delete;
    Context& operator=(Context&&) = delete;

    /**
     * @brief Subscribes to @p windowManager's mouse so newFrame() needs no
     *        arguments.
     *
     * Binds the primary button and tracks cursor motion in the mouse
     * listener's currently active input context -- push a dedicated context
     * first if the application already binds the left button there. Cursor
     * position is polled, not bound, so it stays correct while another
     * subsystem owns the move callback.
     *
     * @param windowManager Window manager to read input from; must outlive
     *        this object.
     */
    void attachInput(wma::IWindowManager& windowManager);

    /// Opens a frame: latches input edges, clears last frame's geometry.
    /// Uses the state maintained by attachInput(); with none attached, the
    /// UI simply sees no cursor and no clicks.
    void newFrame() noexcept;

    /// As above, driven by input the caller supplies rather than by wma.
    void newFrame(const Input& input) noexcept;

    /// Submits the frame's geometry as a single 2D batch. Call between
    /// beginRenderPass() and endRenderPass(), after the scene. No-op if no
    /// widget produced geometry.
    void render();

    /**
     * @brief Begins a draggable, auto-height panel.
     *
     * Remembers its position across frames, keyed by @p title; @p
     * defaultPosition only applies the first time it is seen. Height follows
     * the content. Pair with endPanel() only when this returns true.
     *
     * @param title Title bar text, and the panel's identity across frames.
     * @param defaultPosition Top-left corner in window pixels, first frame only.
     * @param width Panel width in window pixels.
     * @return true when the panel is open and its contents should be emitted.
     */
    [[nodiscard]] bool beginPanel(std::string_view title,
                                  glm::vec2 defaultPosition,
                                  float width = 240.0f);

    /// Closes the panel opened by the matching beginPanel().
    void endPanel();

    /// Draws one row of static text.
    void label(std::string_view text);

    /**
     * @brief A full-width push button.
     * @return true on the frame the press is released over the button.
     */
    bool button(std::string_view text);

    /**
     * @brief A labelled check box bound to @p value.
     * @param[in,out] value Toggled in place when the box is clicked.
     * @return true on the frame @p value changed.
     */
    bool checkbox(std::string_view text, bool& value);

    /**
     * @brief A labelled horizontal slider bound to @p value.
     *
     * Label and current value are drawn on the track. Dragging is tracked
     * until release even if the cursor leaves the track, so a fast drag
     * doesn't snap back.
     *
     * @param[in,out] value Clamped into [@p min, @p max] and updated in place.
     * @param min Lower bound of the track.
     * @param max Upper bound of the track; must be greater than @p min.
     * @return true on any frame @p value changed.
     */
    bool sliderFloat(std::string_view text, float& value, float min, float max);

    /**
     * @brief An editable single-line text field bound to @p value.
     *
     * Reads the platform's committed-text stream (correct through dead keys
     * and IME) rather than translating keycodes. Supports caret/selection,
     * Left/Right (Ctrl: by word, Shift: extend), Home/End, Backspace/Delete,
     * Ctrl+A, and typing over a selection to replace it.
     *
     * Focus is claimed by click or Tab; Enter and Escape both release it,
     * Escape after reverting the edit.
     *
     * @param[in]     label Drawn to the left, and the field's identity.
     * @param[in,out] value Edited in place, always valid UTF-8.
     * @param[in]     maxBytes Upper bound on @p value's size; further input is
     *                dropped rather than truncating a character mid-sequence.
     * @return true on any frame @p value changed.
     */
    bool inputText(std::string_view label, std::string& value, size_t maxBytes = 1024);

    /**
     * @brief A text field that parses its contents as a number.
     *
     * Keeps the *text* between frames rather than reformatting @p value every
     * keystroke -- otherwise typing "1." would be rewritten to "1" and the
     * decimal point could never be entered.
     *
     * @param[in,out] value Updated whenever the field parses to a number.
     * @return true on any frame @p value changed.
     */
    bool inputFloat(std::string_view label, float& value);

    /**
     * @brief A drop-down list of @p items bound to @p index.
     *
     * The open list is drawn over whatever follows it, and closes on selection
     * or on a click elsewhere.
     *
     * @param[in,out] index Selected element; clamped into @p items' range.
     * @return true on the frame the selection changed.
     */
    bool dropdown(std::string_view label, int& index, std::span<const std::string_view> items);

    /**
     * @brief One button of a radio group bound to @p value.
     * @param[in,out] value Set to @p buttonValue when this button is clicked.
     * @return true on the frame this button took the selection.
     */
    bool radioButton(std::string_view label, int& value, int buttonValue);

    /// A clickable header that shows or hides the widgets below it. Open
    /// state is retained by the context; emit contents inside
    /// `if (collapsingHeader(...))`.
    /// @param defaultOpen Applied the first time this header is seen.
    /// @return true when the section is open and its contents should be emitted.
    [[nodiscard]] bool collapsingHeader(std::string_view label, bool defaultOpen = true);

    /// An indented, collapsible node for hierarchical data. Pair with
    /// treePop() only when this returns true, like beginPanel()/endPanel().
    /// Nesting is unlimited; each level indents by the style's padding.
    /// @return true when the node is expanded and its children should be emitted.
    [[nodiscard]] bool treeNode(std::string_view label, bool defaultOpen = false);

    /// Closes the node opened by the matching treeNode().
    void treePop() noexcept;

    /**
     * @brief A selectable row, for lists.
     * @param[in] selected Whether to draw this row as the current selection.
     * @return true on the frame the row is clicked.
     */
    bool selectable(std::string_view label, bool selected);

    /**
     * @brief Begins a fixed-height, clipped, scrollable region.
     *
     * Content taller than @p height is clipped and reachable by the wheel or
     * scrollbar, rather than growing the panel without bound. Scroll offset
     * is retained by the context, keyed by @p id. Pair with endScroll() only
     * when this returns true.
     *
     * @param id Identity across frames; not drawn as a label.
     * @param height Visible height in pixels.
     * @return true when the region is open and its contents should be emitted.
     */
    [[nodiscard]] bool beginScroll(std::string_view id, float height);

    /// Closes the region opened by the matching beginScroll().
    void endScroll();

    /// Begins a row of tabs; pair with endTabBar() when it returns true.
    /// Selected tab is retained by the context; emit each tab's contents
    /// inside `if (tabItem("..."))`.
    [[nodiscard]] bool beginTabBar(std::string_view id);

    /// @return true when @p label is the selected tab in the current bar.
    [[nodiscard]] bool tabItem(std::string_view label);

    /// Closes the bar opened by the matching beginTabBar().
    void endTabBar();

    /// Shows @p text in a floating box beside the cursor. Call immediately
    /// after the widget it describes; draws only while that widget is hovered.
    void tooltip(std::string_view text);

    /**
     * @brief Keeps the next widget on the current row rather than starting one.
     *
     * The next widget begins just after the one just emitted and runs to the
     * row's right edge -- an already-emitted widget cannot be narrowed after
     * the fact, so an even split sizes the *first* widget instead, with
     * setNextItemWidth():
     *
     * @code
     * ui.setNextItemWidth(halfWidth);
     * ui.button("Apply");
     * ui.sameLine();
     * ui.button("Cancel");   // takes the rest of the row
     * @endcode
     */
    void sameLine() noexcept;

    /// Sets the width of the next widget, in pixels. Applies once, then
    /// forgotten. Clamped to the space available.
    void setNextItemWidth(float width) noexcept;

    /// A horizontal rule spanning the content width.
    void separator();

    /// Advances the layout cursor by @p pixels without drawing anything.
    void spacing(float pixels) noexcept;

    /// Gives keyboard focus to the next focusable widget submitted. For
    /// opening a panel with a field already active.
    void setKeyboardFocusHere() noexcept { _focus.requestNext = true; }

    /// True when the UI is consuming the keyboard and the application should
    /// not (gate movement keys on this). True whenever any widget holds
    /// keyboard focus. Reports the most recently completed frame.
    [[nodiscard]] bool isCapturingKeyboard() const noexcept { return _capture.keyboard; }

    /// True when the UI needs the platform's text input active. Drive
    /// IWindowManager::setTextInputEnabled() from this if not using
    /// attachInput(), which does it automatically.
    [[nodiscard]] bool isCapturingTextInput() const noexcept { return _capture.textInput; }

    /// True when the UI is using the mouse and the application should not
    /// (covers hovering any panel and holding a widget whose drag has since
    /// left it). Gate camera look and world picking on this. Reports the
    /// most recently completed frame.
    [[nodiscard]] bool isCapturingMouse() const noexcept { return _capture.mouse; }

    /// Mutable theme; see @ref Style.
    [[nodiscard]] Style& style() noexcept { return _style; }
    [[nodiscard]] const Style& style() const noexcept { return _style; }

    /// Pixel size one line of @p text occupies at the current text scale.
    /// Rasterizes any glyph not yet cached, so not @c const. Line breaks are
    /// not interpreted.
    [[nodiscard]] glm::vec2 measureText(std::string_view text);

private:
    /// Interaction outcome shared by every clickable widget.
    struct Interaction {
        bool hovered = false; //! Cursor is over the widget right now.
        bool held = false;    //! Widget owns the ongoing press.
        bool clicked = false; //! Press was released over the widget this frame.
    };

    /// Runs the hot/active state machine for @p id over @p bounds.
    [[nodiscard]] Interaction _behaviour(u32 id, const Rect& bounds) noexcept;

    /// Enters @p id into this frame's tab order and resolves pending focus
    /// moves. Called by every focusable widget in submission order, which
    /// defines the tab order (there is no widget tree to walk).
    /// @return true when @p id holds keyboard focus this frame.
    bool _focusable(u32 id) noexcept;

    //! Gives @p id keyboard focus. The value snapshot Escape reverts to is
    //! taken by inputText() on the frame it observes the gain, not here: only
    //! the field knows what its own revertible state is.
    void _setFocus(u32 id) noexcept;

    /// Drops keyboard focus if @p id holds it.
    void _clearFocus(u32 id) noexcept;

    /// True when a focused widget should act on this frame's keys: Enter,
    /// keypad Enter, or Space. Shared by every focusable widget.
    /// @param focused Whether the calling widget holds focus; false short-circuits.
    [[nodiscard]] bool _activated(bool focused) const noexcept;

    //! True when @p key was pressed or repeated this frame. @p mods must match
    //! exactly, so Ctrl+A does not also fire the plain-A binding.
    [[nodiscard]] bool _keyPressed(wma::Key key) const noexcept;
    [[nodiscard]] bool _keyPressed(wma::Key key, wma::KeyModifiers mods) const noexcept;

    /// Reserves the next full-width row inside the current panel.
    [[nodiscard]] Rect _nextRow(float height) noexcept;

    //! Byte offset of the caret nearest @p x within @p text drawn at @p originX.
    [[nodiscard]] size_t _caretFromX(std::string_view text, float originX, float x);

    //! X offset of the caret at byte @p offset, relative to the text's origin.
    [[nodiscard]] float _xFromCaret(std::string_view text, size_t offset);

    /// Derives the identity a widget keeps across frames: label, enclosing
    /// panel, and a per-panel counter, so same-labelled widgets don't collide.
    [[nodiscard]] u32 _idFor(std::string_view text) noexcept;

    /// Baseline-to-baseline distance at the current text scale, in pixels.
    [[nodiscard]] float _lineHeight() const noexcept;

    /// True when the cursor is inside @p bounds and inside the active clip.
    [[nodiscard]] bool _hovering(const Rect& bounds) const noexcept;

    /// Draws a focus outline around @p bounds, for the keyboard-focused widget.
    void _focusRing(const Rect& bounds);

    /// Appends a solid quad, clipped to the active clip rectangle.
    void _quad(const Rect& bounds, const glm::vec4& color);

    /// Appends @p text's glyphs with their top-left corner at @p origin.
    void _text(std::string_view text, glm::vec2 origin, const glm::vec4& color);

    /// Appends @p text centred in @p bounds, both axes.
    void _textCentered(std::string_view text, const Rect& bounds, const glm::vec4& color);

    /// Appends one textured quad clipped to @p _clip, on the CPU rather than
    /// a scissor rectangle (drawBatch2D() is one batch with no per-command
    /// state, so a scissor would mean one draw call per clip change).
    void _texturedQuad(Rect bounds, glm::vec2 uvMin, glm::vec2 uvMax, const glm::vec4& color);

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

    Input _input{};             //! Latched at newFrame(), constant for the frame.
    Input _pendingInput{};      //! Written by attachInput()'s callbacks.
    //! Non-owning; set by attachInput() and polled for the cursor position.
    wma::MouseListener* _mouse = nullptr;
    //! Non-owning; set by attachInput(), drives isCapturingTextInput() so a
    //! soft keyboard is raised only while a field is focused.
    wma::IWindowManager* _window = nullptr;

    /// One attachInput() subscription, per device context. Kept so
    /// ~Context() can withdraw them (every wma callback captures @c this).
    /// A vector because attachInput() may run more than once (ui::InputRouter
    /// calls it per UI-enabled mode).
    struct Attachment {
        wma::InputContextId keys{};
        wma::InputContextId pointer{};
        wma::InputContextId touch{};
    };

    std::vector<Attachment> _attachments;

    /// The pointer, and which widget it currently owns. One struct since
    /// they are only ever read/written as a unit.
    struct PointerState {
        //! Resolved one frame late: the last widget to claim the cursor wins
        //! (the topmost, since panels draw back to front); resolving it
        //! within the frame would instead favor whichever was submitted first.
        u32 hot = 0;     //! Widget under the cursor, from last frame's claims.
        u32 nextHot = 0; //! Claims made this frame; becomes @c hot next frame.
        u32 active = 0;  //! Widget owning the ongoing press, if any.

        //! Whether the active widget was submitted last frame. A widget that
        //! vanishes mid-press (panel closed, branch stopped emitting it)
        //! never observes the release that would clear @c active otherwise.
        bool activeSubmitted = false;

        bool previousDown = false; //! Button state as of the previous frame.
        bool pressed = false;      //! Button went down this frame.

        //! A finger is down: the cursor comes from the touch stream instead
        //! of the (stale) polled mouse position.
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
    /// frames like panel positions do. The rest is traversal bookkeeping
    /// _focusable() builds as widgets are emitted -- immediate mode discovers
    /// tab order only in submission order.
    struct FocusState {
        u32 current = 0;
        //! Focus at the end of the previous frame. A widget compares against
        //! this to detect the frame it *gains* focus, when a text field
        //! snapshots its value for Escape to revert to.
        u32 lastFrame = 0;

        //! Focusable submitted immediately before the currently focused one,
        //! which is what Shift+Tab moves to.
        u32 previous = 0;
        u32 first = 0; //! First focusable of the frame, so Tab from the last wraps.
        u32 last = 0;  //! Last focusable of the frame, so Shift+Tab from the first wraps.

        //! Set when Tab is pressed (or by setKeyboardFocusHere()); the next
        //! focusable widget submitted claims focus and clears it.
        bool requestNext = false;
        //! Set when Shift+Tab is pressed; resolved against @c previous.
        bool requestPrev = false;
        //! Shift+Tab landed on the frame's first focusable, which has nothing
        //! before it; render() wraps focus to the last one instead.
        bool wrapToLast = false;

        bool submitted = false; //! Whether @c current was emitted this frame.
        //! Whether the currently focused widget has been passed in submission
        //! order; Tab only grants focus after it. See _focusable().
        bool passed = false;

        //! Drops focus held by a widget that stopped being emitted, then clears
        //! the traversal bookkeeping for the frame about to be built.
        void beginFrame() noexcept
        {
            //! A focused widget that stopped being emitted (panel closed,
            //! branch no longer reaches it) never observes anything again, so
            //! its focus is dropped; same reasoning as activeSubmitted above.
            if (current != 0 && !submitted)
                current = 0;
            submitted = false;

            //! Captured after the vanish check above, so traversal never
            //! navigates relative to a widget that is no longer being emitted.
            lastFrame = current;

            //! Tab grants focus to the first focusable submitted *after* the
            //! current one; with nothing focused, the first of the frame takes it.
            passed = current == 0;

            previous = 0;
            first = 0;
            last = 0;
        }
    };

    FocusState _focus{};

    /// What the UI consumed, as published to the application. Each flag
    /// pairs a build-time accumulator with the published value
    /// isCapturingMouse() and friends return, so those queries give the same
    /// answer anywhere in the game loop.
    struct CaptureState {
        bool mouse = false;
        bool keyboard = false;
        bool textInput = false;

        bool mouseThisFrame = false;
        bool textInputThisFrame = false;
    };

    CaptureState _capture{};

    /// Caret and selection a text field keeps between frames.
    struct TextState {
        //! Byte offsets into the edited string, always on a UTF-8 boundary.
        size_t caret = 0;
        //! Selection anchor; equal to caret means nothing is selected.
        size_t anchor = 0;
        //! Horizontal scroll within the field, so a caret past the right edge
        //! stays visible without the string being truncated.
        float scrollX = 0.0f;
        //! The value as it was when the field took focus, for Escape to revert.
        std::string original;
    };

    std::unordered_map<u32, TextState> _textStates;

    //! Runs one frame of editing on @p value for the focused field. Declared
    //! here rather than with the other helpers because it takes a TextState,
    //! which is defined just above.
    bool _editText(TextState& state, std::string& value, size_t maxBytes);

    //! Live text of an inputFloat(), kept as typed rather than reformatted
    //! from the bound float.
    std::unordered_map<u32, std::string> _numericBuffers;

    /// Scroll offset a region keeps between frames.
    struct ScrollState {
        float offset = 0.0f;
        //! Measured at the previous endScroll(), since a region's content
        //! height is not known until it has been emitted once.
        float contentHeight = 0.0f;
    };

    std::unordered_map<u32, ScrollState> _scrollStates;

    //! One small retained value per widget that needs one: open/closed for
    //! collapsing headers, tree nodes and dropdowns (0 or 1), and the
    //! selected index for tab bars. Widget ids cannot collide across kinds.
    std::unordered_map<u32, u32> _widgetValues;

    /// Position and open state a panel keeps between frames.
    struct PanelState {
        glm::vec2 position{0.0f};
        bool placed = false; //! False until defaultPosition has been applied.
    };

    std::unordered_map<u32, PanelState> _panels;

    //! State of the panel between beginPanel() and endPanel().
    struct CurrentPanel {
        u32 id = 0;
        Rect bounds{};        //! Grows downwards as widgets are added.
        float cursorY = 0.0f; //! Top of the next row, in window pixels.
        u32 widgetIndex = 0;  //! Disambiguates widgets sharing a label.
        size_t backgroundVertex = 0; //! Index of the background quad's first vertex.
        bool backgroundEmitted = false;

        //! Left inset applied to every row, grown by each open treeNode().
        float indent = 0.0f;

        //! Horizontal packing for sameLine(): marks the row still open, so the
        //! next widget reuses rowTop/rowHeight instead of starting its own row.
        //! _nextRow() consumes and clears it.
        bool packNext = false;
        float rowTop = 0.0f;
        float rowHeight = 0.0f;
        //! Width requested by setNextItemWidth(), consumed by the next row.
        //! Zero means "fill the remaining width".
        float nextWidth = 0.0f;

        //! Bounds of the widget submitted most recently, for tooltip().
        Rect lastWidget{};

        //! Whether beginPanel() is open. Kept here rather than beside the
        //! panel so one object answers "is a panel being built, and which".
        bool open = false;

        //! Depth of open treeNode()s, so treePop() restores the indent it added
        //! without the caller passing anything back. Panel state because the
        //! indent it drives is.
        u32 treeDepth = 0;
    };

    /// One open beginScroll() region.
    struct ScrollFrame {
        u32 id = 0;
        Rect viewport{};      //! Visible area, in window pixels.
        Rect savedClip{};     //! Clip to restore at endScroll().
        float contentTop = 0.0f; //! cursorY at entry, for measuring content.
        float savedIndent = 0.0f;
    };

    CurrentPanel _panel{};

    //! Open scroll regions, innermost last. A vector rather than one member so
    //! regions can nest, which a list inside a settings pane naturally does.
    std::vector<ScrollFrame> _scrollStack;

    /// State of the tab bar between beginTabBar() and endTabBar(), grouped for
    /// the same reason CurrentPanel is: it is one construct's worth of state,
    /// live only between its two calls.
    struct TabBar {
        u32 id = 0;
        u32 index = 0;    //! Which tab within the bar is next.
        bool open = false;
        //! Left edge of the next tab. Tabs are width-fitted to their labels and
        //! packed left to right, so the row is walked rather than divided.
        float cursorX = 0.0f;
    };

    TabBar _tabBar{};

    //! Geometry that must draw over everything else: an open dropdown list,
    //! a tooltip. Emitted mid-panel but must appear on top of later widgets;
    //! since the batch has no depth test, deferring to render() is what puts
    //! them last without a second batch.
    struct DeferredQuad {
        Rect bounds{};
        glm::vec2 uvMin{0.0f};
        glm::vec2 uvMax{0.0f};
        glm::vec4 color{1.0f};
        //! Clip in force when the quad was recorded, replayed with it so a
        //! deferred quad is trimmed exactly as it would have been in place.
        //! Carried here rather than in a second vector indexed in lockstep --
        //! nothing then has to keep two containers the same length.
        Rect clip{};
    };

    std::vector<DeferredQuad> _overlayQuads;
    bool _deferring = false;

    /// Replays _overlayQuads into the batch; called at the end of render().
    void _flushOverlays();

    //! Content clip for the current panel. Bottom edge left effectively
    //! unbounded: a panel's height isn't known until endPanel(), and never
    //! needs to be since auto-height panels grow to fit their content.
    Rect _clip{};

    //! Reused caption buffer (e.g. "Exposure: 1.250"). Cleared rather than
    //! rebuilt each frame, so the steady state never reallocates.
    std::string _caption;
};

} // namespace aura3d::ui

#endif // AURA_UI_AURAUI_H
