#ifndef AURA_RENDERER_CANVAS_H
#define AURA_RENDERER_CANVAS_H

#pragma once

#include <span>
#include <vector>

#include "aura/Core/AuraCore.h"

namespace aura3d
{
namespace gfx
{

/// What Canvas::line() needs to place a world-space segment in screen pixels. From IRenderer::canvasView().
struct CanvasView
{
    glm::mat4 clip{1.0f};       ///< World to clip space: projection * view * model.
    glm::vec2 target{0.0f};     ///< Render-target size in pixels.
    bool depthZeroToOne = true; ///< Clip depth is [0, 1] (Vulkan, Metal) rather than [-1, 1].
};

/// Unlit triangles that IRenderer::drawBatch() draws in one call. Geometry stays until
/// clear() and capacity beyond it, so a canvas is rebuilt each frame without allocating,
/// or built once and drawn every frame. Invalid input (nonfinite, degenerate, invisible)
/// adds nothing.
class Canvas
{
  public:
    /// Flat-ended quad @p width wide around the segment; pixels in a Screen batch.
    void line(glm::vec2 from, glm::vec2 to, const glm::vec4 &color, f32 width = 1.0f);

    /// A world segment for a Screen batch, @p width pixels wide at any depth and depth-tested
    /// at its own depth. The part behind the camera is dropped.
    void line(const CanvasView &view, glm::vec3 from, glm::vec3 to, const glm::vec4 &color, f32 width = 1.0f);

    void rect(glm::vec2 origin, glm::vec2 size, const glm::vec4 &color);
    void triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, const glm::vec4 &color);
    /// Screen pixels with depth in z, or world positions, as the canvas is drawn.
    void triangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, const glm::vec4 &color);

    /// Raw geometry; @p indices count from the first of @p vertices.
    void append(std::span<const BatchVertex> vertices, std::span<const u32> indices);

    void clear() noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::span<const BatchVertex> vertices() const noexcept;
    [[nodiscard]] std::span<const u32> indices() const noexcept;

  private:
    //! Flat-ended quad @p width wide around [@p from, @p to] in x and y; each end keeps its z.
    void segment(glm::vec3 from, glm::vec3 to, const glm::vec4 &color, f32 width);
    void quad(const BatchVertex &a, const BatchVertex &b, const BatchVertex &c, const BatchVertex &d);

    std::vector<BatchVertex> _vertices;
    std::vector<u32> _indices;
};

} // namespace gfx
} // namespace aura3d

#endif // AURA_RENDERER_CANVAS_H
