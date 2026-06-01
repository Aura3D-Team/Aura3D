#include "aura/Renderer/OpenGL/OpenGLRenderer.h"

#include <glad/glad.h>

#include "aura/aura.h"

namespace aura3d {
namespace gl {

OpenGLRenderer::OpenGLRenderer(const wma::WindowDetails& windowDetails, RendererMode mode)
    : IRenderer(windowDetails, mode)
{
    INK_INFO << "Renderer - OPENGL (" << RendererModeToString(mode) << ")";
}

OpenGLRenderer::~OpenGLRenderer()
{
    cleanup();
}

void OpenGLRenderer::initialize()
{
    if (_isInitialized) return;

    createWindow(APPLICATION_NAME);

    _windowManagerApi->getKeyboardListener().addKeyAction(wma::Key::KEY_ESCAPE, wma::KeyAction{
        [this](){ cleanup(); }, nullptr });

    _isInitialized = true;
}

void OpenGLRenderer::createWindow(const char* title)
{
    _windowManagerApi = wma::createWindowManager(
        wma::WindowBackend::SDL2, _windowDetails, wma::GraphicsAPI::OpenGL
    );

    _windowManagerApi->createWindow(title);
}

void OpenGLRenderer::handleWindowChanges()
{
    const wma::WindowDetails* wd = _windowManagerApi->getWindowDetails();
    glViewport(0, 0, wd->width, wd->height);
}

void OpenGLRenderer::cleanup()
{
    if (!_isInitialized) return;

    _windowManagerApi.reset();
    _isInitialized = false;
}

}
} // namespace aura3d
