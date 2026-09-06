#ifndef AURA_UI_ICON_H
#define AURA_UI_ICON_H

#pragma once

#include <span>

#include <glm/glm.hpp>
#include <ink/ink_base.hpp>

#include "aura/UI/Core/Geometry.h"

/**
 * @file Icon.h
 * @brief Shapes drawn from the glyph atlas' coverage cells.
 *
 * A UI needs a handful of small shapes -- a chevron, a disclosure arrow, a
 * tick -- and each one drawn as geometry would be its own primitive, its own
 * backend case, and its own aliased diagonal. Instead FontAtlas rasterizes the
 * shape's *exact* coverage once (FontAtlas::convexMask()) and every draw after
 * that is one textured quad, antialiased, tinted by the vertex colour, and in
 * the same batch as the text beside it.
 *
 * @code
 * icon::triangle(out, *shaper(), arrowRect, icon::Direction::Down, style.text);
 * @endcode
 */

namespace aura3d::ui {

class DrawList;
class ITextShaper;

namespace icon {

enum class Direction : u8 { Right, Down, Left, Up };

/**
 * @brief Draws a solid triangle pointing @p direction, inscribed in @p bounds.
 *
 * The disclosure arrow and the drop-down chevron. Rasterized on first use at
 * the size asked for and cached in the atlas thereafter, so the shape costs
 * its rasterization once per size and direction.
 */
void triangle(DrawList& out, ITextShaper& shaper, const Rect& bounds, Direction direction,
              const glm::vec4& color);

/**
 * @brief Draws an arbitrary convex polygon, given in @c [0,1] cell space.
 *
 * What @ref triangle is built on, and the escape hatch for a shape the toolkit
 * does not name. @p id must be unique per shape; ids below @ref kFirstUserId
 * are reserved.
 */
void convex(DrawList& out, ITextShaper& shaper, const Rect& bounds,
            std::span<const glm::vec2> unitPolygon, u32 id, const glm::vec4& color);

/// Shape ids at or above this are the caller's to allocate.
inline constexpr u32 kFirstUserId = 1024;

} // namespace icon

} // namespace aura3d::ui

#endif // AURA_UI_ICON_H
