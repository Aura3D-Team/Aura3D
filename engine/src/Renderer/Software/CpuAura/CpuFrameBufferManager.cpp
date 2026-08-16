#include "aura/Renderer/Software/CpuAura/CpuFrameBufferManager.h"

#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>

#include <wma/managers/IWindowManager.hpp>

#include "aura/Core/JobSystem/JobSystem.h"

namespace aura3d {
namespace cpu {

namespace {

/**
 * @brief Whether the directed edge @p a -> @p b is a top or left edge of a
 *        positive-area triangle, in this rasteriser's Y-down screen space.
 *
 * The fill rule's classification, derived from the same edge function the
 * rasteriser uses, @c edgeFn(A,B,P) = (B-A) x (P-A):
 *
 *  - A horizontal edge (@c ey == 0) has the interior below it exactly when
 *    @c ex > 0, which makes it the triangle's *top* edge.
 *  - A non-horizontal edge has the interior to its right exactly when
 *    @c ey < 0, which makes it a *left* edge.
 *
 * Note this is the mirror of the classification usually quoted for Y-up
 * rasterisers: flipping the Y axis reverses every winding with it.
 */
[[nodiscard]] constexpr bool isTopLeftEdge(const ScreenVertex& a, const ScreenVertex& b) noexcept
{
    const f32 ex = b.x - a.x;
    const f32 ey = b.y - a.y;
    return (ey == 0.0f && ex > 0.0f) || (ey < 0.0f);
}

} // namespace

/**
 * Constructor - allocates the CPU colour/depth plane. Presentation is delegated
 * to the wma window manager, so no backend (SDL/X11/Wayland) objects are owned
 * here; wma::IWindowManager::lockFramebuffer() hands us the surface each frame.
 * @param windowManager wma window created with GraphicsAPI::CPU.
 * @param config        Width, height and depth-buffer settings.
 * @param jobs          Engine-wide worker pool this manager dispatches
 *                       rasterisation and presentation across; see _jobs.
 */
CpuFrameBufferManager::CpuFrameBufferManager(wma::IWindowManager& windowManager, Config config, const JobSystem& jobs) :
    settings(config),
    // Initialize framebuffer with Pixel objects: black color (0) and max depth (1.0f)
    framebuffer(static_cast<size_t>(config.width) * static_cast<size_t>(config.height), Pixel{0, 1.0f}),
    _windowManager(&windowManager),
    _jobs(&jobs),
    _workerCount(jobs.workerCount()),
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
 *    in parallel row-bands across the engine's shared worker pool (the same
 *    one flush() rasterises with; see _jobs), the software analogue of a GPU
 *    spreading pixel work across its execution units.
 * 3. Hand the surface back with presentFramebuffer(), which blits it to screen.
 *
 * wma's role here is purely the raw surface handoff (lockFramebuffer() /
 * presentFramebuffer()) -- the parallel fill itself used to run through wma's
 * own parallelFill() helper, backed by a second, separate process-wide pool
 * of wma's own. wma is the window manager, not the renderer; the renderer is
 * this class, so the parallelism the renderer needs belongs to it too.
 * parallelFill() and the pool behind it are gone from wma entirely now --
 * this class was its only real consumer.
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
    const Pixel* plane = framebuffer.data();

    void* const dstPixels = target.pixels;
    const i32 dstPitch = target.pitch;
    const i32 dstWidth = target.width;

    _jobs->dispatch(target.height, [=](i32 yStart, i32 yEnd) {
        for (i32 y = yStart; y < yEnd; ++y)
        {
            auto* row = reinterpret_cast<u32*>(static_cast<u8*>(dstPixels) + static_cast<size_t>(y) * dstPitch);
            const bool rowInPlane = y < planeHeight;

            for (i32 x = 0; x < dstWidth; ++x)
            {
                row[x] = (rowInPlane && x < planeWidth)
                             ? plane[static_cast<size_t>(y) * static_cast<size_t>(planeWidth) + static_cast<size_t>(x)].rgb
                             : 0u;
            }
        }
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

    /*
     * Reordered to a positive area so a single fill rule covers both windings.
     * Swapping two vertices flips the sign of the area and reverses every edge,
     * which is exactly the transformation isTopLeftEdge() is stated for; the
     * barycentrics below follow the reordered vertices, so attributes are
     * unaffected.
     */
    const ScreenVertex* p0 = &v0;
    const ScreenVertex* p1 = &v1;
    const ScreenVertex* p2 = &v2;

    float area2 = signedArea2(v0, v1, v2);
    if (area2 < 0.0f)
    {
        std::swap(p1, p2);
        area2 = -area2;
    }

    if (area2 < 1e-6f)
        return; //! Degenerate: zero-area triangle covers nothing.

    const float invArea2 = 1.0f / area2;

    /*
     * Top-left fill rule. Without it a pixel falling exactly on the edge two
     * triangles share belongs to *both*, and since this path blends rather than
     * overwrites, it is composited twice: the second pass adds
     * a*(1-a)*(src - dst) on top of the first. Along the diagonal every quad is
     * split on, that draws a seam -- brighter than its surroundings where the
     * source out-glows the destination, darker where it does not. Awarding a
     * shared edge to exactly one of the two triangles is what removes it.
     */
    const bool topLeft0 = isTopLeftEdge(*p1, *p2);
    const bool topLeft1 = isTopLeftEdge(*p2, *p0);
    const bool topLeft2 = isTopLeftEdge(*p0, *p1);

    for (int y = ymin; y <= ymax; ++y)
    {
        const float py = static_cast<float>(y) + 0.5f;

        for (int x = xmin; x <= xmax; ++x)
        {
            const float px = static_cast<float>(x) + 0.5f;

            const float w0 = (p2->x - p1->x) * (py - p1->y) - (p2->y - p1->y) * (px - p1->x);
            const float w1 = (p0->x - p2->x) * (py - p2->y) - (p0->y - p2->y) * (px - p2->x);
            const float w2 = (p1->x - p0->x) * (py - p0->y) - (p1->y - p0->y) * (px - p0->x);

            //! Inside test: strictly inside, or on an edge this triangle owns.
            if (w0 < 0.0f || (w0 == 0.0f && !topLeft0)) continue;
            if (w1 < 0.0f || (w1 == 0.0f && !topLeft1)) continue;
            if (w2 < 0.0f || (w2 == 0.0f && !topLeft2)) continue;

            //! Affine barycentrics: orthographic projection means w is constant.
            const float b0 = w0 * invArea2;
            const float b1 = w1 * invArea2;
            const float b2 = w2 * invArea2;

            const glm::vec2 uv = b0 * p0->uv + b1 * p1->uv + b2 * p2->uv;
            const glm::vec4 vcolor = glm::clamp(b0 * p0->color + b1 * p1->color + b2 * p2->color,
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

void CpuFrameBufferManager::updateBandRanges()
{
    _bandRanges.clear();

    if (settings.height <= 0)
        return;

    /*
     * One band per worker, not several: dispatchRowBands() now runs every
     * band through JobSystem::dispatch(), which statically partitions
     * [0, bandCount) into contiguous per-worker chunks ahead of time rather
     * than handing tasks out through a shared queue. Splitting finer than the
     * worker count used to help exactly because that queue let an idle
     * worker steal the next outstanding band from a busy one; a static
     * partition has nothing to steal from, so the extra bands would only add
     * more (now-pointless) dispatch overhead without recovering any of the
     * load-balance they used to buy. See the JobSystem/CpuFrameBufferManager
     * pool-consolidation notes for the trade this made: dynamic load
     * balancing traded for roughly a 4x cut in per-frame task submissions,
     * which measurement showed mattered far more for typical scene sizes.
     */
    const i32 bands = std::min(_workerCount, settings.height);
    const i32 rowsPerBand = settings.height / bands;
    const i32 remainder = settings.height % bands;

    _bandRanges.reserve(static_cast<size_t>(bands));

    i32 y = 0;
    for (i32 b = 0; b < bands; ++b)
    {
        //! Distribute the remainder across the first `remainder` bands rather
        //! than dumping it all on the last one, so no single thread is left
        //! with a visibly taller slice than its neighbours.
        const i32 bandRows = rowsPerBand + (b < remainder ? 1 : 0);
        _bandRanges.push_back({y, y + bandRows});
        y += bandRows;
    }
}

void CpuFrameBufferManager::dispatchRowBands(
    const std::function<void(i32 band, i32 yStart, i32 yEnd)>& rasterizeBand)
{
    if (_bandRanges.empty())
        return;

    //! JobSystem::dispatch() itself splits [0, bandCount) across the shared
    //! pool and blocks until every worker's slice is done; this just walks
    //! whichever contiguous slice of _bandRanges a given worker was handed.
    _jobs->dispatch(static_cast<i32>(_bandRanges.size()), [this, &rasterizeBand](i32 begin, i32 end) {
        for (i32 band = begin; band < end; ++band)
        {
            const BandRange range = _bandRanges[static_cast<size_t>(band)];
            rasterizeBand(band, range.yStart, range.yEnd);
        }
    });
}

void CpuFrameBufferManager::binQueuedTriangles()
{
    _bandBins.resize(_bandRanges.size());
    for (std::vector<u32>& bin : _bandBins)
        bin.clear();

    if (_bandRanges.empty())
        return;

    /*
     * Bands are contiguous and ordered, so the band a row belongs to is found
     * by walking forward from the previous triangle's band rather than
     * searching. In practice the whole loop is two comparisons per triangle.
     */
    const i32 bandCount = static_cast<i32>(_bandRanges.size());

    for (u32 index = 0; index < _queuedTriangles.size(); ++index)
    {
        const ScreenTriangle& tri = _queuedTriangles[index];

        const float minYf = std::min({tri.v0.y, tri.v1.y, tri.v2.y});
        const float maxYf = std::max({tri.v0.y, tri.v1.y, tri.v2.y});

        //! Matches the row range rasterizeTriangleSpan() derives from the same
        //! vertices, so a triangle is never binned away from a row it covers.
        const i32 minY = std::max(0, static_cast<i32>(std::floor(minYf)));
        const i32 maxY = std::min(settings.height - 1, static_cast<i32>(std::ceil(maxYf)));

        if (minY > maxY)
            continue; //! Entirely above or below the framebuffer.

        for (i32 band = 0; band < bandCount; ++band)
        {
            const BandRange range = _bandRanges[band];
            if (range.yStart > maxY)
                break; //! Bands are ordered; nothing further can overlap.
            if (range.yEnd > minY)
                _bandBins[band].push_back(index);
        }
    }
}

void CpuFrameBufferManager::submitTriangles(std::span<const ScreenTriangle> triangles,
                                            const Texture* texture)
{
    if (triangles.empty())
        return;

    QueuedBatch batch;
    batch.texture = texture;
    batch.overlay = false;
    batch.first = static_cast<u32>(_queuedTriangles.size());
    batch.count = static_cast<u32>(triangles.size());

    _queuedTriangles.insert(_queuedTriangles.end(), triangles.begin(), triangles.end());
    _queuedBatches.push_back(batch);
}

void CpuFrameBufferManager::submitTriangles2D(std::span<const ScreenTriangle> triangles,
                                              const Texture* texture)
{
    if (triangles.empty())
        return;

    QueuedBatch batch;
    batch.texture = texture;
    batch.overlay = true;
    batch.first = static_cast<u32>(_queuedTriangles.size());
    batch.count = static_cast<u32>(triangles.size());

    _queuedTriangles.insert(_queuedTriangles.end(), triangles.begin(), triangles.end());
    _queuedBatches.push_back(batch);
}

void CpuFrameBufferManager::flush()
{
    if (_queuedTriangles.empty())
    {
        _queuedBatches.clear();
        return;
    }

    updateBandRanges();
    binQueuedTriangles();

    dispatchRowBands([this](i32 band, i32 yStart, i32 yEnd) {
        const std::vector<u32>& bin = _bandBins[static_cast<size_t>(band)];

        /*
         * Walk the band's (ascending) triangle indices and the batch list
         * together. Batches partition _queuedTriangles into contiguous ranges
         * in submission order, so one linear pass visits every triangle in
         * exactly the order it was submitted -- which is what keeps the
         * blended 2D overlay compositing on top of the scene rather than
         * under it -- while carrying each triangle's texture and mode along
         * without storing them per triangle.
         */
        size_t cursor = 0;

        for (const QueuedBatch& batch : _queuedBatches)
        {
            const u32 end = batch.first + batch.count;

            while (cursor < bin.size() && bin[cursor] < end)
            {
                const ScreenTriangle& tri = _queuedTriangles[bin[cursor]];

                if (batch.overlay)
                    rasterizeTriangle2DSpan(tri.v0, tri.v1, tri.v2, batch.texture, yStart, yEnd);
                else
                    rasterizeTriangleSpan(tri.v0, tri.v1, tri.v2, batch.texture, yStart, yEnd);

                ++cursor;
            }
        }
    });

    //! clear() keeps the capacity: the next frame reuses these allocations.
    _queuedTriangles.clear();
    _queuedBatches.clear();
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
