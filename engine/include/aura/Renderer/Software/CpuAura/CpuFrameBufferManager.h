#ifndef CPUFRAMEBUFFERMANAGER_H
#define CPUFRAMEBUFFERMANAGER_H

#include <cmath>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <glm/glm.hpp>

static const float PI_FLOAT = std::acos(-1.0f);

#include "aura/Core/AuraCore.h"
#include "aura/Utils/AlignedVector.h"
#include "aura/Core/AuraFont/AuraBitmapFont.h"

/// wma owns the platform window and exposes the CPU framebuffer through its
/// lockFramebuffer()/presentFramebuffer() contract. The manager only needs a
/// non-owning handle to it, so a forward declaration keeps the backend-specific
/// wma/SDL headers out of this public header.
namespace wma { class IWindowManager; }

//! Forward-declared for the same reason: keeps ink/ThreadPool.h (and its
//! <thread>/<mutex> transitive includes) out of every translation unit that
//! merely includes this header to draw a triangle.
namespace ink { class ThreadPool; }

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

//! One triangle's worth of already-projected vertices, as consumed by the
//! batched/parallel drawTriangles()/drawTriangles2D() entry points.
struct ScreenTriangle {
    ScreenVertex v0, v1, v2;
};

class CpuFrameBufferManager
{
public:
    struct Config {
        i32 width = 1280;
        i32 height = 720;
        bool useDepthBuffer = true;
        //! Worker threads drawTriangles()/drawTriangles2D() split a frame
        //! across. 0 auto-detects via std::thread::hardware_concurrency();
        //! see AuraSettings::getCpuThreads(), which feeds this in practice.
        i32 threadCount = 0;
    };

    /// @param windowManager  wma window created with GraphicsAPI::CPU. Must
    ///                        outlive this manager (owned by the CPURenderer).
    /// @param config          Framebuffer dimensions and depth-buffer settings.
    CpuFrameBufferManager(wma::IWindowManager& windowManager, Config config);
    ~CpuFrameBufferManager() = default;

    // Core rendering
    void clear(u32 color = 0);

    /// Present the color plane by locking the backend's software framebuffer
    /// (wma::IWindowManager::lockFramebuffer()), blitting into it in parallel
    /// across CPU cores via wma::parallelFill(), then presenting it.
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

    /**
     * @brief Rasterise a screen-space triangle for the unlit 2D overlay pass.
     *
     * The software counterpart of the GPU backends' 2D pipeline, and it differs
     * from drawTriangle() in exactly the ways that pipeline does:
     *   - Affine (not perspective-correct) attribute interpolation. The overlay
     *     projection is orthographic, so w is 1 everywhere and the perspective
     *     divide would be an identity operation.
     *   - No depth test and no depth write, so the batch always composites on
     *     top of the scene already drawn.
     *   - Straight source-over alpha blending against the colour plane, rather
     *     than an opaque overwrite. This is what lets an antialiased glyph's
     *     partially covered edge texels fade into the background.
     *
     * @param v0,v1,v2  Vertices already in pixel coordinates.
     * @param texture   Optional texture; nullptr draws vertex colour alone.
     */
    void drawTriangle2D(const ScreenVertex& v0, const ScreenVertex& v1,
                        const ScreenVertex& v2, const Texture* texture);

    /**
     * @brief Rasterises a whole triangle list across every CPU core.
     *
     * drawTriangle() alone leaves rasterisation entirely on the calling
     * thread -- only the final present blit (renderFramebuffer()) is
     * parallel -- which is fine for a handful of triangles but is exactly
     * where a several-thousand-triangle mesh spends its time. This splits the
     * framebuffer into one horizontal row-band per hardware thread (the same
     * partitioning renderFramebuffer() uses) and has each thread rasterise
     * every triangle, clipped to its own band. Because bands own disjoint
     * rows, there is no shared mutable state between threads and no locking:
     * two threads never write the same pixel.
     *
     * Every thread scans the full triangle list -- the redundant iteration
     * cost is a handful of bounding-box comparisons per triangle per band,
     * negligible next to the per-pixel shading work an in-band triangle
     * actually costs.
     *
     * Blocks until every band has finished, so the framebuffer is complete
     * when this returns.
     *
     * @param triangles Already-projected triangles (see @ref ScreenTriangle).
     * @param texture   Optional shared texture; pass nullptr for untextured.
     */
    void drawTriangles(std::span<const ScreenTriangle> triangles, const Texture* texture);

    /// The drawTriangle2D() analogue of drawTriangles(): unlit, alpha-blended,
    /// affine-interpolated, parallel across row-bands.
    void drawTriangles2D(std::span<const ScreenTriangle> triangles, const Texture* texture);

    // Text rendering
    void drawText(const std::string& text, Point p, u32 color, u32 fontSize = 2);

    // Getters
    i32 getWidth() const { return settings.width; }
    i32 getHeight() const { return settings.height; }
    //! Worker count drawTriangles()/drawTriangles2D() actually dispatch across
    //! (the resolved value of Config::threadCount, after auto-detection).
    i32 getWorkerCount() const noexcept { return _workerCount; }
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

    /**
     * @brief Core of drawTriangle(), restricted to rows @c [yStart, yEnd).
     *
     * drawTriangle() itself is this called with the full framebuffer height;
     * drawTriangles() calls it once per row-band from worker threads. Callers
     * are responsible for the bands being disjoint -- that disjointness is
     * the entire basis for this being safe to call concurrently.
     */
    void rasterizeTriangleSpan(const ScreenVertex& v0, const ScreenVertex& v1,
                               const ScreenVertex& v2, const Texture* texture,
                               i32 yStart, i32 yEnd);

    /// The drawTriangle2D() analogue of rasterizeTriangleSpan().
    void rasterizeTriangle2DSpan(const ScreenVertex& v0, const ScreenVertex& v1,
                                 const ScreenVertex& v2, const Texture* texture,
                                 i32 yStart, i32 yEnd);

    //! Splits [0, settings.height) into row-bands and runs @p rasterizeBand(yStart,
    //! yEnd) on each, one per hardware thread, blocking until all complete.
    //! Shared by drawTriangles() and drawTriangles2D().
    void dispatchRowBands(const std::function<void(i32 yStart, i32 yEnd)>& rasterizeBand);

private:

    Config settings;
    AlignedVector<Pixel> framebuffer;

    //! Non-owning handle to the presenting window; the CPURenderer owns it and
    //! guarantees it outlives this manager.
    wma::IWindowManager* _windowManager;

    //! Config::threadCount resolved once at construction (auto-detection
    //! applied). Declared before _rasterPool so it is initialized first --
    //! the pool's size depends on it -- and reused by every dispatchRowBands()
    //! call so the number of bands always matches the pool's actual size.
    //! Both of those only hold because it is assigned in the constructor's
    //! initializer list; assigning it in the body runs after _rasterPool is
    //! already built and silently pins the pool to one thread.
    i32 _workerCount = 1;

    //! Backs drawTriangles()/drawTriangles2D(). Owned here (rather than shared
    //! with wma's presentation pool) because rasterisation and presentation
    //! are logically separate workloads that happen to both want "one task
    //! per core"; owning it also keeps this class usable independent of wma.
    std::unique_ptr<ink::ThreadPool> _rasterPool;

    // Aura Font
    const AuraBitmapFont& _font;
};

}
} // namespace aura3d

#endif // CPUFRAMEBUFFERMANAGER_H
