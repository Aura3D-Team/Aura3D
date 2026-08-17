#ifndef AURA_UI_AURAUI_H
#define AURA_UI_AURAUI_H

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include <glm/glm.hpp>
#include <wma/wma.hpp>

#include "aura/Core/AuraFont/FontAtlas.h"
#include "aura/Renderer/IRenderer.h"
#include "aura/Utils/AlignedVector.h"

/**
 * @file AuraUI.h
 * @brief Aura3D's built-in immediate-mode user interface.
 *
 * @par Why this exists rather than a vendored toolkit
 * The engine already owns the two pieces a debug/tool UI is made of: a glyph
 * cache (@ref aura3d::FontAtlas) and a backend-agnostic 2D batch submission
 * primitive (IRenderer::drawBatch2D). This module is the thin layer between
 * them -- layout, hit-testing and a widget vocabulary -- and nothing else. It
 * pulls in no third-party dependency and adds no per-backend code: because it
 * speaks only the public IRenderer API, it renders identically on Vulkan,
 * OpenGL, Metal and the software rasteriser, on desktop, WASM and Android.
 *
 * @par Immediate mode
 * There is no retained widget tree, no callbacks and no ownership to manage.
 * Widgets are function calls that both draw and report what the user did, so
 * the UI is rebuilt from scratch every frame and can never disagree with the
 * state it displays:
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

/**
 * @brief Colours and metrics shared by every widget.
 *
 * Defaults are a neutral dark theme. Mutate through Context::style() at any
 * time; changes take effect on the next widget drawn, since nothing is
 * retained between frames.
 */
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

/**
 * @brief Per-frame input the UI reacts to.
 *
 * Fill this yourself and hand it to Context::newFrame(const Input&), or let
 * Context::attachInput() keep an internal copy fed straight from wma.
 */
struct Input {
    glm::vec2 mouse{0.0f}; //! Cursor position in window pixels.
    bool mouseDown = false; //! Primary (left) button held this frame.
};

/**
 * @brief Construction parameters for a @ref Context.
 */
struct ContextDesc {
    /**
     * Path to a .ttf/.otf file. Left empty -- or naming a file that cannot be
     * read -- falls back to the engine's embedded bitmap font, which needs no
     * asset on disk and therefore always works.
     */
    std::string fontPath;

    float pixelHeight = 16.0f; //! Glyph rasterization size, in pixels.
    u32 atlasSize = 1024;      //! Edge length of the (square) glyph atlas.
};

/**
 * @class Context
 * @brief The immediate-mode UI itself: state between frames, and the widgets.
 *
 * @par Frame shape
 * newFrame() latches input and resets layout, widget calls accumulate geometry,
 * and render() submits it. render() must be called inside the renderer's pass
 * (between beginRenderPass() and endRenderPass()) and after the scene's own
 * draws, since the UI composites over them with straight alpha and no depth
 * test.
 *
 * @par Cost
 * The entire UI leaves as **one** IRenderer::drawBatch2D() call, regardless of
 * how many panels and widgets it contains: solid quads sample the opaque cell
 * FontAtlas::solidTexelUv() reserves, so rectangles and glyphs share one
 * texture and never force a batch break. The vertex and index buffers are
 * retained across frames, so a steady-state UI performs no allocation at all.
 *
 * @note Not thread-safe, and not re-entrant: one Context drives one UI on one
 *       thread, like the renderer it draws through.
 */
class Context {
public:
    /**
     * @brief Builds the UI, its glyph atlas and its atlas texture.
     *
     * Never throws. A font that cannot be loaded degrades to the embedded
     * bitmap font; a renderer that cannot allocate the atlas texture leaves the
     * context inert (every widget still returns sensible values, nothing draws)
     * rather than failing construction.
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
     * Binds the primary button and tracks cursor motion on the mouse listener's
     * **currently active input context**, so push a dedicated context first if
     * the application already binds the left button and the two must not share.
     * Cursor position is polled rather than bound, so it stays correct even
     * while another subsystem owns the move callback.
     *
     * @param windowManager Window manager to read input from; must outlive this
     *        object.
     */
    void attachInput(wma::IWindowManager& windowManager);

    /**
     * @brief Opens a frame: latches input edges, clears last frame's geometry.
     *
     * Uses the state maintained by attachInput(). Equivalent to the overload
     * below when no input source is attached, in which case the UI simply sees
     * no cursor and no clicks.
     */
    void newFrame() noexcept;

    /// As above, driven by input the caller supplies rather than by wma.
    void newFrame(const Input& input) noexcept;

    /**
     * @brief Submits the frame's geometry as a single 2D batch.
     *
     * Call between IRenderer::beginRenderPass() and endRenderPass(), after the
     * scene. Does nothing when no widget produced geometry.
     */
    void render();

    /**
     * @brief Begins a draggable, auto-height panel.
     *
     * The panel remembers its position across frames, keyed by @p title, and
     * moves when its title bar is dragged; @p defaultPosition therefore only
     * places it the first time it is seen. Height follows the content, so
     * nothing needs to be sized by hand.
     *
     * Must be paired with endPanel() only when it returns true.
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
     * The label and the current value are drawn on the track. Dragging is
     * tracked until the button is released, even if the cursor leaves the
     * track, which is what stops a fast drag from snapping back.
     *
     * @param[in,out] value Clamped into [@p min, @p max] and updated in place.
     * @param min Lower bound of the track.
     * @param max Upper bound of the track; must be greater than @p min.
     * @return true on any frame @p value changed.
     */
    bool sliderFloat(std::string_view text, float& value, float min, float max);

    /// A horizontal rule spanning the content width.
    void separator();

    /// Advances the layout cursor by @p pixels without drawing anything.
    void spacing(float pixels) noexcept;

    /**
     * @brief True when the UI is using the mouse and the application should not.
     *
     * Covers both hovering any panel and holding a widget whose drag has since
     * left it. Gate camera look and world picking on this so a click meant for
     * a slider does not also spin the scene.
     *
     * Reports the most recently completed frame, so query it anywhere in the
     * game loop and get a stable answer.
     */
    [[nodiscard]] bool wantsMouse() const noexcept { return _wantsMouse; }

    /// Mutable theme; see @ref Style.
    [[nodiscard]] Style& style() noexcept { return _style; }
    [[nodiscard]] const Style& style() const noexcept { return _style; }

    /**
     * @brief Pixel size one line of @p text occupies at the current text scale.
     *
     * Rasterizes any glyph not yet cached, so it is not @c const. Line breaks
     * are not interpreted: widgets are single-line, and so is this.
     */
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

    /// Reserves the next full-width row inside the current panel.
    [[nodiscard]] Rect _nextRow(float height) noexcept;

    /**
     * @brief Derives the identity a widget keeps across frames.
     *
     * Immediate mode draws no widget objects to point at, so a widget is
     * recognised from one frame to the next by this value alone. It mixes the
     * label, the enclosing panel and a per-panel counter, so neither two panels
     * sharing a label nor two same-labelled widgets in one panel collide.
     */
    [[nodiscard]] u32 _idFor(std::string_view text) noexcept;

    /// Baseline-to-baseline distance at the current text scale, in pixels.
    [[nodiscard]] float _lineHeight() const noexcept;

    /// True when the cursor is inside @p bounds and inside the active clip.
    [[nodiscard]] bool _hovering(const Rect& bounds) const noexcept;

    /// Appends a solid quad, clipped to the active clip rectangle.
    void _quad(const Rect& bounds, const glm::vec4& color);

    /// Appends @p text's glyphs with their top-left corner at @p origin.
    void _text(std::string_view text, glm::vec2 origin, const glm::vec4& color);

    /// Appends @p text centred in @p bounds, both axes.
    void _textCentered(std::string_view text, const Rect& bounds, const glm::vec4& color);

    /**
     * @brief Appends one textured quad clipped to @p _clip.
     *
     * Clipping happens here, on the CPU, rather than through a scissor
     * rectangle: drawBatch2D() submits one batch with no per-command state, so
     * a scissor would mean one draw call per clip change. Quads are
     * axis-aligned, so trimming their corners and interpolating the UVs is
     * exact -- the pixels are identical to what a scissor would have produced.
     */
    void _texturedQuad(Rect bounds, glm::vec2 uvMin, glm::vec2 uvMax, const glm::vec4& color);

    /// Pushes the atlas' pending dirty rectangle to the GPU, if any.
    void _uploadAtlasChanges();

    IRenderer* _renderer;
    std::unique_ptr<FontAtlas> _atlas;
    TextureHandle _atlasTexture;
    glm::vec2 _solidUv{0.0f}; //! Opaque atlas texel every solid quad samples.
    bool _usable = false;     //! False when the atlas or its texture is missing.

    Style _style{};

    /*
     * Retained across frames, and over-aligned for the same reason
     * TextOverlay's are: this batch is memcpy'd straight into write-combined
     * mapped device memory (Vulkan) or handed to glBufferSubData (OpenGL) every
     * frame, and both copy fastest from a source aligned to the widest vector
     * register. gfx::Vertex2D is 32 bytes, so a 32-byte-aligned base aligns
     * every vertex in the batch rather than merely the array's first.
     */
    static_assert(sizeof(gfx::Vertex2D) == 32,
                  "Vertex2D must stay 32 bytes for the aligned batch storage "
                  "below to align every vertex, not merely the array's base.");

    AlignedVector<gfx::Vertex2D> _vertices;
    AlignedVector<u32> _indices;

    Input _input{};             //! Latched at newFrame(), constant for the frame.
    Input _pendingInput{};      //! Written by attachInput()'s callbacks.
    //! Non-owning; set by attachInput() and polled for the cursor position.
    wma::MouseListener* _mouse = nullptr;
    bool _previousMouseDown = false;
    bool _mousePressed = false; //! Button went down this frame.

    /*
     * Hot is resolved one frame late, and deliberately: the last widget to
     * claim the cursor wins, which is the topmost one, since panels are drawn
     * back to front. Resolving it within the frame would instead hand the
     * cursor to whichever widget happened to be submitted first, so a panel
     * would respond to clicks landing on the panel covering it.
     */
    u32 _hot = 0;      //! Widget under the cursor, from last frame's claims.
    u32 _nextHot = 0;  //! Claims made this frame; becomes _hot at newFrame().
    u32 _active = 0;   //! Widget owning the ongoing press, if any.

    /*
     * Whether the active widget was submitted at all last frame. A widget that
     * vanishes mid-press (its panel closed, a branch stopped emitting it) never
     * gets to observe the release that would clear _active, so without this the
     * UI would stay stuck believing a press is in flight forever.
     */
    bool _activeSubmitted = false;

    //! Cursor-to-corner offset captured when a panel drag begins, so the panel
    //! follows the grab point rather than snapping its corner to the cursor.
    glm::vec2 _dragOffset{0.0f};

    bool _wantsMouse = false;
    bool _wantsMouseThisFrame = false;

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
    };

    CurrentPanel _panel{};
    bool _inPanel = false;

    /*
     * Content clip for the current panel, in window pixels. The bottom edge is
     * left effectively unbounded because a panel's height is not known until
     * endPanel() -- and it never needs to be, since auto-height panels grow to
     * fit their content by construction.
     */
    Rect _clip{};

    //! Reused caption buffer (e.g. "Exposure: 1.250"). Cleared rather than
    //! rebuilt each frame, so the steady state never reallocates.
    std::string _caption;
};

} // namespace aura3d::ui

#endif // AURA_UI_AURAUI_H
