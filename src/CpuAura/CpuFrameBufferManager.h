#ifndef CPUFRAMEBUFFERMANAGER_H
#define CPUFRAMEBUFFERMANAGER_H

#pragma once

#include <functional>
#include <algorithm>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>

#include "aura.hpp"
#include "Utils/AlignedVector.h"
#include "AuraFont/AuraBitmapFont.h"

namespace aura3d {

struct Point {
    int x;
    int y;
};

struct Rectangle {
    int x;
    int y;
    int width;
    int height;
};

struct Vertex {
    f32 x, y, z;   // Position
    f32 u, v;      // Texture coordinates
    u32 color;  // Vertex color

    Vertex(f32 _x, f32 _y, f32 _z = 0, f32 _u = 0, f32 _v = 0, u32 _color = 0xFFFFFFFF) :
        x(_x), y(_y), z(_z), u(_u), v(_v), color(_color) {}
};

struct Texture {
    int width;
    int height;
    AlignedVector<u32> data;

    Texture(int w, int h) : width(w), height(h), data(w * h, 0) {}

    u32 sample(f32 u, f32 v) const {
        int x = std::clamp(static_cast<int>(u * width), 0, width - 1);
        int y = std::clamp(static_cast<int>(v * height), 0, height - 1);
        return data[y * width + x];
    }
};

class CpuFrameBufferManager
{
public:
    struct Config {
        int width;
        int height;
        bool useDepthBuffer;

        Config() :
            width(1280),
            height(720),
            useDepthBuffer(false) {}
    };

    CpuFrameBufferManager(SDL_Window* window, Config config);
    ~CpuFrameBufferManager();

    // Core rendering
    void clear(u32 color = 0);
    void renderFramebuffer();

    // Memory management
    void resizeFramebuffer(int width, int height);

    // Basic drawing functions
    u32 getPixel(int x, int y) const;
    void setPixel(int x, int y, u32 color);
    void setPixelWithDepth(int x, int y, f32 z, u32 color);
    f32 getDepthPixel(int x, int y) const;

    void drawLine(int x0, int y0, int x1, int y1, u32 color);
    void drawAALine(int x0, int y0, int x1, int y1, u32 color); // Anti-aliased line

    // Shape drawing
    void drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, u32 color);
    void drawTriangleTextured(const Vertex& v0, const Vertex& v1, const Vertex& v2, const Texture& texture);
    void drawCircle(int centerX, int centerY, int radius, u32 color);
    void drawFillRect(int x, int y, int width, int height, u32 color);
    void drawRect(int x, int y, int width, int height, u32 color);
    void drawRoundedRect(int x, int y, int width, int height, int radius, u32 color);

    // Text rendering
    void drawText(const std::string& text, int x, int y, u32 color, f32 fontSize = 1.45);

    // Advanced rendering
    void drawTriangleWithShader(const Vertex& v0, const Vertex& v1, const Vertex& v2,
                                std::function<u32(f32 u, f32 v, f32 w)> fragmentShader);

    // Getters
    int getWidth() const { return settings.width; }
    int getHeight() const { return settings.height; }
    int getTextWidth(const std::string& text, f32 fontSize = 1.45);
    int getTextHeight(const std::string& text, f32 fontSize = 1.45);

    // Helper methods
    void plotPixel(int x, int y, f32 intensity, u32 color);
    bool isInsideBounds(int x, int y) const;
    u32 blendColors(u32 c1, u32 c2, f32 alpha);
    f32 edgeFunction(f32 ax, f32 ay, f32 bx, f32 by, f32 px, f32 py) const;
    void interpolateAttributes(const Vertex& v0, const Vertex& v1, const Vertex& v2,
                               f32 x, f32 y, f32& u, f32& v, f32& w) const;
private:
    // Text rendering helper methods
    void drawBitmapText(const std::string& text, int x, int y, u32 color);

    Config settings;
    AlignedVector<u32> framebuffer;
    AlignedVector<f32> depthBuffer;
    SDL_Window* _window;
    SDL_Renderer* _renderer;
    SDL_Texture* _texture;

    // Aura Font
    const AuraBitmapFont& _font;
};

} // namespace aura3d

#endif // CPUFRAMEBUFFERMANAGER_H
