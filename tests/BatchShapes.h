#pragma once

//! Geometry for IRenderer::drawBatch() in the tests: the lines, rectangles and triangles the batch, golden and
//! conformance tests draw on every backend.
#include <cmath>
#include <span>
#include <vector>

#include "aura/Renderer/IRenderer.h"

namespace aura3d::test
{

struct BatchShapes
{
    std::vector<gfx::BatchVertex> vertices;
    std::vector<u32> indices;

    void clear()
    {
        vertices.clear();
        indices.clear();
    }

    void draw(IRenderer &renderer, TextureHandle texture = {}, gfx::BatchSpace space = gfx::BatchSpace::Screen) const
    {
        if (!indices.empty())
            renderer.drawBatch(vertices, indices, texture, space);
    }

    void triangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, const glm::vec4 &color)
    {
        const auto base = static_cast<u32>(vertices.size());
        vertices.insert(vertices.end(), {{a, {}, color}, {b, {}, color}, {c, {}, color}});
        indices.insert(indices.end(), {base, base + 1, base + 2});
    }

    void triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, const glm::vec4 &color)
    {
        triangle(glm::vec3{a, 0.0f}, glm::vec3{b, 0.0f}, glm::vec3{c, 0.0f}, color);
    }

    void rect(glm::vec2 origin, glm::vec2 size, const glm::vec4 &color)
    {
        const glm::vec2 end = origin + size;
        quad({origin, 0.0f}, {end.x, origin.y, 0.0f}, {end, 0.0f}, {origin.x, end.y, 0.0f}, color);
    }

    //! Flat-ended quad @p width pixels wide around the segment.
    void line(glm::vec2 from, glm::vec2 to, const glm::vec4 &color, f32 width = 1.0f)
    {
        segment({from, 0.0f}, {to, 0.0f}, color, width);
    }

    //! A world segment projected by @p clip into a @p target-pixel Screen batch, at its own depth; the part behind
    //! the camera is dropped. @p zeroToOne: clip depth is [0, 1] (Vulkan, Metal) rather than [-1, 1].
    void line(const glm::mat4 &clip, glm::vec2 target, bool zeroToOne, glm::vec3 from, glm::vec3 to,
              const glm::vec4 &color, f32 width = 1.0f)
    {
        glm::vec4 a = clip * glm::vec4(from, 1.0f);
        glm::vec4 b = clip * glm::vec4(to, 1.0f);
        constexpr f32 kMinW = 1e-6f;
        if (!clipSegment(a, b, zeroToOne ? a.z : a.z + a.w, zeroToOne ? b.z : b.z + b.w) ||
            !clipSegment(a, b, a.w - kMinW, b.w - kMinW))
            return;
        const auto toScreen = [&](const glm::vec4 &p)
        {
            const glm::vec3 ndc = glm::vec3(p) / p.w;
            return glm::vec3{(ndc.x + 1.0f) * 0.5f * target.x, (1.0f - ndc.y) * 0.5f * target.y,
                             zeroToOne ? ndc.z : (ndc.z + 1.0f) * 0.5f};
        };
        segment(toScreen(a), toScreen(b), color, width);
    }

  private:
    static bool clipSegment(glm::vec4 &a, glm::vec4 &b, f32 da, f32 db)
    {
        if (da < 0.0f && db < 0.0f)
            return false;
        if (da < 0.0f)
            a = glm::mix(a, b, da / (da - db));
        else if (db < 0.0f)
            b = glm::mix(b, a, db / (db - da));
        return true;
    }

    void segment(glm::vec3 from, glm::vec3 to, const glm::vec4 &color, f32 width)
    {
        const glm::vec2 d{to.x - from.x, to.y - from.y};
        const glm::vec3 normal{glm::vec2{-d.y, d.x} * (width * 0.5f / std::sqrt(d.x * d.x + d.y * d.y)), 0.0f};
        quad(from - normal, to - normal, to + normal, from + normal, color);
    }

    void quad(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, const glm::vec4 &color)
    {
        const auto base = static_cast<u32>(vertices.size());
        vertices.insert(vertices.end(), {{a, {}, color}, {b, {}, color}, {c, {}, color}, {d, {}, color}});
        indices.insert(indices.end(), {base, base + 1, base + 2, base + 2, base + 3, base});
    }
};

} // namespace aura3d::test
