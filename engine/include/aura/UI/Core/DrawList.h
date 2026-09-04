#ifndef AURA_UI_DRAWLIST_H
#define AURA_UI_DRAWLIST_H

#pragma once

#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <ink/ink_base.hpp>

#include "aura/Renderer/RenderHandles.h"
#include "aura/UI/Core/Geometry.h"

/**
 * @file DrawList.h
 * @brief What a widget produces instead of draw calls.
 *
 * A widget says "a rounded rectangle here, this text there" and stops. It
 * never sees a vertex, a texture binding or a render pass, which is the
 * property that keeps the widget layer identical on Vulkan, OpenGL, Metal and
 * the software rasterizer -- and testable with no GPU at all, since a DrawList
 * is just data.
 *
 * @code
 * void MyWidget::paint(DrawList& out)
 * {
 *     out.fillRect(bounds(), theme().palette().surface, Corners::all(6.0f));
 *     out.drawText(_shaped, bounds().min + glm::vec2{8.0f, 4.0f}, kTextColor);
 * }
 * @endcode
 *
 * @ref DrawListRenderer turns one into IRenderer draw calls; nothing else in
 * AuraUI knows how.
 */

namespace aura3d::ui {

class ShapedText;

enum class DrawCommandType : u8 { Rect, Text, Image, Mask };

/**
 * @brief One primitive, with everything needed to draw it.
 *
 * A flat record rather than a class hierarchy: the list is walked once per
 * frame in order, so there is nothing for virtual dispatch to buy, and a POD
 * command is what makes a golden-image test a plain comparison.
 */
struct DrawCommand {
    DrawCommandType type = DrawCommandType::Rect;

    /// The rectangle the primitive occupies. For Text, the block's box; the
    /// glyph quads inside it are already positioned relative to @c origin.
    Rect bounds{};

    /**
     * @brief Region the primitive is visible in, already intersected down the
     *        clip stack.
     *
     * Carried per command rather than as a state change so a backend can draw
     * the list in any order and needs no clip machinery of its own. Bounds are
     * left untrimmed because trimming a rounded rectangle would distort its
     * corners -- the backend clips per quad.
     */
    Rect clip{};

    glm::vec4 color{1.0f}; //! Fill, text colour, or image tint.

    glm::vec4 borderColor{0.0f};
    f32 borderWidth = 0.0f;

    Corners radius{};

    //! Text only. Borrowed from the widget, which keeps its shaped text
    //! between frames; valid until the list is cleared.
    const ShapedText* text = nullptr;
    glm::vec2 origin{0.0f};

    TextureHandle texture{}; //! Image only.

    /// @{
    /// Mask only: the atlas cell to sample, and which glyph page it is on.
    glm::vec2 uvMin{0.0f};
    glm::vec2 uvMax{0.0f};
    u32 page = 0;
    /// @}
};

/**
 * @class DrawList
 * @brief The recording surface a widget paints into, and the clip stack.
 *
 * Storage is retained across frames, so a steady-state UI re-records without
 * allocating.
 */
class DrawList {
public:
    /// Opens a frame: drops the previous list and makes @p surface the
    /// outermost clip.
    void begin(const Rect& surface);

    void fillRect(const Rect& bounds, const glm::vec4& color, Corners radius = {});

    /// An outline drawn *inside* @p bounds, so a bordered control occupies
    /// exactly the rectangle it was arranged into.
    void strokeRect(const Rect& bounds, const glm::vec4& color, f32 width, Corners radius = {});

    /// Fill and outline as one primitive -- the usual case, and one command
    /// rather than two.
    void drawRect(const Rect& bounds, const glm::vec4& fill, const glm::vec4& border,
                  f32 borderWidth, Corners radius = {});

    /**
     * @brief Draws already-shaped text with its block origin at @p origin.
     *
     * @param text Must outlive the frame; widgets keep theirs cached, which is
     *        also what stops a label re-shaping every frame.
     */
    void drawText(const ShapedText& text, glm::vec2 origin, const glm::vec4& color);

    void drawImage(const Rect& bounds, TextureHandle texture,
                   const glm::vec4& tint = glm::vec4{1.0f}, Corners radius = {});

    /**
     * @brief Draws one coverage cell from the glyph atlas, tinted.
     *
     * The icon path. FontAtlas rasterizes a shape once into a cell
     * (FontAtlas::convexMask()), and every draw of it after that is this: a
     * single quad in the same batch as the text and surfaces around it. A
     * chevron, a disclosure arrow and a tick are all this call with different
     * UVs.
     *
     * @param page Glyph page the UVs address; see ITextShaper::page().
     */
    void drawMask(const Rect& bounds, u32 page, glm::vec2 uvMin, glm::vec2 uvMax,
                  const glm::vec4& color);

    /// @{
    /// Intersects @p bounds with the current clip and pushes it. Every push
    /// needs a pop; @ref ClipScope pairs them for you.
    void pushClip(const Rect& bounds);
    void popClip() noexcept;
    [[nodiscard]] const Rect& clip() const noexcept { return _clips.back(); }
    /// @}

    [[nodiscard]] std::span<const DrawCommand> commands() const noexcept { return _commands; }
    [[nodiscard]] usize size() const noexcept { return _commands.size(); }
    [[nodiscard]] bool empty() const noexcept { return _commands.empty(); }

private:
    /// False when @p bounds cannot contribute a pixel, which is how a
    /// scrolled-away subtree costs nothing but its layout.
    [[nodiscard]] bool _visible(const Rect& bounds) const noexcept;

    std::vector<DrawCommand> _commands;

    //! Never empty: begin() seeds it with the surface, so clip() needs no
    //! null check on the path of every primitive.
    std::vector<Rect> _clips{Rect{}};
};

/**
 * @class ClipScope
 * @brief Pushes a clip for a scope and pops it however the scope exits.
 *
 * @code
 * {
 *     ClipScope clipped(out, viewport());
 *     for (Widget* child : children()) child->paint(out);
 * }
 * @endcode
 */
class ClipScope {
public:
    ClipScope(DrawList& list, const Rect& bounds) : _list(list) { _list.pushClip(bounds); }
    ~ClipScope() { _list.popClip(); }

    ClipScope(const ClipScope&) = delete;
    ClipScope& operator=(const ClipScope&) = delete;

private:
    DrawList& _list;
};

} // namespace aura3d::ui

#endif // AURA_UI_DRAWLIST_H
