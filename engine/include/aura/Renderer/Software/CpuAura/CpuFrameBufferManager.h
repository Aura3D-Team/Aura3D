#ifndef CPUFRAMEBUFFERMANAGER_H
#define CPUFRAMEBUFFERMANAGER_H

#include <cmath>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>
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

//! Forward-declared for the same reason this class used to forward-declare
//! ink::ThreadPool: keeps JobSystem.h out of every translation unit that
//! merely includes this header to draw a triangle. See the constructor's
//! comment for why a ThreadPool of its own is gone entirely now.
namespace aura3d { class JobSystem; }

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
//! batched/parallel submitTriangles()/submitTriangles2D() entry points.
struct ScreenTriangle {
    ScreenVertex v0, v1, v2;
};

/**
 * @brief Twice the signed area of a screen-space triangle.
 *
 * Doubles as the edge-function denominator inside the rasteriser and as the
 * winding test outside it; the two must agree, which is why there is one
 * definition rather than one per caller.
 */
[[nodiscard]]
constexpr f32 signedArea2(const ScreenVertex& v0, const ScreenVertex& v1,
                          const ScreenVertex& v2) noexcept
{
    return (v1.x - v0.x) * (v2.y - v0.y)
         - (v1.y - v0.y) * (v2.x - v0.x);
}

/**
 * @brief True when a projected triangle faces the camera.
 *
 * The convention has to match the GPU backends, which cull back faces with a
 * counter-clockwise front face, or the same asset renders solid under Vulkan
 * and inside-out here.
 *
 * Front faces have a *negative* signed area rather than a positive one because
 * this rasteriser's screen space has Y growing downwards, while the clip space
 * feeding it is the OpenGL [-1,1] convention with +Y up. That single flip
 * reverses the apparent winding of every triangle, so the test reverses with
 * it. (Exercised directly by tests/test_cpu_raster.cpp, which checks the sign
 * against face normals on a cube.)
 */
[[nodiscard]]
constexpr bool isFrontFacing(const ScreenVertex& v0, const ScreenVertex& v1,
                             const ScreenVertex& v2) noexcept
{
    return signedArea2(v0, v1, v2) < 0.0f;
}

class CpuFrameBufferManager
{
public:
    struct Config {
        i32 width = 1280;
        i32 height = 720;
        bool useDepthBuffer = true;
    };

    /// @param windowManager  wma window created with GraphicsAPI::CPU. Must
    ///                        outlive this manager (owned by the CPURenderer).
    /// @param config          Framebuffer dimensions and depth-buffer settings.
    /// @param jobs            Engine-wide worker pool (see JobSystem) this
    ///                        manager dispatches rasterisation and
    ///                        presentation across, instead of building a
    ///                        pool of its own. Must outlive this manager --
    ///                        Engine guarantees that: JobSystem is
    ///                        constructed before any renderer and survives
    ///                        every backend switch.
    CpuFrameBufferManager(wma::IWindowManager& windowManager, Config config, const JobSystem& jobs);
    ~CpuFrameBufferManager() = default;

    // Core rendering
    void clear(u32 color = 0);

    /// Present the color plane by locking the backend's software framebuffer
    /// (wma::IWindowManager::lockFramebuffer()) and blitting into it in
    /// parallel across the same worker pool flush() rasterises with.
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
     * @brief Queues an already-projected triangle list for this frame.
     *
     * Copies the triangles into the frame's batch queue and returns
     * immediately -- nothing is rasterised until flush().
     *
     * That deferral is the point. Rasterising here instead would mean one
     * thread-pool fan-out *per draw call*: a scene of 200 objects paid 200
     * rounds of waking every worker, splitting the framebuffer and joining
     * again, and each round's fixed cost is paid whether the draw covers the
     * screen or four pixels. Queuing lets a whole frame's geometry go out in a
     * single dispatch.
     *
     * @param triangles Already-projected triangles (see @ref ScreenTriangle).
     * @param texture   Optional shared texture; pass nullptr for untextured.
     *                  Must stay alive until flush() returns.
     */
    void submitTriangles(std::span<const ScreenTriangle> triangles, const Texture* texture);

    /// The drawTriangle2D() analogue of submitTriangles(): unlit, alpha-blended,
    /// affine-interpolated. Queued into the same list, so an overlay submitted
    /// after the scene still composites on top of it.
    void submitTriangles2D(std::span<const ScreenTriangle> triangles, const Texture* texture);

    /**
     * @brief Rasterises everything queued this frame, then empties the queue.
     *
     * The framebuffer is split into horizontal row-bands and each band is
     * rasterised by a worker thread. Because bands own disjoint rows there is
     * no shared mutable state and no locking: two threads never write the same
     * pixel.
     *
     * Within a band, batches are replayed in submission order, so the result is
     * pixel-identical to having rasterised each draw call as it arrived -- the
     * property the alpha-blended 2D overlay depends on, since blending is not
     * commutative.
     *
     * Each band rasterises only the triangles that actually reach its rows:
     * a binning pass buckets triangles by the bands their bounding boxes span
     * first, so a band no longer scans the whole frame's triangle list to
     * discover that most of it lies elsewhere.
     *
     * Blocks until every band has finished, so the framebuffer is complete
     * when this returns. Safe to call with an empty queue.
     */
    void flush();

    // Text rendering
    void drawText(const std::string& text, Point p, u32 color, u32 fontSize = 2);

    // Getters
    i32 getWidth() const { return settings.width; }
    i32 getHeight() const { return settings.height; }
    //! Worker count flush() and renderFramebuffer() dispatch across -- the
    //! engine's shared JobSystem::workerCount(), cached at construction. One
    //! row-band per worker (see updateBandRanges()), so this is also the band
    //! count.
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
     * flush() calls it once per row-band from worker threads. Callers
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

    //! Splits [0, settings.height) into row-bands and runs @p rasterizeBand(band,
    //! yStart, yEnd) on each, blocking until all complete. Band boundaries come
    //! from _bandRanges, so binning and rasterisation always agree on them.
    void dispatchRowBands(const std::function<void(i32 band, i32 yStart, i32 yEnd)>& rasterizeBand);

    //! Recomputes _bandRanges for the current height and worker count. Cheap,
    //! and called once per flush() so a resize cannot leave stale boundaries.
    void updateBandRanges();

    //! Buckets every queued triangle into the bands its bounding box touches.
    void binQueuedTriangles();

    /**
     * @brief One draw call's worth of queued triangles.
     *
     * `first`/`count` name a contiguous range of _queuedTriangles; batches
     * partition that array in submission order, which is what lets a band walk
     * its (ascending) bin and its batch list together in one linear pass.
     */
    struct QueuedBatch {
        const Texture* texture = nullptr;
        //! true -> rasterizeTriangle2DSpan (unlit, blended, no depth).
        bool overlay = false;
        u32 first = 0;
        u32 count = 0;
    };

    //! Half-open row range [yStart, yEnd) owned by one band.
    struct BandRange {
        i32 yStart = 0;
        i32 yEnd = 0;
    };

private:

    Config settings;
    AlignedVector<Pixel> framebuffer;

    //! Non-owning handle to the presenting window; the CPURenderer owns it and
    //! guarantees it outlives this manager.
    wma::IWindowManager* _windowManager;

    /*
     * Non-owning handle to the engine's shared worker pool. Both flush()
     * (rasterisation) and renderFramebuffer() (the copy into the locked
     * window surface) dispatch through it, via JobSystem::dispatch() -- so a
     * CPU-backend run spins up exactly one pool for the whole frame, not one
     * per workload. This used to be two: a ThreadPool this class built for
     * itself, plus a second, separate one wma's parallelFill() owned
     * internally for the presentation copy. Neither is needed once both
     * halves go through the same dispatch() call; wma is left with nothing
     * but lockFramebuffer()/presentFramebuffer(), the raw surface handoff,
     * which is all a *window* manager should own -- the parallel fill was
     * arguably the renderer's job in the first place, and the renderer here
     * is aura3d, not wma.
     */
    const JobSystem* _jobs;

    //! JobSystem::workerCount(), cached at construction so getWorkerCount()
    //! and updateBandRanges() don't re-derive it every frame.
    i32 _workerCount = 1;

    /*
     * Everything below is frame-scratch: cleared between frames but never
     * shrunk, so a steady-state frame rasterises without touching the
     * allocator. They used to be locals rebuilt per draw call -- a fresh
     * triangle vector per drawIndexed(), a fresh futures vector per dispatch --
     * which is allocation churn proportional to the scene's object count.
     */

    //! Every triangle queued this frame, in submission order.
    std::vector<ScreenTriangle> _queuedTriangles;
    //! Contiguous, ordered partition of _queuedTriangles; one per draw call.
    std::vector<QueuedBatch> _queuedBatches;
    /*
     * Per-band indices into _queuedTriangles, ascending. Built by
     * binQueuedTriangles() so a band rasterises only the triangles whose
     * bounding box actually reaches its rows, instead of scanning the whole
     * frame's list to reject most of it.
     */
    std::vector<std::vector<u32>> _bandBins;
    //! Row range owned by each band; index-aligned with _bandBins. One band
    //! per worker (see updateBandRanges()) -- JobSystem::dispatch() statically
    //! partitions its item range across the pool, so oversplitting into more
    //! bands than workers no longer buys anything: there is no shared task
    //! queue left for an idle worker to steal extra bands from.
    std::vector<BandRange> _bandRanges;

    // Aura Font
    const AuraBitmapFont& _font;
};

}
} // namespace aura3d

#endif // CPUFRAMEBUFFERMANAGER_H
