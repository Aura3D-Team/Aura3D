#ifndef IRENDERER_H
#define IRENDERER_H

#pragma once

#include <wma/wma.hpp>

#define RENDERER_LIST   \
    X(SOFTWARE)         \
    X(OPENGL)           \
    X(VULKAN)           \

namespace aura3d {

enum class RendererChoice
{

#define X(name) name,
    RENDERER_LIST
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

        return false;
}

/**
 * @brief Abstract base class for all renderer implementations
 *
 * The Renderer class provides a common interface for different rendering backends
 * (Vulkan, OpenGL, CPU-based software rendering, etc.)
 */
class IRenderer {
public:
    /**
     * @brief Constructor with window configuration
     * @param windowDetails Window configuration parameters
     */
    IRenderer(const wma::WindowDetails& windowDetails) : _windowDetails(windowDetails) {};

    /**
     * @brief Virtual destructor
     */
    virtual ~IRenderer() = default;

    /**
     * @brief Initialize the renderer
     * This method should be called before any rendering operations
     */
    virtual void initialize() = 0;

    /**
     * @brief Handle window resize and other window-related changes
     */
    virtual void handleWindowChanges() = 0;

    /**
     * @brief Clean up all resources used by the renderer
     */
    virtual void cleanup() = 0;

    /**
     * @brief Get the current window details
     * @return Current window configuration
     */
    const wma::WindowDetails& getWindowDetails() const { return _windowDetails; }

    /**
     * @brief Get the underlying window manager
     * @return Pointer to the window manager (valid after initialize())
     */
    virtual wma::IWindowManager* getWindowManager() = 0;

protected:
    /**
     * @brief Create and initialize the window
     * @param title Window title
     */
    virtual void createWindow(const char* title) = 0;

    // Window details
    wma::WindowDetails _windowDetails;
};

} // namespace aura3d

#endif // RENDERER_H
