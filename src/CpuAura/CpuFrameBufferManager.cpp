#include "CpuFrameBufferManager.h"

#include <cstring>

#include "AuraException/AuraException.h"
#include "AuraLogger/AuraLogger.h"
#include "AuraAssert/AuraAssert.h"

namespace aura3d {

/**
 * Constructor - Initializes the framebuffer and optional depth buffer
 * @param config Configuration settings for width, height, and depth buffer usage
 */
CpuFrameBufferManager::CpuFrameBufferManager(SDL_Window* window, Config config) :
    settings(config),
    framebuffer(config.width * config.height, 0), // Initialize with black pixels
    _window(window),
    _renderer(nullptr),
    _texture(nullptr)
{
    // Create depth buffer if enabled in settings
    if (settings.useDepthBuffer) {
        depthBuffer.resize(config.width * config.height, 1.0f); // Initialize with max depth
    }

    _renderer = SDL_CreateRenderer(_window, -1, SDL_RENDERER_ACCELERATED);
    AURA_ASSERT_MSG(_renderer != nullptr, "Renderer could not be created! SDL_Error: " + std::string(SDL_GetError()));

    // Create texture that will be used to display our framebuffer
    _texture = SDL_CreateTexture(
        _renderer,
        SDL_PIXELFORMAT_ARGB8888,  // Ensure this matches your framebuffer format
        SDL_TEXTUREACCESS_STREAMING,
        settings.width,
        settings.height
    );
    AURA_ASSERT_MSG(_renderer != nullptr, "Texture could not be created! SDL_Error: " + std::string(SDL_GetError()));
}

/**
 * Destructor - Vector memory is automatically freed
 */
CpuFrameBufferManager::~CpuFrameBufferManager()
{
    if (_texture != nullptr) {
        SDL_DestroyTexture(_texture);
        _texture = nullptr;
    }

    if (_renderer != nullptr) {
        SDL_DestroyRenderer(_renderer);
        _renderer = nullptr;
    }
}

/**
 * Clears the framebuffer to a specific color
 * @param color 32-bit color value (0xRRGGBB format, alpha is ignored)
 */
void CpuFrameBufferManager::clear(uint32_t color)
{
    uint32_t* framebufferPtr = framebuffer.data();
    size_t pixelCount = settings.width * settings.height;

    // Optimization for solid black (common case)
    if (color == 0) {
        std::memset(framebufferPtr, 0, pixelCount * sizeof(uint32_t));
    }
    // Optimization for solid white (another common case)
    else if (color == 0xFFFFFFFF) {
        std::memset(framebufferPtr, 0xFF, pixelCount * sizeof(uint32_t));
    }
    // Process framebuffer in chunks for better cache utilization
    else {
        // Process in blocks of 64 pixels (better aligned for cache lines and SIMD)
        size_t i = 0;
        for (; i + 63 < pixelCount; i += 64) {
            for (size_t j = 0; j < 64; ++j) {
                framebufferPtr[i + j] = color;
            }
        }

        // Handle remaining pixels
        for (; i < pixelCount; i++) {
            framebufferPtr[i] = color;
        }
    }

    // Reset depth buffer if enabled
    if (settings.useDepthBuffer) {
        std::fill(depthBuffer.begin(), depthBuffer.end(), 1.0f);
    }
}

/**
 * Renders the framebuffer to the screen using SDL
 * @param renderer SDL renderer to use
 * @param texture SDL texture to update with framebuffer content
 *
 * Process:
 * 1. Copy framebuffer data to SDL texture
 * 2. Clear the renderer
 * 3. Copy texture to renderer
 * 4. Present the renderer (display the result)
 */
void CpuFrameBufferManager::renderFramebuffer() {
    SDL_UpdateTexture(_texture, nullptr, framebuffer.data(), settings.width * sizeof(uint32_t));
    SDL_RenderClear(_renderer);
    SDL_RenderCopy(_renderer, _texture, nullptr, nullptr);
    SDL_RenderPresent(_renderer);
}

/**
 * Resizes the framebuffer to new dimensions
 * @param width New width in pixels
 * @param height New height in pixels
 */
void CpuFrameBufferManager::resizeFramebuffer(int width, int height) {
    // Validate input dimensions
    if (width <= 0 || height <= 0) {
        AURA_ERROR << "Error: Invalid framebuffer dimensions (" << width << "x" << height << ")";
        return;
    }

    // Check if resize is actually needed
    if (width == settings.width && height == settings.height) {
        return;
    }

    // Clean up existing texture
    if (_texture != nullptr) {
        SDL_DestroyTexture(_texture);
    }

    // Create new texture with new dimensions
    _texture = SDL_CreateTexture(
        _renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        width, height
    );

    if (_texture == nullptr) {
        throw aura3d::AuraException("Texture could not be created! SDL_Error: " + std::string(SDL_GetError()));
    }

    // Reserve capacity to avoid multiple reallocations
    AlignedVector<uint32_t> newFramebuffer;
    newFramebuffer.reserve(width * height);
    newFramebuffer.resize(width * height, 0);

    AlignedVector<float> newDepthBuffer;
    if (settings.useDepthBuffer) {
        newDepthBuffer.reserve(width * height);
        newDepthBuffer.resize(width * height, 1.0f);
    }

    // Update settings
    settings.width = width;
    settings.height = height;

    // Swap buffers (safer than move)
    framebuffer.swap(newFramebuffer);

    if (settings.useDepthBuffer) {
        depthBuffer.swap(newDepthBuffer);
    }

    // Log the resize operation
    AURA_DEBUG << "Framebuffer resized to " << width << "x" << height;
}

/**
 * Checks if coordinates are within the framebuffer bounds
 * @param x X-coordinate to check
 * @param y Y-coordinate to check
 * @return true if coordinates are valid, false otherwise
 */
bool CpuFrameBufferManager::isInsideBounds(int x, int y) const {
    return x >= 0 && x < settings.width && y >= 0 && y < settings.height;
}

/**
 * Sets a pixel color at the specified coordinates
 * @param x X-coordinate
 * @param y Y-coordinate
 * @param color 32-bit color value (0xRRGGBB format)
 */
void CpuFrameBufferManager::setPixel(int x, int y, uint32_t color) {
    if (isInsideBounds(x, y)) {
        framebuffer[y * settings.width + x] = color;
    }
}

/**
 * Sets a pixel color with depth testing
 * @param x X-coordinate
 * @param y Y-coordinate
 * @param z Depth value (smaller values are closer to camera)
 * @param color 32-bit color value (0xRRGGBB format)
 */
void CpuFrameBufferManager::setPixelWithDepth(int x, int y, float z, uint32_t color) {
    if (!isInsideBounds(x, y)) return;
    const int index = y * settings.width + x;
    // Only update pixel if it's closer than the existing depth
    // or if depth testing is disabled
    if (!settings.useDepthBuffer || z < depthBuffer[index]) {
        framebuffer[index] = color;
        if (settings.useDepthBuffer) {
            depthBuffer[index] = z; // Update depth buffer
        }
    }
}

/**
 * Gets the color of a pixel at the specified coordinates
 * @param x X-coordinate
 * @param y Y-coordinate
 * @return 32-bit color value, or 0 if coordinates are invalid
 */
uint32_t CpuFrameBufferManager::getPixel(int x, int y) const {
    if (isInsideBounds(x, y)) {
        return framebuffer[y * settings.width + x];
    }
    return 0;
}

/**
 * Draws a line using Bresenham's algorithm
 * @param x0, y0 Starting point coordinates
 * @param x1, y1 Ending point coordinates
 * @param color Line color
 *
 * This algorithm uses integer-only arithmetic for speed.
 * It works by determining which pixels to color based on the error accumulation.
 */
void CpuFrameBufferManager::drawLine(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    while (true) {
        setPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;

        e2 = 2 * err;
        // Update x if we're moving horizontally
        if (e2 >= dy) { err += dy; x0 += sx; }
        // Update y if we're moving vertically
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/**
 * Helper function for anti-aliased line drawing
 * @param x, y Coordinates
 * @param intensity Alpha value (0.0 to 1.0)
 * @param color Line color
 */
void CpuFrameBufferManager::plotPixel(int x, int y, float intensity, uint32_t color)
{
    if (!isInsideBounds(x, y)) return;
    // Get existing color and blend with new color
    uint32_t bg = getPixel(x, y);
    uint32_t blended = blendColors(bg, color, intensity);
    setPixel(x, y, blended);
}

/**
 * Draws an anti-aliased line using Xiaolin Wu's algorithm
 * @param x0, y0 Starting point coordinates
 * @param x1, y1 Ending point coordinates
 * @param color Line color
 *
 * This algorithm creates smoother lines by using alpha blending on
 * edge pixels, resulting in reduced jaggedness compared to Bresenham.
 */
void CpuFrameBufferManager::drawAALine(int x0, int y0, int x1, int y1, uint32_t color) {
    // Ensure line is always drawn from left to right
    bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);
    if (steep) {
        // If line is steep (more vertical than horizontal), swap x and y
        std::swap(x0, y0);
        std::swap(x1, y1);
    }
    if (x0 > x1) {
        // Always draw from left to right
        std::swap(x0, x1);
        std::swap(y0, y1);
    }

    int dx = x1 - x0;
    int dy = y1 - y0;
    float gradient = (dx == 0) ? 1.0f : static_cast<float>(dy) / dx;

    // Handle first endpoint
    int xend = x0;
    float yend = y0 + gradient * (xend - x0);
    float xgap = 1.0f - std::fmod(x0 + 0.5f, 1.0f);
    int xpxl1 = xend;
    int ypxl1 = static_cast<int>(yend);

    // Draw first endpoint pixels with partial coverage
    if (steep) {
        plotPixel(ypxl1, xpxl1, (1.0f - std::fmod(yend, 1.0f)) * xgap, color);
        plotPixel(ypxl1 + 1, xpxl1, std::fmod(yend, 1.0f) * xgap, color);
    } else {
        plotPixel(xpxl1, ypxl1, (1.0f - std::fmod(yend, 1.0f)) * xgap, color);
        plotPixel(xpxl1, ypxl1 + 1, std::fmod(yend, 1.0f) * xgap, color);
    }

    float intery = yend + gradient; // First y-intersection for the main loop

    // Handle second endpoint
    xend = x1;
    yend = y1 + gradient * (xend - x1);
    xgap = std::fmod(x1 + 0.5f, 1.0f);
    int xpxl2 = xend;
    int ypxl2 = static_cast<int>(yend);

    // Draw second endpoint pixels with partial coverage
    if (steep) {
        plotPixel(ypxl2, xpxl2, (1.0f - std::fmod(yend, 1.0f)) * xgap, color);
        plotPixel(ypxl2 + 1, xpxl2, std::fmod(yend, 1.0f) * xgap, color);
    } else {
        plotPixel(xpxl2, ypxl2, (1.0f - std::fmod(yend, 1.0f)) * xgap, color);
        plotPixel(xpxl2, ypxl2 + 1, std::fmod(yend, 1.0f) * xgap, color);
    }

    // Main loop - draw the interior of the line
    if (steep) {
        for (int x = xpxl1 + 1; x < xpxl2; x++) {
            // For steep lines, y and x coordinates are swapped
            plotPixel(static_cast<int>(intery), x, 1.0f - std::fmod(intery, 1.0f), color);
            plotPixel(static_cast<int>(intery) + 1, x, std::fmod(intery, 1.0f), color);
            intery += gradient;
        }
    } else {
        for (int x = xpxl1 + 1; x < xpxl2; x++) {
            plotPixel(x, static_cast<int>(intery), 1.0f - std::fmod(intery, 1.0f), color);
            plotPixel(x, static_cast<int>(intery) + 1, std::fmod(intery, 1.0f), color);
            intery += gradient;
        }
    }
}

/**
 * Calculates the edge function value for barycentric coordinates
 * @param ax, ay First point of edge
 * @param bx, by Second point of edge
 * @param px, py Point to test
 * @return Signed distance value - positive if point is on the right side of the edge
 *
 * The edge function is a fundamental part of barycentric coordinate calculation.
 * It determines which side of a line a point is on and by how much.
 */
float CpuFrameBufferManager::edgeFunction(float ax, float ay, float bx, float by, float px, float py) const {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

/**
 * Draws a filled triangle using a scanline approach
 * @param x0, y0 First vertex coordinates
 * @param x1, y1 Second vertex coordinates
 * @param x2, y2 Third vertex coordinates
 * @param color Triangle color
 *
 * This implementation uses a top-down scanline approach, dividing the
 * triangle into top and bottom halves for efficient rasterization.
 */
void CpuFrameBufferManager::drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color) {
    // Sort vertices by y-coordinate (y0 <= y1 <= y2)
    // This simplifies the scanline algorithm by ensuring consistent ordering
    if (y0 > y1) { std::swap(x0, x1); std::swap(y0, y1); }
    if (y1 > y2) { std::swap(x1, x2); std::swap(y1, y2); }
    if (y0 > y1) { std::swap(x0, x1); std::swap(y0, y1); }

    // Compute bounding box with clipping to screen bounds
    int minY = std::max(0, y0);
    int maxY = std::min(settings.height - 1, y2);

    // Skip if triangle is completely off-screen
    if (minY > maxY) return;

    // Calculate edge slopes for interpolation
    float dx01 = x1 - x0, dy01 = y1 - y0;
    float dx12 = x2 - x1, dy12 = y2 - y1;
    float dx20 = x0 - x2, dy20 = y0 - y2;

    // Skip degenerate triangles (lines or points)
    if ((y1 == y0 && y2 == y1) || (x1 == x0 && x2 == x1)) return;

    // Calculate edge slopes for x-coordinate interpolation
    float slope01 = (dy01 != 0) ? dx01 / dy01 : 0;
    float slope12 = (dy12 != 0) ? dx12 / dy12 : 0;
    float slope20 = (dy20 != 0) ? dx20 / dy20 : 0;

    // For each scanline from top to bottom of the triangle
    for (int y = minY; y <= maxY; y++) {
        // Determine if we're in the top or bottom half of the triangle
        bool isTopHalf = y < y1;

        // Calculate x-coordinates where scanline intersects edges
        float leftX, rightX;

        if (isTopHalf) {
            // Top half: Use edges 0-1 and 0-2
            if (dy01 != 0) leftX = x0 + slope01 * (y - y0);
            else leftX = x0;

            if (dy20 != 0) rightX = x2 + slope20 * (y - y2);
            else rightX = x2;
        } else {
            // Bottom half: Use edges 1-2 and 0-2
            if (dy12 != 0) leftX = x1 + slope12 * (y - y1);
            else leftX = x1;

            if (dy20 != 0) rightX = x2 + slope20 * (y - y2);
            else rightX = x2;
        }

        // Make sure left <= right for consistent drawing
        if (leftX > rightX) std::swap(leftX, rightX);

        // Convert to integer and clip to screen boundaries
        int startX = std::max(0, static_cast<int>(leftX + 0.5f));
        int endX = std::min(settings.width - 1, static_cast<int>(rightX + 0.5f));

        // Draw horizontal line for this scanline
        for (int x = startX; x <= endX; x++) {
            setPixel(x, y, color);
        }
    }
}

/**
 * Interpolates attributes (texture coordinates, depth) across a triangle
 * @param v0, v1, v2 The three vertices of the triangle
 * @param x, y The point at which to interpolate
 * @param u, v, w Output parameters for interpolated texture coordinates and depth
 *
 * This implements perspective-correct interpolation for 3D rendering.
 * Without this correction, textures would appear distorted in 3D space.
 */
void CpuFrameBufferManager::interpolateAttributes(const Vertex& v0, const Vertex& v1, const Vertex& v2,
                                                  float x, float y, float& u, float& v, float& w) const {
    // Calculate barycentric coordinates
    float area = edgeFunction(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
    float w0 = edgeFunction(v1.x, v1.y, v2.x, v2.y, x, y) / area;
    float w1 = edgeFunction(v2.x, v2.y, v0.x, v0.y, x, y) / area;
    float w2 = edgeFunction(v0.x, v0.y, v1.x, v1.y, x, y) / area;

    // Interpolate depth (Z) for depth testing
    // The division by Z before interpolation and multiplication after
    // is what makes this perspective-correct
    w = 1.0f / (w0 * (1.0f / v0.z) + w1 * (1.0f / v1.z) + w2 * (1.0f / v2.z));

    // Perspective-correct texture coordinate interpolation
    u = w * (w0 * (v0.u / v0.z) + w1 * (v1.u / v1.z) + w2 * (v2.u / v2.z));
    v = w * (w0 * (v0.v / v0.z) + w1 * (v1.v / v1.z) + w2 * (v2.v / v2.z));
}

/**
 * Draws a textured triangle with perspective correction
 * @param v0, v1, v2 The three vertices with position, texture coords, and color
 * @param texture The texture to sample from
 *
 * This function rasterizes a triangle and samples a texture for each pixel,
 * with proper perspective correction for 3D rendering.
 */
void CpuFrameBufferManager::drawTriangleTextured(const Vertex& v0, const Vertex& v1, const Vertex& v2, const Texture& texture) {
    // Compute bounding box with clipping to screen bounds
    int minX = std::max(0, static_cast<int>(std::min({v0.x, v1.x, v2.x})));
    int minY = std::max(0, static_cast<int>(std::min({v0.y, v1.y, v2.y})));
    int maxX = std::min(settings.width - 1, static_cast<int>(std::max({v0.x, v1.x, v2.x})));
    int maxY = std::min(settings.height - 1, static_cast<int>(std::max({v0.y, v1.y, v2.y})));

    // Early rejection for off-screen triangles
    if (maxX < minX || maxY < minY) return;

    // For each pixel in the triangle's bounding box
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            // Calculate barycentric coordinates to determine if pixel is inside triangle
            float area = edgeFunction(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
            float w0 = edgeFunction(v1.x, v1.y, v2.x, v2.y, x, y) / area;
            float w1 = edgeFunction(v2.x, v2.y, v0.x, v0.y, x, y) / area;
            float w2 = edgeFunction(v0.x, v0.y, v1.x, v1.y, x, y) / area;

            // Check if point is inside triangle
            if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                float u, v, w;
                // Interpolate texture coordinates and depth
                interpolateAttributes(v0, v1, v2, x, y, u, v, w);

                // Sample texture at the interpolated coordinates
                uint32_t color = texture.sample(u, v);

                // Set pixel with optional depth testing
                if (settings.useDepthBuffer) {
                    setPixelWithDepth(x, y, w, color);
                } else {
                    setPixel(x, y, color);
                }
            }
        }
    }
}

/**
 * Draws a triangle with a custom fragment shader
 * @param v0, v1, v2 The three vertices
 * @param fragmentShader Function that computes color for each pixel
 *
 * This allows for custom effects by providing a function that determines
 * pixel color based on interpolated attributes.
 */
void CpuFrameBufferManager::drawTriangleWithShader(const Vertex& v0,
                                                   const Vertex& v1,
                                                   const Vertex& v2,
                                                   std::function<uint32_t(float u, float v, float w)> fragmentShader)
{
    // Compute bounding box
    int minX = std::max(0, static_cast<int>(std::min({v0.x, v1.x, v2.x})));
    int minY = std::max(0, static_cast<int>(std::min({v0.y, v1.y, v2.y})));
    int maxX = std::min(settings.width - 1, static_cast<int>(std::max({v0.x, v1.x, v2.x})));
    int maxY = std::min(settings.height - 1, static_cast<int>(std::max({v0.y, v1.y, v2.y})));

    // Early rejection
    if (maxX < minX || maxY < minY) return;

    // For each pixel in the triangle's bounding box
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            // Calculate barycentric coordinates
            float area = edgeFunction(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
            float w0 = edgeFunction(v1.x, v1.y, v2.x, v2.y, x, y) / area;
            float w1 = edgeFunction(v2.x, v2.y, v0.x, v0.y, x, y) / area;
            float w2 = edgeFunction(v0.x, v0.y, v1.x, v1.y, x, y) / area;

            // Check if point is inside triangle
            if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                float u, v, w;
                // Interpolate attributes
                interpolateAttributes(v0, v1, v2, x, y, u, v, w);

                // Execute fragment shader to determine pixel color
                uint32_t color = fragmentShader(u, v, w);

                // Set pixel with optional depth testing
                if (settings.useDepthBuffer) {
                    setPixelWithDepth(x, y, w, color);
                } else {
                    setPixel(x, y, color);
                }
            }
        }
    }
}

/**
 * Draws a circle using the Midpoint Circle Algorithm
 * @param centerX, centerY Center coordinates of the circle
 * @param radius Circle radius in pixels
 * @param color Circle color
 *
 * This draws the outline of a circle efficiently by exploiting 8-way symmetry.
 */
void CpuFrameBufferManager::drawCircle(int centerX, int centerY, int radius, uint32_t color) {
    int x = radius;
    int y = 0;
    int err = 0;

    // The algorithm takes advantage of circle symmetry
    // by calculating points in one octant and mirroring them
    while (x >= y) {
        // Draw 8 symmetric points
        setPixel(centerX + x, centerY + y, color);
        setPixel(centerX + y, centerY + x, color);
        setPixel(centerX - y, centerY + x, color);
        setPixel(centerX - x, centerY + y, color);
        setPixel(centerX - x, centerY - y, color);
        setPixel(centerX - y, centerY - x, color);
        setPixel(centerX + y, centerY - x, color);
        setPixel(centerX + x, centerY - y, color);

        // Update using midpoint decision criterion
        if (err <= 0) {
            y += 1;
            err += 2 * y + 1;
        }

        if (err > 0) {
            x -= 1;
            err -= 2 * x + 1;
        }
    }
}

/**
 * Draws a filled rectangle
 * @param x, y Top-left corner coordinates
 * @param width, height Dimensions of the rectangle
 * @param color Rectangle color
 */
void CpuFrameBufferManager::drawFillRect(int x, int y, int width, int height, uint32_t color) {
    // Clip rectangle to screen bounds
    int x1 = std::max(0, x);
    int y1 = std::max(0, y);
    int x2 = std::min(settings.width - 1, x + width - 1);
    int y2 = std::min(settings.height - 1, y + height - 1);

    // Skip if rectangle is completely off-screen
    if (x2 < x1 || y2 < y1) return;

    // Draw rectangle row by row - direct buffer access for speed
    for (int cy = y1; cy <= y2; ++cy) {
        for (int cx = x1; cx <= x2; ++cx) {
            framebuffer[cy * settings.width + cx] = color;
        }
    }
}

void CpuFrameBufferManager::drawRect(int x, int y, int width, int height, uint32_t color)
{
    int xmin = std::max(0, x);;
    int xmax = xmin + std::min(settings.width-1, width);
    int ymin = std::max(0, y);
    int ymax = ymin + std::min(settings.height-1, height);

    drawLine(xmin, ymin, xmax, ymin, color);
    drawLine(xmax, ymin, xmax, ymax, color);
    drawLine(xmax, ymax, xmin, ymax, color);
    drawLine(xmin, ymax, xmin, ymin, color);
}

/**
 * Draws a rectangle with rounded corners
 * @param x, y Top-left corner coordinates
 * @param width, height Dimensions of the rectangle
 * @param radius Corner radius in pixels
 * @param color Rectangle color
 *
 * This is implemented by drawing a main rectangle and four corner arcs.
 */
void CpuFrameBufferManager::drawRoundedRect(int x, int y, int width, int height, int radius, uint32_t color) {
    // Center rectangle (full width minus corners, full height)
    drawRect(x + radius, y, width - 2 * radius, height, color);

    // Left and right rectangles (to fill the sides)
    drawRect(x, y + radius, radius, height - 2 * radius, color);
    drawRect(x + width - radius, y + radius, radius, height - 2 * radius, color);

    // Draw four corner arcs as partial circles
    auto drawArc = [this, color](int cx, int cy, int r, int quadrant) {
        int x = r;
        int y = 0;
        int err = 0;

        while (x >= y) {
            // Draw only the points in the specified quadrant
            switch (quadrant) {
            case 0: // Top-right
                setPixel(cx + x, cy - y, color);
                setPixel(cx + y, cy - x, color);
                break;
            case 1: // Top-left
                setPixel(cx - x, cy - y, color);
                setPixel(cx - y, cy - x, color);
                break;
            case 2: // Bottom-left
                setPixel(cx - x, cy + y, color);
                setPixel(cx - y, cy + x, color);
                break;
            case 3: // Bottom-right
                setPixel(cx + x, cy + y, color);
                setPixel(cx + y, cy + x, color);
                break;
            }

            // Midpoint circle algorithm steps
            if (err <= 0) {
                y += 1;
                err += 2 * y + 1;
            }

            if (err > 0) {
                x -= 1;
                err -= 2 * x + 1;
            }
        }
    };

    // Draw four corners
    drawArc(x + width - radius, y + radius, radius, 0); // Top-right
    drawArc(x + radius, y + radius, radius, 1); // Top-left
    drawArc(x + radius, y + height - radius, radius, 2); // Bottom-left
    drawArc(x + width - radius, y + height - radius, radius, 3); // Bottom-right
}

/**
 * Blends two colors according to an alpha value
 * @param c1 Background color
 * @param c2 Foreground color
 * @param alpha Blend factor (0.0 = all c1, 1.0 = all c2)
 * @return Blended color
 *
 * Colors are in 0xRRGGBB format with no alpha component.
 */
uint32_t CpuFrameBufferManager::blendColors(uint32_t c1, uint32_t c2, float alpha) {
    if (alpha <= 0.0f) return c1;
    if (alpha >= 1.0f) return c2;
    // Extract color components (R, G, B)
    uint8_t r1 = (c1 >> 16) & 0xFF;
    uint8_t g1 = (c1 >> 8) & 0xFF;
    uint8_t b1 = c1 & 0xFF;
    uint8_t r2 = (c2 >> 16) & 0xFF;
    uint8_t g2 = (c2 >> 8) & 0xFF;
    uint8_t b2 = c2 & 0xFF;
    // Linear interpolation for each component
    uint8_t r = static_cast<uint8_t>(r1 * (1.0f - alpha) + r2 * alpha);
    uint8_t g = static_cast<uint8_t>(g1 * (1.0f - alpha) + g2 * alpha);
    uint8_t b = static_cast<uint8_t>(b1 * (1.0f - alpha) + b2 * alpha);
    // Combine components back into a single color value
    return (r << 16) | (g << 8) | b;
}

}  // namespace aura3d
