#ifndef OPENGL_RENDERER_H
#define OPENGL_RENDERER_H

#pragma once

#include "aura/Renderer/IRenderer.h"

namespace aura3d {
namespace gl {

/**
 * @brief OpenGL implementation of the Renderer interface
 */
class OpenGLRenderer : public IRenderer {
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

    /**
     * @brief Create uniform buffers for shader parameters
     */
    void createUniformBuffers();

    /**
     * @brief Create a texture for fragment shader
     */
    void createDefaultTexture();

    /**
     * @brief Update matrices for perspective view
     */
    void updateGlobalMatrices();

private:
    std::unique_ptr<wma::IWindowManager> _windowManagerApi;

    // OpenGL context and resource handles would go here
    bool _isInitialized = false;
    // OpenGL Object IDs
    u32 _shaderProgram;
    u32 _VAO; // Vertex Array Object
    u32 _VBO; // Vertex Buffer Object
    u32 _EBO; // Element Buffer Object
    u32 _UBO; // Uniform Buffer Object

    u32 _whiteTexture;
};

}
} // namespace aura3d

#endif // OPENGL_RENDERER_H
