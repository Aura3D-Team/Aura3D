#include "CpuFrameBufferManager.h"
#include <cstring>
#include <vector>

namespace aura3d {

CpuFrameBufferManager::CpuFrameBufferManager(Config config, aura3d::AuraThreadPool& pool) :
    settings(config),
    framebuffer(config.width * config.height, 0),
    pendingTasks(0),
    threadPool(pool)
{
    if (settings.useDepthBuffer) {
        depthBuffer.resize(config.width * config.height, 1.0f);
    }
}

CpuFrameBufferManager::~CpuFrameBufferManager()
{

}

void CpuFrameBufferManager::clear(uint32_t color)
{
    uint32_t* framebufferPtr = framebuffer.data();
    size_t pixelCount = settings.width * settings.height;

    // Process framebuffer in chunks of 50 pixels
    size_t i = 0;
    for (; i + 49 < pixelCount; i += 50) {
        framebufferPtr[i]     = color;
        framebufferPtr[i + 1] = color;
        framebufferPtr[i + 2] = color;
        framebufferPtr[i + 3] = color;
        framebufferPtr[i + 4] = color;
        framebufferPtr[i + 5] = color;
        framebufferPtr[i + 6] = color;
        framebufferPtr[i + 7] = color;
        framebufferPtr[i + 8] = color;
        framebufferPtr[i + 9] = color;
        framebufferPtr[i + 10] = color;
        framebufferPtr[i + 11] = color;
        framebufferPtr[i + 12] = color;
        framebufferPtr[i + 13] = color;
        framebufferPtr[i + 14] = color;
        framebufferPtr[i + 15] = color;
        framebufferPtr[i + 16] = color;
        framebufferPtr[i + 17] = color;
        framebufferPtr[i + 18] = color;
        framebufferPtr[i + 19] = color;
        framebufferPtr[i + 20] = color;
        framebufferPtr[i + 21] = color;
        framebufferPtr[i + 22] = color;
        framebufferPtr[i + 23] = color;
        framebufferPtr[i + 24] = color;
        framebufferPtr[i + 25] = color;
        framebufferPtr[i + 26] = color;
        framebufferPtr[i + 27] = color;
        framebufferPtr[i + 28] = color;
        framebufferPtr[i + 29] = color;
        framebufferPtr[i + 30] = color;
        framebufferPtr[i + 31] = color;
        framebufferPtr[i + 32] = color;
        framebufferPtr[i + 33] = color;
        framebufferPtr[i + 34] = color;
        framebufferPtr[i + 35] = color;
        framebufferPtr[i + 36] = color;
        framebufferPtr[i + 37] = color;
        framebufferPtr[i + 38] = color;
        framebufferPtr[i + 39] = color;
        framebufferPtr[i + 40] = color;
        framebufferPtr[i + 41] = color;
        framebufferPtr[i + 42] = color;
        framebufferPtr[i + 43] = color;
        framebufferPtr[i + 44] = color;
        framebufferPtr[i + 45] = color;
        framebufferPtr[i + 46] = color;
        framebufferPtr[i + 47] = color;
        framebufferPtr[i + 48] = color;
        framebufferPtr[i + 49] = color;
    }

    // Handle remaining pixels
    for (; i < pixelCount; i++) {
        framebufferPtr[i] = color;
    }

    // Process depth buffer in chunks of 50 pixels
    if (settings.useDepthBuffer) {
        float* depthBufferPtr = depthBuffer.data();
        i = 0;
        for (; i + 49 < pixelCount; i += 50) {
            depthBufferPtr[i]     = 1.0f;
            depthBufferPtr[i + 1] = 1.0f;
            depthBufferPtr[i + 2] = 1.0f;
            depthBufferPtr[i + 3] = 1.0f;
            depthBufferPtr[i + 4] = 1.0f;
            depthBufferPtr[i + 5] = 1.0f;
            depthBufferPtr[i + 6] = 1.0f;
            depthBufferPtr[i + 7] = 1.0f;
            depthBufferPtr[i + 8] = 1.0f;
            depthBufferPtr[i + 9] = 1.0f;
            depthBufferPtr[i + 10] = 1.0f;
            depthBufferPtr[i + 11] = 1.0f;
            depthBufferPtr[i + 12] = 1.0f;
            depthBufferPtr[i + 13] = 1.0f;
            depthBufferPtr[i + 14] = 1.0f;
            depthBufferPtr[i + 15] = 1.0f;
            depthBufferPtr[i + 16] = 1.0f;
            depthBufferPtr[i + 17] = 1.0f;
            depthBufferPtr[i + 18] = 1.0f;
            depthBufferPtr[i + 19] = 1.0f;
            depthBufferPtr[i + 20] = 1.0f;
            depthBufferPtr[i + 21] = 1.0f;
            depthBufferPtr[i + 22] = 1.0f;
            depthBufferPtr[i + 23] = 1.0f;
            depthBufferPtr[i + 24] = 1.0f;
            depthBufferPtr[i + 25] = 1.0f;
            depthBufferPtr[i + 26] = 1.0f;
            depthBufferPtr[i + 27] = 1.0f;
            depthBufferPtr[i + 28] = 1.0f;
            depthBufferPtr[i + 29] = 1.0f;
            depthBufferPtr[i + 30] = 1.0f;
            depthBufferPtr[i + 31] = 1.0f;
            depthBufferPtr[i + 32] = 1.0f;
            depthBufferPtr[i + 33] = 1.0f;
            depthBufferPtr[i + 34] = 1.0f;
            depthBufferPtr[i + 35] = 1.0f;
            depthBufferPtr[i + 36] = 1.0f;
            depthBufferPtr[i + 37] = 1.0f;
            depthBufferPtr[i + 38] = 1.0f;
            depthBufferPtr[i + 39] = 1.0f;
            depthBufferPtr[i + 40] = 1.0f;
            depthBufferPtr[i + 41] = 1.0f;
            depthBufferPtr[i + 42] = 1.0f;
            depthBufferPtr[i + 43] = 1.0f;
            depthBufferPtr[i + 44] = 1.0f;
            depthBufferPtr[i + 45] = 1.0f;
            depthBufferPtr[i + 46] = 1.0f;
            depthBufferPtr[i + 47] = 1.0f;
            depthBufferPtr[i + 48] = 1.0f;
            depthBufferPtr[i + 49] = 1.0f;
        }

        // Handle remaining depth pixels
        for (; i < pixelCount; i++) {
            depthBufferPtr[i] = 1.0f;
        }
    }
}

void CpuFrameBufferManager::renderFramebuffer(SDL_Renderer* renderer, SDL_Texture* texture) {
    SDL_UpdateTexture(texture, nullptr, framebuffer.data(), settings.width * sizeof(uint32_t));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

bool CpuFrameBufferManager::isInsideBounds(int x, int y) const {
    return x >= 0 && x < settings.width && y >= 0 && y < settings.height;
}

void CpuFrameBufferManager::setPixel(int x, int y, uint32_t color) {
    if (isInsideBounds(x, y)) {
        framebuffer[y * settings.width + x] = color;
    }
}

void CpuFrameBufferManager::setPixelWithDepth(int x, int y, float z, uint32_t color) {
    if (!isInsideBounds(x, y)) return;

    const int index = y * settings.width + x;

    if (!settings.useDepthBuffer || z < depthBuffer[index]) {
        framebuffer[index] = color;

        if (settings.useDepthBuffer) {
            depthBuffer[index] = z;
        }
    }
}

uint32_t CpuFrameBufferManager::getPixel(int x, int y) const {
    if (isInsideBounds(x, y)) {
        return framebuffer[y * settings.width + x];
    }
    return 0;
}

// Draw a Line using Bresenham's Algorithm
void CpuFrameBufferManager::drawLine(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    while (true) {
        setPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// Draw an anti-aliased line using Xiaolin Wu's algorithm
void CpuFrameBufferManager::drawAALine(int x0, int y0, int x1, int y1, uint32_t color) {
    // Function to plot a pixel with blending based on intensity
    auto plotPixel = [this, color](int x, int y, float intensity) {
        if (!isInsideBounds(x, y)) return;

        uint32_t bg = getPixel(x, y);
        uint32_t blended = blendColors(bg, color, intensity);
        setPixel(x, y, blended);
    };

    // Implementation of Xiaolin Wu's line algorithm
    bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);
    if (steep) {
        std::swap(x0, y0);
        std::swap(x1, y1);
    }
    if (x0 > x1) {
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

    if (steep) {
        plotPixel(ypxl1, xpxl1, (1.0f - std::fmod(yend, 1.0f)) * xgap);
        plotPixel(ypxl1 + 1, xpxl1, std::fmod(yend, 1.0f) * xgap);
    } else {
        plotPixel(xpxl1, ypxl1, (1.0f - std::fmod(yend, 1.0f)) * xgap);
        plotPixel(xpxl1, ypxl1 + 1, std::fmod(yend, 1.0f) * xgap);
    }

    float intery = yend + gradient;

    // Handle second endpoint
    xend = x1;
    yend = y1 + gradient * (xend - x1);
    xgap = std::fmod(x1 + 0.5f, 1.0f);
    int xpxl2 = xend;
    int ypxl2 = static_cast<int>(yend);

    if (steep) {
        plotPixel(ypxl2, xpxl2, (1.0f - std::fmod(yend, 1.0f)) * xgap);
        plotPixel(ypxl2 + 1, xpxl2, std::fmod(yend, 1.0f) * xgap);
    } else {
        plotPixel(xpxl2, ypxl2, (1.0f - std::fmod(yend, 1.0f)) * xgap);
        plotPixel(xpxl2, ypxl2 + 1, std::fmod(yend, 1.0f) * xgap);
    }

    // Main loop
    if (steep) {
        for (int x = xpxl1 + 1; x < xpxl2; x++) {
            plotPixel(static_cast<int>(intery), x, 1.0f - std::fmod(intery, 1.0f));
            plotPixel(static_cast<int>(intery) + 1, x, std::fmod(intery, 1.0f));
            intery += gradient;
        }
    } else {
        for (int x = xpxl1 + 1; x < xpxl2; x++) {
            plotPixel(x, static_cast<int>(intery), 1.0f - std::fmod(intery, 1.0f));
            plotPixel(x, static_cast<int>(intery) + 1, std::fmod(intery, 1.0f));
            intery += gradient;
        }
    }
}

// Edge function for barycentric coordinates
float CpuFrameBufferManager::edgeFunction(float ax, float ay, float bx, float by, float px, float py) const {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

// Draw a filled triangle using barycentric coordinates with optimizations
void CpuFrameBufferManager::drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color) {
    // Compute bounding box with clipping to screen bounds
    int minX = std::max(0, std::min({x0, x1, x2}));
    int minY = std::max(0, std::min({y0, y1, y2}));
    int maxX = std::min(settings.width - 1, std::max({x0, x1, x2}));
    int maxY = std::min(settings.height - 1, std::max({y0, y1, y2}));

    // Early rejection for tiny or off-screen triangles
    if (maxX < minX || maxY < minY) return;

    // Precompute edge functions for triangle
    float area = edgeFunction(x0, y0, x1, y1, x2, y2);

    // Skip degenerate triangles
    if (std::abs(area) < 0.1f) return;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            float w0 = edgeFunction(x1, y1, x2, y2, x, y);
            float w1 = edgeFunction(x2, y2, x0, y0, x, y);
            float w2 = edgeFunction(x0, y0, x1, y1, x, y);

            if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                setPixel(x, y, color);
            }
        }
    }
}

// Interpolate attributes for textured triangle
void CpuFrameBufferManager::interpolateAttributes(const Vertex& v0, const Vertex& v1, const Vertex& v2,
                                                  float x, float y, float& u, float& v, float& w) const {
    // Calculate barycentric coordinates
    float area = edgeFunction(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
    float w0 = edgeFunction(v1.x, v1.y, v2.x, v2.y, x, y) / area;
    float w1 = edgeFunction(v2.x, v2.y, v0.x, v0.y, x, y) / area;
    float w2 = edgeFunction(v0.x, v0.y, v1.x, v1.y, x, y) / area;

    // Interpolate depth (Z) for depth testing
    w = 1.0f / (w0 * (1.0f / v0.z) + w1 * (1.0f / v1.z) + w2 * (1.0f / v2.z));

    // Perspective-correct texture coordinate interpolation
    u = w * (w0 * (v0.u / v0.z) + w1 * (v1.u / v1.z) + w2 * (v2.u / v2.z));
    v = w * (w0 * (v0.v / v0.z) + w1 * (v1.v / v1.z) + w2 * (v2.v / v2.z));
}

// Draw textured triangle with perspective correction
void CpuFrameBufferManager::drawTriangleTextured(const Vertex& v0, const Vertex& v1, const Vertex& v2, const Texture& texture) {
    // Compute bounding box
    int minX = std::max(0, static_cast<int>(std::min({v0.x, v1.x, v2.x})));
    int minY = std::max(0, static_cast<int>(std::min({v0.y, v1.y, v2.y})));
    int maxX = std::min(settings.width - 1, static_cast<int>(std::max({v0.x, v1.x, v2.x})));
    int maxY = std::min(settings.height - 1, static_cast<int>(std::max({v0.y, v1.y, v2.y})));

    // Early rejection
    if (maxX < minX || maxY < minY) return;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            float area = edgeFunction(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
            float w0 = edgeFunction(v1.x, v1.y, v2.x, v2.y, x, y) / area;
            float w1 = edgeFunction(v2.x, v2.y, v0.x, v0.y, x, y) / area;
            float w2 = edgeFunction(v0.x, v0.y, v1.x, v1.y, x, y) / area;

            if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                float u, v, w;
                interpolateAttributes(v0, v1, v2, x, y, u, v, w);

                // Sample texture
                uint32_t color = texture.sample(u, v);

                // Blend with vertex colors (simplified for now)
                uint32_t finalColor = color;

                if (settings.useDepthBuffer) {
                    setPixelWithDepth(x, y, w, finalColor);
                } else {
                    setPixel(x, y, finalColor);
                }
            }
        }
    }
}

// Draw triangle with a custom fragment shader function
void CpuFrameBufferManager::drawTriangleWithShader(
    const Vertex& v0, const Vertex& v1, const Vertex& v2,
    std::function<uint32_t(float u, float v, float w)> fragmentShader) {

    // Compute bounding box
    int minX = std::max(0, static_cast<int>(std::min({v0.x, v1.x, v2.x})));
    int minY = std::max(0, static_cast<int>(std::min({v0.y, v1.y, v2.y})));
    int maxX = std::min(settings.width - 1, static_cast<int>(std::max({v0.x, v1.x, v2.x})));
    int maxY = std::min(settings.height - 1, static_cast<int>(std::max({v0.y, v1.y, v2.y})));

    // Early rejection
    if (maxX < minX || maxY < minY) return;
\
    for (int y = minY; y <= maxY; ++y)
    {
        for (int x = minX; x <= maxX; ++x) {
            float area = edgeFunction(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
            float w0 = edgeFunction(v1.x, v1.y, v2.x, v2.y, x, y) / area;
            float w1 = edgeFunction(v2.x, v2.y, v0.x, v0.y, x, y) / area;
            float w2 = edgeFunction(v0.x, v0.y, v1.x, v1.y, x, y) / area;

            if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                float u, v, w;
                interpolateAttributes(v0, v1, v2, x, y, u, v, w);

                // Execute fragment shader
                uint32_t color = fragmentShader(u, v, w);

                if (settings.useDepthBuffer) {
                    setPixelWithDepth(x, y, w, color);
                } else {
                    setPixel(x, y, color);
                }
            }
        }
    }
}

// Draw a circle using Midpoint Circle Algorithm
void CpuFrameBufferManager::drawCircle(int centerX, int centerY, int radius, uint32_t color) {
    int x = radius;
    int y = 0;
    int err = 0;

    while (x >= y) {
        setPixel(centerX + x, centerY + y, color);
        setPixel(centerX + y, centerY + x, color);
        setPixel(centerX - y, centerY + x, color);
        setPixel(centerX - x, centerY + y, color);
        setPixel(centerX - x, centerY - y, color);
        setPixel(centerX - y, centerY - x, color);
        setPixel(centerX + y, centerY - x, color);
        setPixel(centerX + x, centerY - y, color);

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

// Draw a filled rectangle
void CpuFrameBufferManager::drawRect(int x, int y, int width, int height, uint32_t color) {
    // Clip rectangle to screen bounds
    int x1 = std::max(0, x);
    int y1 = std::max(0, y);
    int x2 = std::min(settings.width - 1, x + width - 1);
    int y2 = std::min(settings.height - 1, y + height - 1);

    if (x2 < x1 || y2 < y1) return;

    for (int cy = y1; cy <= y2; ++cy) {
        for (int cx = x1; cx <= x2; ++cx) {
            framebuffer[cy * settings.width + cx] = color;
        }
    }
}

// Draw a rounded rectangle
void CpuFrameBufferManager::drawRoundedRect(int x, int y, int width, int height, int radius, uint32_t color) {
    // Center rectangle
    drawRect(x + radius, y, width - 2 * radius, height, color);

    // Left and right rectangles
    drawRect(x, y + radius, radius, height - 2 * radius, color);
    drawRect(x + width - radius, y + radius, radius, height - 2 * radius, color);

    // Draw four corner arcs
    auto drawArc = [this, color](int cx, int cy, int r, int quadrant) {
        int x = r;
        int y = 0;
        int err = 0;

        while (x >= y) {
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

// Blend two colors with alpha
uint32_t CpuFrameBufferManager::blendColors(uint32_t c1, uint32_t c2, float alpha) {
    if (alpha <= 0.0f) return c1;
    if (alpha >= 1.0f) return c2;

    uint8_t r1 = (c1 >> 16) & 0xFF;
    uint8_t g1 = (c1 >> 8) & 0xFF;
    uint8_t b1 = c1 & 0xFF;

    uint8_t r2 = (c2 >> 16) & 0xFF;
    uint8_t g2 = (c2 >> 8) & 0xFF;
    uint8_t b2 = c2 & 0xFF;

    uint8_t r = static_cast<uint8_t>(r1 * (1.0f - alpha) + r2 * alpha);
    uint8_t g = static_cast<uint8_t>(g1 * (1.0f - alpha) + g2 * alpha);
    uint8_t b = static_cast<uint8_t>(b1 * (1.0f - alpha) + b2 * alpha);

    return (r << 16) | (g << 8) | b;
}

// Resize framebuffer
void CpuFrameBufferManager::resizeFramebuffer(int width, int height) {
    if (width == settings.width && height == settings.height) {
        return;
    }

    settings.width = width;
    settings.height = height;

    framebuffer.resize(width * height, 0);

    if (settings.useDepthBuffer) {
        depthBuffer.resize(width * height, 1.0f);
    }
}

}
