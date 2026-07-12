#ifndef CPUFRAMEBUFFERMANAGER_H
#define CPUFRAMEBUFFERMANAGER_H

#include <SDL3/SDL.h>
#include <cmath>
#include <string>
#include <glm/glm.hpp>

static const float PI_FLOAT = std::acos(-1.0f);

#include "aura/Core/AuraCore.h"
#include "aura/Utils/AlignedVector.h"
#include "aura/Core/AuraFont/AuraBitmapFont.h"

namespace aura3d {
namespace cpu {

struct Point {
    Point(i32 _x, i32 _y) :
        x(_x), y(_y) {};

    i32 x;
    i32 y;
};

struct Pixel {
    constexpr Pixel(u32 rgb_ = 0, f32 z_ = 1.0f) noexcept
        : rgb(rgb_), z(z_) {}

    u32 rgb = 0;
    f32 z = 1.0f;
};

struct Rectangle {
    constexpr Rectangle(
        i32 x_ = 0,
        i32 y_ = 0,
        i32 width_ = 0,
        i32 height_ = 0
        ) noexcept
        : x(x_), y(y_), width(width_), height(height_) {}

    i32 x = 0;
    i32 y = 0;
    i32 width = 0;
    i32 height = 0;

    [[nodiscard]]
    constexpr bool isValid() const noexcept {
        return width > 0 && height > 0 && x >= 0 && y >= 0;
    }

    [[nodiscard]]
    constexpr bool contains(i32 px, i32 py) const noexcept {
        return px >= x &&
               py >= y &&
               px < x + width &&
               py < y + height;
    }
};

enum InterpolationMethod: u32 {
    Linear = 0,
    Smoothstep = 1,
    Accelerate = 2,
    Decelerate = 3,
    Sine = 4,
    Cosine = 5
};

struct Texture {
    Texture(int w, int h) : data(w * h, 0), width(w), height(h) {}

    AlignedVector<u32> data;
    i32 width;
    i32 height;

    u32 sample(f32 u, f32 v) const noexcept 
    {
        int x = INK_CLAMP(static_cast<int>(u * static_cast<f32>(width)),  0, width  - 1);
        int y = INK_CLAMP(static_cast<int>(v * static_cast<f32>(height)), 0, height - 1);
        return data[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
    }
};

/**
 * @brief Screen-space vertex produced by the vertex-transform stage.
 *
 * All floating-point positions are in pixel coordinates.
 * Attributes (uv, color) are stored in their original form; perspective-
 * correct interpolation is applied inside drawTriangle().
 */
struct ScreenVertex {
    f32        x    = 0.0f;   ///< Pixel X (left = 0)
    f32        y    = 0.0f;   ///< Pixel Y (top  = 0)
    f32        z    = 1.0f;   ///< Depth in [0, 1]  (0 = near, 1 = far)
    f32        invW = 1.0f;   ///< 1 / clip_w  (for perspective-correct interp)
    glm::vec2  uv   = {};     ///< Texture coordinates
    glm::vec4  color= {1,1,1,1}; ///< Vertex color  [0, 1] per channel
};

class CpuFrameBufferManager
{
public:
    struct Config {
        i32 width = 1280;
        i32 height = 720;
        bool useDepthBuffer = true;
    };

    CpuFrameBufferManager(SDL_Window* window, Config config);
    ~CpuFrameBufferManager();

    // Core rendering
    void clear(u32 color = 0);
    void renderFramebuffer();

    // Memory management
    void resizeFramebuffer(int width, int height);

    // Basic drawing functions
    Pixel getPixel(Point p) const;
    void setPixel(Point p, u32 color);
    void setPixelWithDepth(Point p, f32 z, u32 color);

    void drawLine(Point p0, Point p1, u32 color);

    void drawPolygon(const std::vector<Point>& points, u32 color, bool closed = true);
    void drawPolygon(const std::vector<Point>& points, const std::vector<u32>& colors, bool closed = true);
    void drawFilledPolygon(const std::vector<Point>& points, u32 color);

    /**
     * @brief Rasterise a single screen-space triangle.
     *
     * Implements a half-space (edge-function) rasteriser with:
     *   - Barycentric perspective-correct attribute interpolation
     *   - Per-pixel depth test (when Config::useDepthBuffer is true)
     *   - Optional nearest-neighbour texture sampling
     *   - Vertex-colour × texture-colour modulation
     *
     * @param v0,v1,v2  Screen-space vertices from the vertex transform stage.
     * @param texture   Optional texture; pass nullptr for untextured geometry.
     */
    void drawTriangle(const ScreenVertex& v0, const ScreenVertex& v1,
                      const ScreenVertex& v2, const Texture* texture);

    // Text rendering
    void drawText(const std::string& text, Point p, u32 color, u32 fontSize = 2);

    // Getters
    i32 getWidth() const { return settings.width; }
    i32 getHeight() const { return settings.height; }
    i32 getTextWidth(const std::string& text, u32 fontSize = 2);
    i32 getTextHeight(const std::string& text, u32 fontSize = 2);

    // Helper methods
    void plotPixel(Point p, f32 intensity, u32 color);
    bool isInsideBounds(Point p) const;
    u32 blendColors(u32 c1, u32 c2, f32 alpha);

    template<typename T>
    T interpolate(const T& a, const T& b, float t_param, InterpolationMethod method = InterpolationMethod::Linear);

private:
    float get_eased_time(float t_param, InterpolationMethod method);

private:

    Config settings;
    AlignedVector<Pixel> framebuffer;
    SDL_Window* _window;
    SDL_Renderer* _renderer;
    SDL_Texture* _texture;

    // Aura Font
    const AuraBitmapFont& _font;
};

}
} // namespace aura3d

#endif // CPUFRAMEBUFFERMANAGER_H
