#ifndef IRENDERER_H
#define IRENDERER_H

#pragma once

#include <wma/wma.hpp>

#define RENDERER_LIST   \
    X(SOFTWARE)         \
    X(OPENGL)           \
    X(VULKAN)           \

#define RENDERER_MODE_LIST \
    X(MODE_2D)             \
    X(MODE_3D)             \

namespace aura3d {

enum class RendererChoice
{
#define X(name) name,
    RENDERER_LIST
#undef X
};

enum class RendererMode
{
#define X(name) name,
    RENDERER_MODE_LIST
#undef X
};

inline bool RendererChoiceFromString(const std::string& s, RendererChoice& out)
{
    std::string up;
    up.reserve(s.size());
    for (const char c : s) up += std::toupper(c);

#define X(name) if (up == #name) { out = RendererChoice::name; return true; }
    RENDERER_LIST
#undef X

    if (up == "CPU") { out = RendererChoice::SOFTWARE; return true; }

    return false;
}

inline bool RendererModeFromString(const std::string& s, RendererMode& out)
{
    std::string low;
    low.reserve(s.size());
    for (const char c : s) low += std::tolower(c);

    if (low == "2d" || low == "mode_2d") { out = RendererMode::MODE_2D; return true; }
    if (low == "3d" || low == "mode_3d") { out = RendererMode::MODE_3D; return true; }

    return false;
}

inline const char* RendererChoiceToString(RendererChoice c)
{
    switch (c) {
    case RendererChoice::SOFTWARE: return "SOFTWARE";
    case RendererChoice::OPENGL:   return "OPENGL";
    case RendererChoice::VULKAN:   return "VULKAN";
    }
    return "UNKNOWN";
}

inline const char* RendererModeToString(RendererMode m)
{
    switch (m) {
    case RendererMode::MODE_2D: return "2D";
    case RendererMode::MODE_3D: return "3D";
    }
    return "UNKNOWN";
}

class IRenderer {
public:
    IRenderer(const wma::WindowDetails& windowDetails, RendererMode mode)
        : _windowDetails(windowDetails), _mode(mode) {}

    virtual ~IRenderer() = default;

    virtual void initialize() = 0;
    virtual void handleWindowChanges() = 0;
    virtual void cleanup() = 0;

    const wma::WindowDetails& getWindowDetails() const { return _windowDetails; }
    RendererMode getMode() const { return _mode; }
    bool is2D() const { return _mode == RendererMode::MODE_2D; }
    bool is3D() const { return _mode == RendererMode::MODE_3D; }

    virtual wma::IWindowManager* getWindowManager() = 0;

    virtual RendererChoice getBackendType() const = 0;

protected:
    virtual void createWindow(const char* title) = 0;

    wma::WindowDetails _windowDetails;
    RendererMode _mode;
};

} // namespace aura3d

#endif // IRENDERER_H
