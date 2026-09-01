#ifndef AURA_UI_AURAUI_H
#define AURA_UI_AURAUI_H

#pragma once

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <wma/wma.hpp>

#include "aura/UI/Style.h"

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

namespace aura3d {
class IRenderer;
} // namespace aura3d

namespace aura3d::ui {

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
     * @brief The per-component theme, and the metrics every component shares.
     *
     * A plain value, edited in place -- there is nothing to notify, since each
     * widget reads its Part as it is submitted, so an edit lands on the very
     * next one, including mid-panel.
     *
     * @code
     * gui.theme = ui::Theme::light();               // wholesale
     * gui.theme.applyPalette(brand);                // from nine colours
     * gui.theme[ui::Part::Button].rounding = 8.0f;  // one component
     * gui.theme.metrics.rowHeight = 26.0f;          // shared layout
     * @endcode
     *
     * Declared before @c _impl on purpose: the Impl is handed a reference to
     * it, so it has to be alive first.
     */
    Theme theme{};

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
    void label(std::string_view text, const Style& style = {});

    /**
     * @brief A full-width push button.
     * @return true on the frame the press is released over the button.
     */
    bool button(std::string_view text, const Style& style = {});

    /**
     * @brief A labelled check box bound to @p value.
     * @param[in,out] value Toggled in place when the box is clicked.
     * @return true on the frame @p value changed.
     */
    bool checkbox(std::string_view text, bool& value, const Style& style = {});

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
    bool sliderFloat(std::string_view text, float& value, float min, float max,
                     const Style& style = {});

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
    bool inputText(std::string_view label, std::string& value, size_t maxBytes = 1024,
                   const Style& style = {});

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
    bool inputFloat(std::string_view label, float& value, const Style& style = {});

    /**
     * @brief A drop-down list of @p items bound to @p index.
     *
     * The open list is drawn over whatever follows it, and closes on selection
     * or on a click elsewhere.
     *
     * @param[in,out] index Selected element; clamped into @p items' range.
     * @return true on the frame the selection changed.
     */
    bool dropdown(std::string_view label, int& index, std::span<const std::string_view> items,
                  const Style& style = {});

    /**
     * @brief One button of a radio group bound to @p value.
     * @param[in,out] value Set to @p buttonValue when this button is clicked.
     * @return true on the frame this button took the selection.
     */
    bool radioButton(std::string_view label, int& value, int buttonValue,
                     const Style& style = {});

    /// A clickable header that shows or hides the widgets below it. Open
    /// state is retained by the context; emit contents inside
    /// `if (collapsingHeader(...))`.
    /// @param defaultOpen Applied the first time this header is seen.
    /// @return true when the section is open and its contents should be emitted.
    [[nodiscard]] bool collapsingHeader(std::string_view label, bool defaultOpen = true,
                                        const Style& style = {});

    /// An indented, collapsible node for hierarchical data. Pair with
    /// treePop() only when this returns true, like beginPanel()/endPanel().
    /// Nesting is unlimited; each level indents by the style's padding.
    /// @return true when the node is expanded and its children should be emitted.
    [[nodiscard]] bool treeNode(std::string_view label, bool defaultOpen = false,
                                const Style& style = {});

    /// Closes the node opened by the matching treeNode().
    void treePop() noexcept;

    /**
     * @brief A selectable row, for lists.
     * @param[in] selected Whether to draw this row as the current selection.
     * @return true on the frame the row is clicked.
     */
    bool selectable(std::string_view label, bool selected, const Style& style = {});

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
    [[nodiscard]] bool beginScroll(std::string_view id, float height, const Style& style = {});

    /// Closes the region opened by the matching beginScroll().
    void endScroll();

    /// Begins a row of tabs; pair with endTabBar() when it returns true.
    /// Selected tab is retained by the context; emit each tab's contents
    /// inside `if (tabItem("..."))`.
    [[nodiscard]] bool beginTabBar(std::string_view id);

    /// @return true when @p label is the selected tab in the current bar.
    [[nodiscard]] bool tabItem(std::string_view label, const Style& style = {});

    /// Closes the bar opened by the matching beginTabBar().
    void endTabBar() noexcept;

    /// Shows @p text in a floating box beside the cursor. Call immediately
    /// after the widget it describes; draws only while that widget is hovered.
    void tooltip(std::string_view text, const Style& style = {});

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
    void separator(const Style& style = {});

    /// Advances the layout cursor by @p pixels without drawing anything.
    void spacing(float pixels) noexcept;

    /// Gives keyboard focus to the next focusable widget submitted. For
    /// opening a panel with a field already active.
    void setKeyboardFocusHere() noexcept;

    /// True when the UI is consuming the keyboard and the application should
    /// not (gate movement keys on this). True whenever any widget holds
    /// keyboard focus. Reports the most recently completed frame.
    [[nodiscard]] bool isCapturingKeyboard() const noexcept;

    /// True when the UI needs the platform's text input active. Drive
    /// IWindowManager::setTextInputEnabled() from this if not using
    /// attachInput(), which does it automatically.
    [[nodiscard]] bool isCapturingTextInput() const noexcept;

    /// True when the UI is using the mouse and the application should not
    /// (covers hovering any panel and holding a widget whose drag has since
    /// left it). Gate camera look and world picking on this. Reports the
    /// most recently completed frame.
    [[nodiscard]] bool isCapturingMouse() const noexcept;

    /**
     * @brief Draws the widgets until endDisabled() greyed out and inert.
     *
     * They still lay out, still draw and still return their value -- they
     * simply take no input and enter no tab stop, so a panel doesn't reflow
     * as options become available. Nests; @p disabled false pushes a level
     * that changes nothing, so a condition needs no matching @c if.
     */
    void beginDisabled(bool disabled = true);

    /// Closes the level opened by the matching beginDisabled().
    void endDisabled() noexcept;

    /// True inside a beginDisabled() that is actually disabling.
    [[nodiscard]] bool isDisabled() const noexcept;

    /// Pixel size one line of @p text occupies at the current text scale.
    /// Rasterizes any glyph not yet cached, so not @c const. Line breaks are
    /// not interpreted.
    [[nodiscard]] glm::vec2 measureText(std::string_view text);

private:
    //! Every piece of state the UI keeps -- the geometry buffers, the glyph
    //! atlas, the pointer/focus/capture machinery, the per-widget retained
    //! maps and the layout cursors -- lives in Impl, defined in AuraUI.cpp.
    //!
    //! None of it is anyone else's business, and keeping it out of this header
    //! means a translation unit that draws a checkbox parses the API and
    //! nothing else: no FontAtlas, no IRenderer, no <unordered_map>. Changing
    //! how a widget remembers something recompiles one file.
    class Impl;
    std::unique_ptr<Impl> _impl;
};

/**
 * @class StyleGuard
 * @brief Restyles one @ref Part for a scope, and puts it back afterwards.
 *
 * The retained-mode escape hatch immediate mode is otherwise missing: a
 * section of UI that looks different, without every widget in it repeating
 * the override.
 *
 * Takes the same @ref Style patch a widget does, applied over whatever the
 * Part currently looks like -- so a scope that only wants a different colour
 * says only that.
 *
 * @code
 * {
 *     ui::StyleGuard scope(gui, ui::Part::Button,
 *                          ui::Style{}.fill({0.66f, 0.20f, 0.22f, 1.0f}));
 *
 *     if (gui.button("Delete"))     deleteThing();
 *     if (gui.button("Delete all")) deleteEverything();
 * }
 * @endcode
 *
 * For a single widget, pass the patch to it directly; this is for a run of
 * them.
 *
 * @note Not copyable or movable -- it is a scope, and one scope cannot be two
 *       places. Declare it; don't return it.
 */
class StyleGuard {
public:
    StyleGuard(Context& ui, Part part, const Style& style);
    ~StyleGuard();

    StyleGuard(const StyleGuard&) = delete;
    StyleGuard& operator=(const StyleGuard&) = delete;
    StyleGuard(StyleGuard&&) = delete;
    StyleGuard& operator=(StyleGuard&&) = delete;

private:
    Context& _ui;
    Part _part;
    WidgetStyle _saved;
};

} // namespace aura3d::ui

#endif // AURA_UI_AURAUI_H
