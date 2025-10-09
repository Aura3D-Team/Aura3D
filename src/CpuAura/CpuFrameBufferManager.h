#ifndef CPUFRAMEBUFFERMANAGER_H
#define CPUFRAMEBUFFERMANAGER_H

#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>

// For C++20 PI constants.
// If you are not using C++20, you can define PI_FLOAT manually:
// const float PI_FLOAT = 3.1415926535f;
// or more accurately: const float PI_FLOAT = std::acos(-1.0f);
#if __cplusplus >= 202002L
#include <numbers>
static const float PI_FLOAT = std::numbers::pi_v<float>;
#else
// Fallback for pre-C++20 (or if <numbers> is not available)
static const float PI_FLOAT = std::acos(-1.0f);
#endif

#include "aura.hpp"
#include "Utils/AlignedVector.h"
#include "AuraFont/AuraBitmapFont.h"

namespace aura3d {

struct Pixel {
    Pixel(u32 _rgb = 0, f32 _z = 1.0f) :
        rgb(_rgb), z(_z) {}

    u32 rgb;
    f32 z;
};

struct Point {
    Point(i32 _x, i32 _y) :
        x(_x), y(_y) {}

    i32 x;
    i32 y;

    inline bool isValid(Point dims) noexcept {
        return x <= dims.x && x >= 0 && y <= dims.y && y >= 0;
    }
};

struct Rectangle {
    Rectangle(i32 _x, i32 _y, i32 _width, i32 _height) :
        x(_x), y(_y), width(_width), height(_height) {}

    i32 x;
    i32 y;
    i32 width;
    i32 height;

    inline bool isValid() noexcept {
        return x <= width && x >= 0 && y <= height && y >= 0;
    }
};

struct Vertex {
    Vertex(f32 _x, f32 _y, f32 _z = 0, f32 _u = 0, f32 _v = 0, u32 _color = 0xFFFFFFFF) :
        x(_x), y(_y), z(_z), u(_u), v(_v), color(_color) {}

    f32 x, y, z;    // Position
    f32 u, v;       // Texture coordinates
    u32 color;  // Vertex color
};

struct Texture {
    Texture(int w, int h) : data(w * h, 0), width(w), height(h)  {}

    AlignedVector<u32> data;
    i32 width;
    i32 height;

    u32 sample(f32 u, f32 v) const {
        int x = INK_CLAMP(static_cast<int>(u * width), 0, width - 1);
        int y = INK_CLAMP(static_cast<int>(v * height), 0, height - 1);
        return data[y * width + x];
    }
};

enum class InterpolationMethod {
    Linear = 0,
    Smoothstep = 1,
    Accelerate = 2,
    Decelerate = 3,
    Sine = 4,
    Cosine = 5
};

class CpuFrameBufferManager
{
public:
    struct Config {
        i32 width;
        i32 height;
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
    Pixel getPixel(Point p) const;
    void setPixel(Point p, u32 color);
    void setPixelWithDepth(Point p, f32 z, u32 color);

    void drawLine(Point p0, Point p1, u32 color);

    void drawPolygon(const std::vector<Point>& points, u32 color, bool closed = true);
    void drawPolygon(const std::vector<Point>& points, const std::vector<u32>& colors, bool closed = true);
    void drawFilledPolygon(const std::vector<Point>& points, u32 color);

    // Text rendering
    void drawText(const std::string& text, Point p, u32 color, f32 fontSize = 1.45);

    // Getters
    i32 getWidth() const { return settings.width; }
    i32 getHeight() const { return settings.height; }
    i32 getTextWidth(const std::string& text, f32 fontSize = 1.45);
    i32 getTextHeight(const std::string& text, f32 fontSize = 1.45);

    // Helper methods
    void plotPixel(Point p, f32 intensity, u32 color);
    bool isInsideBounds(Point p) const;
    u32 blendColors(u32 c1, u32 c2, f32 alpha);

    template<typename T>
    T interpolate(const T& a, const T& b, float t_param, InterpolationMethod method = InterpolationMethod::Linear);

private:
    float get_eased_time(float t_param, InterpolationMethod method);

private:

    Config settings;
    AlignedVector<Pixel> framebuffer;
    SDL_Window* _window;
    SDL_Renderer* _renderer;
    SDL_Texture* _texture;

    // Aura Font
    const AuraBitmapFont& _font;
};

} // namespace aura3d

#endif // CPUFRAMEBUFFERMANAGER_H
