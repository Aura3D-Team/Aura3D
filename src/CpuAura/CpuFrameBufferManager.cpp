#include "CpuFrameBufferManager.h"

#include <cstring>
#include <cmath>
#include <ink/ink.hpp>

#include "AuraException/AuraException.h"
#include "Utils/AuraUtils.h"


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
    _texture(nullptr),
    _font(GetDefaultBitmapFont())
{
    // Create depth buffer if enabled in settings
    if (settings.useDepthBuffer) {
        depthBuffer.resize(config.width * config.height, 1.0f); // Initialize with max depth
    }

    _renderer = SDL_CreateRenderer(_window, -1, SDL_RENDERER_ACCELERATED);
    INK_ASSERT_MSG(_renderer != nullptr, "Renderer could not be created! SDL_Error: " + std::string(SDL_GetError()));

    // Create texture that will be used to display our framebuffer
    _texture = SDL_CreateTexture(
        _renderer,
        SDL_PIXELFORMAT_ARGB8888,  // Ensure this matches your framebuffer format
        SDL_TEXTUREACCESS_STREAMING,
        settings.width,
        settings.height
        );
    INK_ASSERT_MSG(_texture != nullptr, "Texture could not be created! SDL_Error: " + std::string(SDL_GetError()));
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
void CpuFrameBufferManager::clear(u32 color)
{
    u32* framebufferPtr = framebuffer.data();
    size_t pixelCount = settings.width * settings.height;

    // Optimization for solid black (common case)
    if (color == 0) {
        std::memset(framebufferPtr, 0, pixelCount * sizeof(u32));
    }
    // Optimization for solid white (another common case)
    else if (color == 0xFFFFFFFF) {
        std::memset(framebufferPtr, 0xFF, pixelCount * sizeof(u32));
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
void CpuFrameBufferManager::renderFramebuffer()
{
    SDL_UpdateTexture(_texture, nullptr, framebuffer.data(), settings.width * sizeof(u32));
    SDL_RenderClear(_renderer);
    SDL_RenderCopy(_renderer, _texture, nullptr, nullptr);
    SDL_RenderPresent(_renderer);
}

/**
 * Resizes the framebuffer to new dimensions
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
    AlignedVector<u32> newFramebuffer;
    newFramebuffer.reserve(width * height);
    newFramebuffer.resize(width * height, 0);

    AlignedVector<f32> newDepthBuffer;
    if (settings.useDepthBuffer) {
        newDepthBuffer.reserve(width * height);
        newDepthBuffer.resize(width * height, 1.0f);
    }

    // Update settings
    settings.width = width;
    settings.height = height;

    // Swap buffers (safer than move)
    framebuffer = std::move(newFramebuffer);

    if (settings.useDepthBuffer) {
        depthBuffer = std::move(newDepthBuffer);
    }
}

/**
 * Checks if coordinates are within the framebuffer bounds
 * @param x X-coordinate to check
 * @param y Y-coordinate to check
 * @return true if coordinates are valid, false otherwise
 */
bool CpuFrameBufferManager::isInsideBounds(Point p) const
{
    return p.x >= 0 && p.x < settings.width && p.y >= 0 && p.y < settings.height;
}

/**
 * Sets a pixel color at the specified coordinates
 * @param x X-coordinate
 * @param y Y-coordinate
 * @param color 32-bit color value (0xRRGGBB format)
 */
void CpuFrameBufferManager::setPixel(Point p, u32 color)
{
    if (isInsideBounds({p.x, p.y})) {
        framebuffer[p.y * settings.width + p.x] = color;
    }
}

/**
 * Sets a pixel color with depth testing
 * @param x X-coordinate
 * @param y Y-coordinate
 * @param z Depth value (smaller values are closer to camera)
 * @param color 32-bit color value (0xRRGGBB format)
 */
void CpuFrameBufferManager::setPixelWithDepth(Point p, f32 z, u32 color)
{
    if (!isInsideBounds({p.x, p.y})) return;
    const int index = p.y * settings.width + p.x;

    framebuffer[index] = color;

    if (settings.useDepthBuffer) {
        depthBuffer[index] = z; // Update depth buffer
    }
}

/**
 * Helper function for anti-aliased line drawing
 * @param x, y Coordinates
 * @param intensity Alpha value (0.0 to 1.0)
 * @param color Line color
 */
void CpuFrameBufferManager::plotPixel(Point p, f32 intensity, u32 color)
{
    if (!isInsideBounds({p.x, p.y})) return;
    // Get existing color and blend with new color
    u32 bg = getPixel({p.x, p.y});
    u32 blended = blendColors(bg, color, intensity);
    setPixel({p.x, p.y}, blended);
}

/**
 * Gets the color of a pixel at the specified coordinates
 * @param x X-coordinate
 * @param y Y-coordinate
 * @return 32-bit color value, or 0 if coordinates are invalid
 */
u32 CpuFrameBufferManager::getPixel(Point p) const
{
    if (isInsideBounds({p.x, p.y})) {
        return framebuffer[p.y * settings.width + p.x];
    }
    return 0;
}

f32 CpuFrameBufferManager::getDepthPixel(Point p) const
{
    if (isInsideBounds({p.x, p.y})) {
        return depthBuffer[p.y * settings.width + p.x];
    }
    return 1.0f;
}

/**
 * Draws a line using Bresenham's algorithm
 * @param p0 Starting point coordinates
 * @param p1 Ending point coordinates
 * @param color Line color
 *
 * This algorithm uses integer-only arithmetic for speed.
 * It works by determining which pixels to color based on the error accumulation.
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
        // This case should ideally not be reached if all enum values are handled.
        // A good compiler with warnings enabled might flag unhandled enum values.
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

    // Ensure colors vector has at least enough entries for all line segments
    if (colors.size() < (closed ? points.size() : points.size() - 1)) {
        // Fall back to single color if not enough colors provided
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
    if (points.size() < 3) return; // Need at least 3 points for a polygon

    // Find the bounding box of the polygon
    int minY = getHeight();
    int maxY = 0;

    for (const auto& p : points) {
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }

    // Clip to screen bounds
    minY = std::max(minY, 0);
    maxY = std::min(maxY, getHeight() - 1);

    // Allocate array for edge intersections
    std::vector<int> intersections;
    intersections.reserve(points.size());

    // Scanline algorithm
    for (int y = minY; y <= maxY; y++) {
        intersections.clear();

        // Find intersections with all edges
        for (size_t i = 0; i < points.size(); i++) {
            // Get edge vertices
            const Point& p1 = points[i];
            const Point& p2 = points[(i + 1) % points.size()];

            // Skip horizontal edges and vertices outside current scanline
            if ((p1.y == p2.y) || (p1.y > y && p2.y > y) || (p1.y < y && p2.y < y))
                continue;

            // Calculate intersection
            float t = static_cast<float>(y - p1.y) / static_cast<float>(p2.y - p1.y);
            int x = p1.x + static_cast<int>(t * (p2.x - p1.x));

            intersections.push_back(x);
        }

        // Sort intersections from left to right
        std::sort(intersections.begin(), intersections.end());

        // Fill between pairs of intersections
        for (size_t i = 0; i < intersections.size(); i += 2) {
            if (i + 1 >= intersections.size()) break;

            int startX = std::max(intersections[i], 0);
            int endX = std::min(intersections[i + 1], getWidth() - 1);

            // Draw horizontal line between intersections
            for (int x = startX; x <= endX; x++) {
                setPixel(Point(x, y), color);
            }
        }
    }
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

void CpuFrameBufferManager::drawText(const std::string& text, Point p, u32 color, f32 fontSize) {
    int cursorX = p.x;
    // Calculate scaled dimensions
    int scaledWidth = std::floor(_font.charWidth * fontSize);
    int scaledHeight = std::floor(_font.charHeight * fontSize);
    int scaledSpacing = std::ceil(_font.charSpacing * fontSize);

    for (char c : text) {
        // Handle newline
        if (c == '\n') {
            cursorX = p.x;
            p.y += scaledHeight + scaledSpacing;
            continue;
        }

        // Replace non-ASCII with ?
        if (c < 0 || c > 127) c = '?';

        // Skip if completely out of bounds
        if (cursorX >= settings.width || p.y >= settings.height || cursorX + scaledWidth <= 0 || p.y + scaledHeight <= 0) {
            cursorX += scaledWidth + scaledSpacing;
            continue;
        }

        // Fix cast syntax
        const auto& charData = _font.data[static_cast<unsigned char>(c)];

        // Draw character with scaling
        for (int row = 0; row < _font.charHeight; row++) {
            u8 rowBits = charData[row];

            // Scale each row vertically
            for (int scaleY = 0; scaleY < fontSize; scaleY++) {
                int pixelY = p.y + (int)(row * fontSize) + scaleY;
                if (pixelY < 0 || pixelY >= settings.height) continue;

                // Process each bit in the row
                for (int col = 0; col < _font.charWidth; col++) {
                    bool isPixelOn = (rowBits & (1 << (_font.charWidth - 1 - col))) != 0;
                    if (isPixelOn)
                    {
                        // Scale each pixel horizontally
                        for (int scaleX = 0; scaleX < fontSize; scaleX++) {
                            int pixelX = cursorX + (int)(col * fontSize) + scaleX;
                            if (pixelX < 0 || pixelX >= settings.width) continue;

                            setPixel({ pixelX, pixelY }, color);
                        }
                    }
                }
            }
        }

        // Move cursor to next character position
        cursorX += scaledWidth + scaledSpacing;
    }
}

int CpuFrameBufferManager::getTextWidth(const std::string& text, f32 fontSize)
{
    i32 scaledWidth = std::floor(_font.charWidth * fontSize);
    i32 scaledSpacing = std::ceil(_font.charSpacing * fontSize);

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

int CpuFrameBufferManager::getTextHeight(const std::string& text, f32 fontSize)
{
    int scaledHeight = std::floor(_font.charHeight * fontSize);
    int lines = 1;

    for (char c : text) {
        if (c == '\n') {
            lines++;
        }
    }

    return lines * (scaledHeight + 1) - 1;
}

}  // namespace aura3d
