#include "aura/Renderer/Software/CpuAura/CpuFrameBufferManager.h"

#include <cstring>
#include <cmath>
#include <algorithm>
#include <future>
#include <thread>
#include <vector>

#include <ink/ThreadPool.h>
#include <wma/managers/IWindowManager.hpp>
#include <wma/rendering/SoftwareRenderer.hpp>

namespace aura3d {
namespace cpu {

namespace {

/**
 * @brief Resolves Config::threadCount, applying auto-detection for 0.
 *
 * A free function rather than constructor-body code because _rasterPool's
 * initializer needs the resolved value: member initializers run before the
 * body, so anything assigned in the body arrives too late to size the pool.
 */
[[nodiscard]] i32 resolveWorkerCount(i32 configured) noexcept
{
    if (configured > 0)
        return configured;

    const unsigned detected = std::thread::hardware_concurrency();
    return detected > 0 ? static_cast<i32>(detected) : 1;
}

} // namespace

/**
 * Constructor - allocates the CPU colour/depth plane. Presentation is delegated
 * to the wma window manager, so no backend (SDL/X11/Wayland) objects are owned
 * here; wma::IWindowManager::lockFramebuffer() hands us the surface each frame.
 * @param windowManager wma window created with GraphicsAPI::CPU.
 * @param config        Width, height, depth-buffer and worker-count settings.
 */
CpuFrameBufferManager::CpuFrameBufferManager(wma::IWindowManager& windowManager, Config config) :
    settings(config),
    // Initialize framebuffer with Pixel objects: black color (0) and max depth (1.0f)
    framebuffer(static_cast<size_t>(config.width) * static_cast<size_t>(config.height), Pixel{0, 1.0f}),
    _windowManager(&windowManager),
    /*
     * Resolved here, in the initializer list, and not in the body: _rasterPool
     * is constructed from it on the very next line. Assigning _workerCount in
     * the constructor body instead left the pool permanently sized to
     * _workerCount's default of 1, so dispatchRowBands() split the frame into
     * hardware_concurrency() bands and then fed all of them to a single
     * worker -- correct output, zero parallelism.
     */
    _workerCount(resolveWorkerCount(config.threadCount)),
    _rasterPool(std::make_unique<ink::ThreadPool>(static_cast<size_t>(_workerCount))),
    _font(GetDefaultBitmapFont())
{
}

/**
 * Clears the framebuffer to a specific color and resets depth
 * @param color 32-bit color value (0xRRGGBB format, alpha is ignored)
 */
void CpuFrameBufferManager::clear(u32 color)
{
    // Create a pixel with the specified color and maximum depth (1.0f)
    const Pixel clearPixel(color, 1.0f);

    // Use std::fill for a clean and efficient way to clear the entire framebuffer
    std::fill(framebuffer.begin(), framebuffer.end(), clearPixel);
}

/**
 * Presents the colour plane through wma's software-render contract.
 *
 * Process:
 * 1. Acquire the backend's CPU-writable surface via lockFramebuffer().
 * 2. Copy our packed ARGB8888 colours into it — honouring the surface pitch —
 *    in parallel across CPU cores with wma::parallelFill(), the software
 *    analogue of a GPU spreading pixel work across its execution units.
 * 3. Hand the surface back with presentFramebuffer(), which blits it to screen.
 *
 * This works on every wma backend that supports GraphicsAPI::CPU, with no
 * direct dependency on any windowing library.
 */
void CpuFrameBufferManager::renderFramebuffer()
{
    if (_windowManager == nullptr)
        return;

    const wma::SoftwareFramebuffer target = _windowManager->lockFramebuffer();
    if (!target.valid())
        return;

    // Our plane and the locked surface can momentarily disagree on size (a
    // resize event not yet propagated through handleWindowChanges), so bound
    // every sample to the plane we actually own.
    const i32 planeWidth  = settings.width;
    const i32 planeHeight = settings.height;

    wma::parallelFill(target, [this, planeWidth, planeHeight](i32 x, i32 y) noexcept -> u32 {
        if (x < planeWidth && y < planeHeight)
            return framebuffer[static_cast<size_t>(y) * static_cast<size_t>(planeWidth)
                             + static_cast<size_t>(x)].rgb;
        return 0u;
    });

    _windowManager->presentFramebuffer();
}

/**
 * Resizes the CPU framebuffer to new dimensions. The backend surface tracks the
 * window on its own (lockFramebuffer() always returns the current size), so
 * only our own colour/depth plane needs reallocating here.
 * @param width New width in pixels
 * @param height New height in pixels
 */
void CpuFrameBufferManager::resizeFramebuffer(int width, int height)
{
    // Validate input dimensions
    if (width <= 0 || height <= 0) {
        INK_ERROR << "Error: Invalid framebuffer dimensions (" << width << "x" << height << ")";
        return;
    }

    // Check if resize is actually needed
    if (width == settings.width && height == settings.height) {
        return;
    }

    settings.width = width;
    settings.height = height;

    // Resize the framebuffer vector, initializing new pixels to black with max depth
    framebuffer.assign(static_cast<size_t>(width) * static_cast<size_t>(height), Pixel{0, 1.0f});
}

/**
 * Checks if coordinates are within the framebuffer bounds
 * @param p Point to check
 * @return true if coordinates are valid, false otherwise
 */
bool CpuFrameBufferManager::isInsideBounds(Point p) const
{
    return p.x >= 0 && p.x < settings.width && p.y >= 0 && p.y < settings.height;
}

/**
 * Sets a pixel color at the specified coordinates, leaving depth unchanged.
 * @param p Point coordinates
 * @param color 32-bit color value (0xRRGGBB format)
 */
void CpuFrameBufferManager::setPixel(Point p, u32 color)
{
    if (isInsideBounds(p)) {
        framebuffer[p.y * settings.width + p.x].rgb = color;
    }
}

/**
 * Sets a pixel color and its depth value.
 * @param p Point coordinates
 * @param z Depth value (smaller values are closer to camera)
 * @param color 32-bit color value (0xRRGGBB format)
 */
void CpuFrameBufferManager::setPixelWithDepth(Point p, f32 z, u32 color)
{
    if (isInsideBounds(p)) {
        const int index = p.y * settings.width + p.x;
        // NOTE: A proper depth test would be `if (z < framebuffer[index].z)`
        // This function just sets the values unconditionally.
        framebuffer[index].rgb = color;
        if (settings.useDepthBuffer) {
            framebuffer[index].z = z; // Update depth buffer value
        }
    }
}

/**
 * Helper function for anti-aliased line drawing
 * @param p Coordinates
 * @param intensity Alpha value (0.0 to 1.0)
 * @param color Line color
 */
void CpuFrameBufferManager::plotPixel(Point p, f32 intensity, u32 color)
{
    if (!isInsideBounds(p)) return;
    // Get existing color from the Pixel struct and blend with new color
    u32 bg = getPixel(p).rgb;
    u32 blended = blendColors(bg, color, intensity);
    setPixel(p, blended);
}

/**
 * Gets the Pixel object (color and depth) at the specified coordinates
 * @param p Point coordinate
 * @return Pixel object, or a default black pixel if coordinates are invalid
 */
Pixel CpuFrameBufferManager::getPixel(Point p) const
{
    if (isInsideBounds(p)) {
        return framebuffer[p.y * settings.width + p.x];
    }
    return Pixel{0, 1.0f}; // Return black, max-depth pixel
}

/**
 * Draws a line using Bresenham's algorithm
 * @param p0 Starting point coordinates
 * @param p1 Ending point coordinates
 * @param color Line color
 */
void CpuFrameBufferManager::drawLine(Point p0, Point p1, u32 color)
{
    i32 x0 = p0.x, y0 = p0.y;
    i32 x1 = p1.x, y1 = p1.y;

    i32 dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    i32 dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    i32 err = dx + dy; // error value

    while (true)
    {
        setPixel({x0, y0}, color);
        if (x0 == x1 && y0 == y1) break;
        i32 e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

float CpuFrameBufferManager::get_eased_time(float t_param, InterpolationMethod method)
{
    float t = INK_CLAMP(t_param, 0.0f, 1.0f);
    switch (method) {
    case InterpolationMethod::Linear:
        return t;
    case InterpolationMethod::Smoothstep:
        return t * t * (3.0f - 2.0f * t);
    case InterpolationMethod::Accelerate:
        return t * t;
    case InterpolationMethod::Decelerate:
        return 1.0f - (1.0f - t) * (1.0f - t);
    case InterpolationMethod::Sine:
        return std::sin(t * PI_FLOAT * 0.5f);
    case InterpolationMethod::Cosine:
        return (1.0f - std::cos(t * PI_FLOAT)) * 0.5f;
    default:
        return t; // Fallback to linear
    }
}

// Generic interpolate function
template<typename T>
T CpuFrameBufferManager::interpolate(const T& a, const T& b, float t_param, InterpolationMethod method)
{
    float t_eased = get_eased_time(t_param, method);
    return a + (b - a) * t_eased;
}

// Polygon outline drawer function
void CpuFrameBufferManager::drawPolygon(const std::vector<Point>& points, u32 color, bool closed)
{
    if (points.size() < 2) return;

    for (size_t i = 0; i < points.size() - 1; ++i) {
        drawLine(points[i], points[i + 1], color);
    }

    if (closed && points.size() > 2) {
        drawLine(points.back(), points.front(), color);
    }
}

void CpuFrameBufferManager::drawPolygon(const std::vector<Point>& points, const std::vector<u32>& colors, bool closed) {
    if (points.size() < 2) return;

    if (colors.size() < (closed ? points.size() : points.size() - 1)) {
        drawPolygon(points, colors.empty() ? 0xFFFFFFFF : colors[0], closed);
        return;
    }

    for (size_t i = 0; i < points.size() - 1; ++i) {
        drawLine(points[i], points[i + 1], colors[i]);
    }

    if (closed && points.size() > 2) {
        drawLine(points.back(), points.front(), colors[points.size() - 1]);
    }
}

void CpuFrameBufferManager::drawFilledPolygon(const std::vector<Point>& points, u32 color) {
    if (points.size() < 3) return;

    int minY = getHeight();
    int maxY = 0;

    for (const auto& p : points) {
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }

    minY = std::max(minY, 0);
    maxY = std::min(maxY, getHeight() - 1);

    std::vector<int> intersections;
    intersections.reserve(points.size());

    for (int y = minY; y <= maxY; y++) {
        intersections.clear();

        for (size_t i = 0; i < points.size(); i++) {
            const Point& p1 = points[i];
            const Point& p2 = points[(i + 1) % points.size()];

            if ((p1.y == p2.y) || (y < std::min(p1.y, p2.y)) || (y >= std::max(p1.y, p2.y)))
                continue;

            float t = static_cast<float>(y - p1.y) / static_cast<float>(p2.y - p1.y);
            int x = p1.x + static_cast<int>(t * (p2.x - p1.x));

            intersections.push_back(x);
        }

        std::sort(intersections.begin(), intersections.end());

        for (size_t i = 0; i < intersections.size(); i += 2) {
            if (i + 1 >= intersections.size()) break;

            int startX = std::max(intersections[i], 0);
            int endX = std::min(intersections[i + 1], getWidth() - 1);

            for (int x = startX; x <= endX; x++) {
                setPixel(Point(x, y), color);
            }
        }
    }
}


/**
 * Half-space (edge-function) triangle rasteriser.
 *
 * Pipeline per triangle
 * 1. Compute screen-space bounding box (clamped to viewport).
 * 2. Evaluate edge functions at each pixel centre (+0.5 sub-pixel bias).
 * 3. Accept pixels whose barycentric weights are all ≥ 0 (handles both
 *    CW and CCW winding by normalising with the signed area).
 * 4. Depth test: discard fragment if z ≥ stored depth.
 * 5. Perspective-correct interpolation of UV and vertex colour.
 * 6. Nearest-neighbour texture sample (if texture != nullptr).
 * 7. Modulate texture colour by vertex colour, write pixel + depth.
 */ 
void CpuFrameBufferManager::drawTriangle(const ScreenVertex& v0, const ScreenVertex& v1,
                                         const ScreenVertex& v2, const Texture* texture)
{
    rasterizeTriangleSpan(v0, v1, v2, texture, 0, settings.height);
}

void CpuFrameBufferManager::rasterizeTriangleSpan(const ScreenVertex& v0, const ScreenVertex& v1,
                                                   const ScreenVertex& v2, const Texture* texture,
                                                   i32 yStart, i32 yEnd)
{
    // bbox, additionally clipped to [yStart, yEnd) -- the caller's row-band.
    const int xmin = std::max(0, (int)std::floor(std::min({v0.x, v1.x, v2.x})));
    const int xmax = std::min(settings.width - 1, (int)std::ceil(std::max({v0.x, v1.x, v2.x})));
    const int ymin = std::max(yStart, (int)std::floor(std::min({v0.y, v1.y, v2.y})));
    const int ymax = std::min(yEnd - 1, (int)std::ceil(std::max({v0.y, v1.y, v2.y})));

    if (xmin > xmax || ymin > ymax)
        return;

    // Signed area (2×) also serves as the edge-function denominator
    //   area2 = edgeFn(v0, v1, v2)
    //         = (v1.x−v0.x)·(v2.y−v0.y) − (v1.y−v0.y)·(v2.x−v0.x)
    const float area2 = (v1.x - v0.x) * (v2.y - v0.y)
                      - (v1.y - v0.y) * (v2.x - v0.x);

    if (std::abs(area2) < 1e-6f) 
        return;

    const float invArea2 = 1.0f / area2;

    // Pre-divide attributes by w for perspective-correct interpolation 
    const glm::vec2 uv0w = v0.uv * v0.invW;
    const glm::vec2 uv1w = v1.uv * v1.invW;
    const glm::vec2 uv2w = v2.uv * v2.invW;
    const glm::vec4 col0w = v0.color * v0.invW;
    const glm::vec4 col1w = v1.color * v1.invW;
    const glm::vec4 col2w = v2.color * v2.invW;

    // Rasterise
    for (int y = ymin; y <= ymax; ++y)
    {
        const float py = static_cast<float>(y) + 0.5f;

        for (int x = xmin; x <= xmax; ++x)
        {
            const float px = static_cast<float>(x) + 0.5f;

            // Edge functions:
            //   w0 = edgeFn(v1, v2, p)  →  barycentric weight for v0
            //   w1 = edgeFn(v2, v0, p)  →  barycentric weight for v1
            //   w2 = edgeFn(v0, v1, p)  →  barycentric weight for v2
            const float w0 = (v2.x - v1.x) * (py - v1.y) - (v2.y - v1.y) * (px - v1.x);
            const float w1 = (v0.x - v2.x) * (py - v2.y) - (v0.y - v2.y) * (px - v2.x);
            const float w2 = (v1.x - v0.x) * (py - v0.y) - (v1.y - v0.y) * (px - v0.x);

            // Inside test — normalise by sign of area to handle both windings.
            if (area2 > 0.0f) {
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
            } else {
                if (w0 > 0.0f || w1 > 0.0f || w2 > 0.0f) continue;
            }

            // Barycentric coordinates ∈ [0, 1],  b0+b1+b2 = 1
            const float b0 = w0 * invArea2;
            const float b1 = w1 * invArea2;
            const float b2 = w2 * invArea2;

            // Interpolated depth (linear in screen space is fine for NDC z)
            const float z = b0 * v0.z + b1 * v1.z + b2 * v2.z;

            // Depth test
            const int idx = y * settings.width + x;
            if (settings.useDepthBuffer && z >= framebuffer[idx].z) continue;

            // Perspective-correct 1/w
            const float invW = b0 * v0.invW + b1 * v1.invW + b2 * v2.invW;
            if (invW <= 0.0f) continue;
            const float perspW = 1.0f / invW;

            // Reconstruct UV and colour
            const glm::vec2 uv    = (b0 * uv0w  + b1 * uv1w  + b2 * uv2w)  * perspW;
            const glm::vec4 vcolor= glm::clamp(
                                        (b0 * col0w + b1 * col1w + b2 * col2w) * perspW,
                                        0.0f, 1.0f);

            // Texture sample (nearest-neighbour)
            const u32 texel = texture ? texture->sample(uv.x, uv.y) : 0xFFFFFFFFu;

            // Unpack texel (ARGB8888 — matches SDL_PIXELFORMAT_ARGB8888)
            const u8 ta = static_cast<u8>((texel >> 24) & 0xFFu);
            const u8 tr = static_cast<u8>((texel >> 16) & 0xFFu);
            const u8 tg = static_cast<u8>((texel >>  8) & 0xFFu);
            const u8 tb = static_cast<u8>( texel        & 0xFFu);

            // Modulate by vertex colour
            const u8 fr = static_cast<u8>(static_cast<float>(tr) * vcolor.r);
            const u8 fg = static_cast<u8>(static_cast<float>(tg) * vcolor.g);
            const u8 fb = static_cast<u8>(static_cast<float>(tb) * vcolor.b);
            const u8 fa = static_cast<u8>(static_cast<float>(ta) * vcolor.a);

            const u32 finalColor =  (static_cast<u32>(fa) << 24)
                                  | (static_cast<u32>(fr) << 16)
                                  | (static_cast<u32>(fg) <<  8)
                                  |  static_cast<u32>(fb);

            // Write pixel and depth
            framebuffer[idx].rgb = finalColor;
            if (settings.useDepthBuffer) {
                framebuffer[idx].z = z;
            }
        }
    }
}

void CpuFrameBufferManager::drawTriangle2D(const ScreenVertex& v0, const ScreenVertex& v1,
                                           const ScreenVertex& v2, const Texture* texture)
{
    rasterizeTriangle2DSpan(v0, v1, v2, texture, 0, settings.height);
}

void CpuFrameBufferManager::rasterizeTriangle2DSpan(const ScreenVertex& v0, const ScreenVertex& v1,
                                                     const ScreenVertex& v2, const Texture* texture,
                                                     i32 yStart, i32 yEnd)
{
    const int xmin = std::max(0, static_cast<int>(std::floor(std::min({v0.x, v1.x, v2.x}))));
    const int xmax = std::min(settings.width - 1, static_cast<int>(std::ceil(std::max({v0.x, v1.x, v2.x}))));
    const int ymin = std::max(yStart, static_cast<int>(std::floor(std::min({v0.y, v1.y, v2.y}))));
    const int ymax = std::min(yEnd - 1, static_cast<int>(std::ceil(std::max({v0.y, v1.y, v2.y}))));

    if (xmin > xmax || ymin > ymax)
        return;

    //! Twice the signed area, doubling as the edge-function denominator.
    const float area2 = (v1.x - v0.x) * (v2.y - v0.y)
                      - (v1.y - v0.y) * (v2.x - v0.x);

    if (std::abs(area2) < 1e-6f)
        return; //! Degenerate: zero-area triangle covers nothing.

    const float invArea2 = 1.0f / area2;

    for (int y = ymin; y <= ymax; ++y)
    {
        const float py = static_cast<float>(y) + 0.5f;

        for (int x = xmin; x <= xmax; ++x)
        {
            const float px = static_cast<float>(x) + 0.5f;

            const float w0 = (v2.x - v1.x) * (py - v1.y) - (v2.y - v1.y) * (px - v1.x);
            const float w1 = (v0.x - v2.x) * (py - v2.y) - (v0.y - v2.y) * (px - v2.x);
            const float w2 = (v1.x - v0.x) * (py - v0.y) - (v1.y - v0.y) * (px - v0.x);

            //! Inside test, normalised by the area's sign so either winding works.
            if (area2 > 0.0f) {
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
            } else {
                if (w0 > 0.0f || w1 > 0.0f || w2 > 0.0f) continue;
            }

            //! Affine barycentrics: orthographic projection means w is constant.
            const float b0 = w0 * invArea2;
            const float b1 = w1 * invArea2;
            const float b2 = w2 * invArea2;

            const glm::vec2 uv = b0 * v0.uv + b1 * v1.uv + b2 * v2.uv;
            const glm::vec4 vcolor = glm::clamp(b0 * v0.color + b1 * v1.color + b2 * v2.color,
                                                0.0f, 1.0f);

            const u32 texel = texture ? texture->sample(uv.x, uv.y) : 0xFFFFFFFFu;

            //! ARGB8888, matching SDL_PIXELFORMAT_ARGB8888.
            const float ta = static_cast<float>((texel >> 24) & 0xFFu) / 255.0f;
            const float tr = static_cast<float>((texel >> 16) & 0xFFu);
            const float tg = static_cast<float>((texel >>  8) & 0xFFu);
            const float tb = static_cast<float>( texel        & 0xFFu);

            //! Unlit: texel * vertex colour, exactly like the GPU 2D shader.
            const float srcA = ta * vcolor.a;
            if (srcA <= 0.0f)
                continue; //! Fully transparent: nothing to composite.

            const float srcR = tr * vcolor.r;
            const float srcG = tg * vcolor.g;
            const float srcB = tb * vcolor.b;

            const int idx = y * settings.width + x;
            const u32 dst = framebuffer[idx].rgb;

            const float dstR = static_cast<float>((dst >> 16) & 0xFFu);
            const float dstG = static_cast<float>((dst >>  8) & 0xFFu);
            const float dstB = static_cast<float>( dst        & 0xFFu);

            /*
             * Source-over: out = src * a + dst * (1 - a). The same operation
             * GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA performs on the GPU paths.
             */
            const float invA = 1.0f - srcA;
            const u8 outR = static_cast<u8>(srcR * srcA + dstR * invA);
            const u8 outG = static_cast<u8>(srcG * srcA + dstG * invA);
            const u8 outB = static_cast<u8>(srcB * srcA + dstB * invA);

            //! Colour plane only: the overlay never touches the depth buffer.
            framebuffer[idx].rgb = 0xFF000000u
                                 | (static_cast<u32>(outR) << 16)
                                 | (static_cast<u32>(outG) <<  8)
                                 |  static_cast<u32>(outB);
        }
    }
}

void CpuFrameBufferManager::dispatchRowBands(const std::function<void(i32 yStart, i32 yEnd)>& rasterizeBand)
{
    if (settings.height <= 0)
        return;

    const i32 bands = std::min(_workerCount, settings.height);
    const i32 rowsPerBand = settings.height / bands;
    const i32 remainder = settings.height % bands;

    std::vector<std::future<void>> futures;
    futures.reserve(static_cast<size_t>(bands));

    i32 y = 0;
    for (i32 b = 0; b < bands; ++b)
    {
        //! Distribute the remainder across the first `remainder` bands rather
        //! than dumping it all on the last one, so no single thread is left
        //! with a visibly taller slice than its neighbours.
        const i32 bandRows = rowsPerBand + (b < remainder ? 1 : 0);
        const i32 yStart = y;
        const i32 yEnd = y + bandRows;
        y = yEnd;

        futures.push_back(_rasterPool->submit([&rasterizeBand, yStart, yEnd] {
            rasterizeBand(yStart, yEnd);
        }));
    }

    for (auto& f : futures)
        f.get();
}

void CpuFrameBufferManager::drawTriangles(std::span<const ScreenTriangle> triangles, const Texture* texture)
{
    if (triangles.empty())
        return;

    dispatchRowBands([this, triangles, texture](i32 yStart, i32 yEnd) {
        for (const ScreenTriangle& tri : triangles)
            rasterizeTriangleSpan(tri.v0, tri.v1, tri.v2, texture, yStart, yEnd);
    });
}

void CpuFrameBufferManager::drawTriangles2D(std::span<const ScreenTriangle> triangles, const Texture* texture)
{
    if (triangles.empty())
        return;

    dispatchRowBands([this, triangles, texture](i32 yStart, i32 yEnd) {
        for (const ScreenTriangle& tri : triangles)
            rasterizeTriangle2DSpan(tri.v0, tri.v1, tri.v2, texture, yStart, yEnd);
    });
}

/**
 * Blends two colors according to an alpha value
 * @param c1 Background color
 * @param c2 Foreground color
 * @param alpha Blend factor (0.0 = all c1, 1.0 = all c2)
 * @return Blended color
 */
u32 CpuFrameBufferManager::blendColors(u32 c1, u32 c2, f32 alpha) {
    if (alpha <= 0.0f) return c1;
    if (alpha >= 1.0f) return c2;

    u8 r1 = (c1 >> 16) & 0xFF;
    u8 g1 = (c1 >> 8) & 0xFF;
    u8 b1 = c1 & 0xFF;
    u8 r2 = (c2 >> 16) & 0xFF;
    u8 g2 = (c2 >> 8) & 0xFF;
    u8 b2 = c2 & 0xFF;

    u8 r = static_cast<u8>(r1 * (1.0f - alpha) + r2 * alpha);
    u8 g = static_cast<u8>(g1 * (1.0f - alpha) + g2 * alpha);
    u8 b = static_cast<u8>(b1 * (1.0f - alpha) + b2 * alpha);

    return (r << 16) | (g << 8) | b;
}

void CpuFrameBufferManager::drawText(const std::string& text, Point p, u32 color, u32 fontSize) {
    int cursorX = p.x;
    int scaledWidth = _font.charWidth * fontSize;
    int scaledHeight = _font.charHeight * fontSize;
    int scaledSpacing = _font.charSpacing * fontSize;

    for (char c : text) {
        if (c == '\n') {
            cursorX = p.x;
            p.y += scaledHeight + scaledSpacing;
            continue;
        }

        if ((c < 0) || (c > 127))
            c = '?';

        if (cursorX >= settings.width || p.y >= settings.height || cursorX + scaledWidth <= 0 || p.y + scaledHeight <= 0) {
            cursorX += scaledWidth + scaledSpacing;
            continue;
        }

        const auto& charData = _font.data[static_cast<unsigned char>(c)];

        for (i32 row = 0; row < _font.charHeight; row++) {
            u8 rowBits = charData[row];
            for (u32 scaleY = 0; scaleY < fontSize; scaleY++) {
                i32 pixelY = p.y + static_cast<int>(row * fontSize) + scaleY;
                if (pixelY < 0 || pixelY >= settings.height) continue;

                for (i32 col = 0; col < _font.charWidth; col++) {
                    bool isPixelOn = (rowBits & (1 << (_font.charWidth - 1 - col))) != 0;
                    if (isPixelOn) {
                        for (u32 scaleX = 0; scaleX < fontSize; scaleX++) {
                            i32 pixelX = cursorX + static_cast<int>(col * fontSize) + scaleX;
                            if (pixelX >= 0 && pixelX < settings.width) {
                                setPixel({ pixelX, pixelY }, color);
                            }
                        }
                    }
                }
            }
        }
        cursorX += scaledWidth + scaledSpacing;
    }
}

int CpuFrameBufferManager::getTextWidth(const std::string& text, u32 fontSize)
{
    i32 scaledWidth = _font.charWidth * fontSize;
    i32 scaledSpacing = _font.charSpacing * fontSize;

    i32 width = 0;
    i32 maxWidth = 0;

    for (char c : text) {
        if (c == '\n') {
            maxWidth = std::max(maxWidth, width);
            width = 0;
            continue;
        }

        width += scaledWidth + scaledSpacing;
    }

    return std::max(maxWidth, width);
}

int CpuFrameBufferManager::getTextHeight(const std::string& text, u32 fontSize)
{
    int scaledHeight = _font.charHeight * fontSize;
    int lines = 1;

    for (char c : text) {
        if (c == '\n') {
            lines++;
        }
    }

    return lines * (scaledHeight + 1) - 1;
}

}
}  // namespace aura3d
