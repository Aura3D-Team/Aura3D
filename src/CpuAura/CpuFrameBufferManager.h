#ifndef CPUFRAMEBUFFERMANAGER_H
#define CPUFRAMEBUFFERMANAGER_H
#pragma once

#include <vector>
#include <functional>
#include <algorithm>
#include <cstdint>
#include <SDL2/SDL.h>

namespace aura3d {

// Custom aligned allocator for high-performance memory access
template<typename T, std::size_t Alignment = 32> // 32-byte alignment for AVX instructions
class AlignedAllocator {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    template<typename U>
    struct rebind {
        using other = AlignedAllocator<U, Alignment>;
    };

    AlignedAllocator() noexcept = default;
    template<typename U>
    AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

    pointer allocate(size_type n) {
        void* ptr = nullptr;
#if defined(_MSC_VER)
        ptr = _aligned_malloc(n * sizeof(T), Alignment);
#else
        if (posix_memalign(&ptr, Alignment, n * sizeof(T))) {
            ptr = nullptr;
        }
#endif

        if (!ptr) {
            throw std::bad_alloc();
        }

        return static_cast<pointer>(ptr);
    }

    void deallocate(pointer p, size_type) noexcept {
#if defined(_MSC_VER)
        _aligned_free(p);
#else
        free(p);
#endif
    }

    // Required for C++11 allocator compatibility
    bool operator==(const AlignedAllocator&) const noexcept { return true; }
    bool operator!=(const AlignedAllocator&) const noexcept { return false; }
};

// Use the aligned allocator with vectors for SIMD-friendly memory access
template<typename T, std::size_t Alignment = 32>  // 32-byte alignment for AVX
using AlignedVector = std::vector<T, AlignedAllocator<T, Alignment>>;

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
        AlignedVector<uint32_t> data;

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

    CpuFrameBufferManager(SDL_Window* window, Config config);
    ~CpuFrameBufferManager();

    // Core rendering
    void clear(uint32_t color = 0);
    void renderFramebuffer();

    // Memory management
    void resizeFramebuffer(int width, int height);

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
    void drawFillRect(int x, int y, int width, int height, uint32_t color);
    void drawRect(int x, int y, int width, int height, uint32_t color);
    void drawRoundedRect(int x, int y, int width, int height, int radius, uint32_t color);

    // Advanced rendering
    void drawTriangleWithShader(const Vertex& v0, const Vertex& v1, const Vertex& v2,
                                std::function<uint32_t(float u, float v, float w)> fragmentShader);

    // Getters
    int getWidth() const { return settings.width; }
    int getHeight() const { return settings.height; }

    // Helper methods
    void plotPixel(int x, int y, float intensity, uint32_t color);
    bool isInsideBounds(int x, int y) const;
    uint32_t blendColors(uint32_t c1, uint32_t c2, float alpha);
    float edgeFunction(float ax, float ay, float bx, float by, float px, float py) const;
    void interpolateAttributes(const Vertex& v0, const Vertex& v1, const Vertex& v2,
                               float x, float y, float& u, float& v, float& w) const;

private:
    Config settings;
    AlignedVector<uint32_t> framebuffer;
    AlignedVector<float> depthBuffer;
    SDL_Window* _window;
    SDL_Renderer* _renderer;
    SDL_Texture* _texture;
};

} // namespace aura3d

#endif // CPUFRAMEBUFFERMANAGER_H
