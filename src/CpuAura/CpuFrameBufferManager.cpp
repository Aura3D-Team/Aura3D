#include "CpuFrameBufferManager.h"

#include <cstring>
#include <cmath>
#include <algorithm> // For std::fill and std::transform
#include <vector>    // For the temporary pixel buffer
#include <ink/ink.hpp>

#include "AuraException/AuraException.h"

namespace aura3d {

/**
 * Constructor - Initializes the framebuffer using the Pixel struct
 * @param config Configuration settings for width, height, and depth buffer usage
 */
CpuFrameBufferManager::CpuFrameBufferManager(SDL_Window* window, Config config) :
    settings(config),
    // Initialize framebuffer with Pixel objects: black color (0) and max depth (1.0f)
    framebuffer(config.width* config.height, Pixel{0, 1.0f}),
    _window(window),
    _renderer(nullptr),
    _texture(nullptr),
    _font(GetDefaultBitmapFont())
{
    // NOTE: The separate depthBuffer has been removed. Depth is now part of the Pixel struct.

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
 * Renders the framebuffer to the screen using SDL
 *
 * Process:
 * 1. Extract just the RGB color data into a temporary, contiguous buffer.
 * 2. Copy that temporary buffer to the SDL texture.
 * 3. Clear the renderer.
 * 4. Copy texture to renderer.
 * 5. Present the renderer (display the result).
 */
void CpuFrameBufferManager::renderFramebuffer()
{
    // SDL_UpdateTexture expects a contiguous array of u32 colors, but our
    // framebuffer is an array of Pixel structs (u32 rgb, f32 z).
    // We must first copy the color data into a temporary buffer.
    std::vector<u32> pixel_data(settings.width * settings.height);
    std::transform(framebuffer.begin(), framebuffer.end(), pixel_data.begin(),
                   [](const Pixel& p) { return p.rgb; });

    SDL_UpdateTexture(_texture, nullptr, pixel_data.data(), settings.width * sizeof(u32));
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

    // Update settings first
    settings.width = width;
    settings.height = height;

    // Resize the framebuffer vector, initializing new pixels to black with max depth
    framebuffer.assign(width * height, Pixel{0, 1.0f});
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

        if (c < 0 || c > 127) c = '?';

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

}  // namespace aura3d
