#ifndef CPUFRAMEBUFFERMANAGER_H
#define CPUFRAMEBUFFERMANAGER_H
#pragma once

#include <SDL2/SDL.h>
#include <vector>
#include <algorithm>
#include <functional>
#include <cmath>
#include <immintrin.h>

namespace aura3d {

class CpuFrameBufferManager
{
public:
    struct Vertex {
        float x, y, z;   // Position
        float u, v;      // Texture coordinates
        uint32_t color;  // Vertex color

        Vertex(float _x, float _y, float _z = 0, float _u = 0, float _v = 0, uint32_t _color = 0xFFFFFFFF) :
            x(_x), y(_y), z(_z), u(_u), v(_v), color(_color) {}
    };

    struct Texture {
        int width;
        int height;
        std::vector<uint32_t> data;

        Texture(int w, int h) : width(w), height(h), data(w * h, 0) {}

        uint32_t sample(float u, float v) const {
            int x = std::clamp(static_cast<int>(u * width), 0, width - 1);
            int y = std::clamp(static_cast<int>(v * height), 0, height - 1);
            return data[y * width + x];
        }
    };

    struct Config {
        int width;
        int height;
        bool useDepthBuffer;

        Config() :
            width(1280),
            height(720),
            useDepthBuffer(false) {}
    };

    CpuFrameBufferManager(Config config);
    ~CpuFrameBufferManager();

    // Core rendering
    void clear(uint32_t color = 0);
    void renderFramebuffer(SDL_Renderer* renderer, SDL_Texture* texture);

    // Basic drawing functions
    void setPixel(int x, int y, uint32_t color);
    void setPixelWithDepth(int x, int y, float z, uint32_t color);
    uint32_t getPixel(int x, int y) const;
    void drawLine(int x0, int y0, int x1, int y1, uint32_t color);
    void drawAALine(int x0, int y0, int x1, int y1, uint32_t color); // Anti-aliased line

    // Shape drawing
    void drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color);
    void drawTriangleTextured(const Vertex& v0, const Vertex& v1, const Vertex& v2, const Texture& texture);
    void drawCircle(int centerX, int centerY, int radius, uint32_t color);
    void drawRect(int x, int y, int width, int height, uint32_t color);
    void drawRoundedRect(int x, int y, int width, int height, int radius, uint32_t color);

    // Advanced rendering
    void drawTriangleWithShader(const Vertex& v0, const Vertex& v1, const Vertex& v2,
                                std::function<uint32_t(float u, float v, float w)> fragmentShader);

    // Memory management
    void resizeFramebuffer(int width, int height);

    // Getters
    int getWidth() const { return settings.width; }
    int getHeight() const { return settings.height; }

    bool isInsideBounds(int x, int y) const;
    uint32_t blendColors(uint32_t c1, uint32_t c2, float alpha);
    float edgeFunction(float ax, float ay, float bx, float by, float px, float py) const;
    void interpolateAttributes(const Vertex& v0, const Vertex& v1, const Vertex& v2,
                               float x, float y, float& u, float& v, float& w) const;
private:
    Config settings;
    std::vector<uint32_t> framebuffer;
    std::vector<float> depthBuffer;
};

}

#endif // CPUFRAMEBUFFERMANAGER_H
