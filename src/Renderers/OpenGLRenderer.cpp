#include "Renderers/OpenGLRenderer.h"

#include <GL/gl.h>
#include <wma/wma.hpp>
#include <ink/ink.hpp>

#include "aura.hpp"

namespace aura3d {

OpenGLRenderer::OpenGLRenderer(const wma::WindowDetails& windowDetails)
    : Renderer(windowDetails)
{
    // Constructor only stores parameters - initialization happens in initialize()
}

OpenGLRenderer::~OpenGLRenderer()
{
    cleanup();
}

void OpenGLRenderer::initialize()
{
    if (_isInitialized) {
        return;
    }

    // Create window manager
    _windowManagerApi = wma::createWindowManager(
        wma::getDefaultBackend(),
        _windowDetails,
        wma::GraphicsAPI::OpenGL
    );

    _isInitialized = true;
}

void OpenGLRenderer::createWindow(const char* title)
{
    _windowManagerApi->createWindow(title);

    // Setup OpenGL context after window creation
    // This would typically include:
    // - Setting up the OpenGL version
    // - Setting up the viewport
    // - Enabling/disabling features like depth testing, face culling, etc.
}

void OpenGLRenderer::run()
{
    if (!_isInitialized) {
        initialize();
    }

    // Create window with OpenGL context
    createWindow(APPLICATION_NAME);

    // Setup OpenGL resources
    setupShaders();
    createVertexBuffers();

    // Main render loop
    _windowManagerApi->process([&]() {
        // Clear the color buffer
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

        // In a more complete implementation, you would:
        // 1. Bind the shader program
        // 2. Bind the VAO
        // 3. Set any uniforms (MVP matrices, textures, etc.)
        // 4. Issue draw calls
        // 5. Swap buffers (handled by the window manager)
    });
}

void OpenGLRenderer::setupShaders()
{
    // In a more complete implementation, you would:
    // 1. Load and compile vertex and fragment shaders
    // 2. Link shaders into a program
    // 3. Get uniform and attribute locations
}

void OpenGLRenderer::createVertexBuffers()
{
    // In a more complete implementation, you would:
    // 1. Create vertex buffer objects (VBOs)
    // 2. Create vertex array objects (VAOs)
    // 3. Setup attribute pointers
}

void OpenGLRenderer::handleWindowChanges()
{
    // Update viewport on window resize
    auto windowDetails = _windowManagerApi->getWindowDetails();
    glViewport(0, 0, windowDetails->width, windowDetails->height);

    // Update projection matrices or other size-dependent resources
}

void OpenGLRenderer::cleanup()
{
    if (!_isInitialized) {
        return;
    }

    // In a more complete implementation, you would:
    // 1. Delete shader programs
    // 2. Delete VBOs and VAOs
    // 3. Delete textures and framebuffers

    // Finally reset window system
    _windowManagerApi.reset();

    _isInitialized = false;
}

} // namespace aura3d
