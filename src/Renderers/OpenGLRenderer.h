#ifndef OPENGL_RENDERER_H
#define OPENGL_RENDERER_H

#pragma once

#include "Renderers/Renderer.h"

namespace aura3d {

/**
 * @brief OpenGL implementation of the Renderer interface
 */
class OpenGLRenderer : public Renderer {
public:
    /**
     * @brief Construct an OpenGL renderer
     *
     * @param windowDetails Window configuration
     */
    OpenGLRenderer(const wma::WindowDetails& windowDetails);

    /**
     * @brief Destroy the OpenGL renderer
     */
    virtual ~OpenGLRenderer();

    /**
     * @brief Initialize the OpenGL renderer
     */
    void initialize() override;

    /**
     * @brief Run the main rendering loop
     */
    void run() override;

    /**
     * @brief Handle window changes (resize, etc.)
     */
    void handleWindowChanges() override;

    /**
     * @brief Clean up OpenGL resources
     */
    void cleanup() override;

protected:
    /**
     * @brief Create the window with OpenGL support
     *
     * @param title Window title
     */
    void createWindow(const char* title) override;

    /**
     * @brief Set up the OpenGL shaders
     */
    void setupShaders();

    /**
     * @brief Create vertex buffers for rendering
     */
    void createVertexBuffers();

private:
    std::unique_ptr<wma::IWindowManager> _windowManagerApi;

    // OpenGL context and resource handles would go here
    bool _isInitialized = false;
};

} // namespace aura3d

#endif // OPENGL_RENDERER_H
