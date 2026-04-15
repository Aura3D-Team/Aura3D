#ifndef OPENGL_RENDERER_H
#define OPENGL_RENDERER_H

#pragma once

#include "aura/Renderer/IRenderer.h"

namespace aura3d {
namespace gl {

/**
 * @brief OpenGL implementation of the Renderer interface.
 *
 * initialize() creates the SDL2 window, establishes the OpenGL context and
 * loads GLAD.  All geometry, shaders and the per-frame draw loop are the
 * responsibility of the consuming application (Sandbox).
 */
class OpenGLRenderer : public IRenderer {
public:
    OpenGLRenderer(const wma::WindowDetails& windowDetails);
    virtual ~OpenGLRenderer();

    /**
     * @brief Create the window and initialise the OpenGL context.
     * After this call GLAD is loaded and GL calls are valid.
     */
    void initialize() override;

    /**
     * @brief Update the GL viewport after a window resize.
     */
    void handleWindowChanges() override;

    /**
     * @brief Release window and GL context resources.
     */
    void cleanup() override;

    wma::IWindowManager* getWindowManager() override { return _windowManagerApi.get(); }

protected:
    void createWindow(const char* title) override;

private:
    std::unique_ptr<wma::IWindowManager> _windowManagerApi;
};

}
} // namespace aura3d

#endif // OPENGL_RENDERER_H
