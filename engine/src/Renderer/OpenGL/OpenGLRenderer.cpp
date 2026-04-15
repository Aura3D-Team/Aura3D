#include "aura/Renderer/OpenGL/OpenGLRenderer.h"

#include <glad/glad.h>

#include "aura/aura.h"

namespace aura3d {
namespace gl {

OpenGLRenderer::OpenGLRenderer(const wma::WindowDetails& windowDetails)
    : IRenderer(windowDetails)
{
    INK_INFO << "Renderer - OPENGL";
}

OpenGLRenderer::~OpenGLRenderer()
{
    cleanup();
}

void OpenGLRenderer::initialize()
{
    // Empty
}

void OpenGLRenderer::createWindow(const char* title)
{
    _windowManagerApi = wma::createWindowManager(
        wma::WindowBackend::SDL2, _windowDetails, wma::GraphicsAPI::OpenGL
    );

    // _windowManagerApi->getKeyboardListener().addKeyAction(wma::Key::KEY_ESCAPE, wma::KeyAction{
    //     [this](){ cleanup(); }, nullptr });

    _windowManagerApi->createWindow(title);
}

void OpenGLRenderer::handleWindowChanges()
{
    const wma::WindowDetails* wd = _windowManagerApi->getWindowDetails();
    glViewport(0, 0, wd->width, wd->height);
}

void OpenGLRenderer::cleanup()
{
    _windowManagerApi.reset();
}

}
} // namespace aura3d
