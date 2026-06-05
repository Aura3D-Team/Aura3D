#include "aura/Renderer/OpenGL/OpenGLRenderer.h"

#include <glad/glad.h>
#include <SDL2/SDL.h>
#include <stdexcept>
#include <thread>

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

void OpenGLRenderer::initialize(const AuraSettings* settings)
{
    if (_isInitialized) return;

    createWindow(APPLICATION_NAME, wma::WindowBackend::SDL2);
    loadOpenGLEntryPoints();
    compileBuiltInShaders();

    _vertexMgr  = std::make_unique<GlVertexBufferManager>();
    _indexMgr   = std::make_unique<GlIndexBufferManager>();
    _uniformMgr = std::make_unique<GlUniformBufferManager>();
    _textureMgr = std::make_unique<GlTextureManager>();

    _uniformMgr->create(_shaderProgram);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    _isInitialized = true;
}

void OpenGLRenderer::loadOpenGLEntryPoints()
{
    if (!_windowManagerApi) {
        throw std::runtime_error("OpenGLRenderer: window manager not created before GLAD load");
    }

    auto* window = static_cast<SDL_Window*>(_windowManagerApi->getWindowInstance());
    if (!window) {
        throw std::runtime_error("OpenGLRenderer: SDL window handle is null");
    }

    SDL_GLContext context = SDL_GL_GetCurrentContext();
    if (!context) {
        context = SDL_GL_CreateContext(window);
        if (!context) {
            throw std::runtime_error(
                std::string("OpenGLRenderer: failed to create GL context: ") + SDL_GetError());
        }
    }

    if (SDL_GL_MakeCurrent(window, context) != 0) {
        throw std::runtime_error(
            std::string("OpenGLRenderer: failed to make GL context current: ") + SDL_GetError());
    }

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        throw std::runtime_error("OpenGLRenderer: gladLoadGLLoader failed");
    }

    INK_INFO << "OpenGL " << GLVersion.major << "." << GLVersion.minor
             << " | " << reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    handleWindowChanges();
}

void OpenGLRenderer::compileBuiltInShaders()
{
    auto compileShader = [](GLenum type, const char* source) -> GLuint {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint ok;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetShaderInfoLog(shader, 512, nullptr, log);
            INK_ERROR << "GL shader compile error: " << log;
        }
        return shader;
    };

    const char* vertSrc = is2D() ? GL_VERTEX_2D : GL_VERTEX_3D;
    const char* fragSrc = is2D() ? GL_FRAGMENT_2D : GL_FRAGMENT_3D;

    GLuint vert = compileShader(GL_VERTEX_SHADER, vertSrc);
    GLuint frag = compileShader(GL_FRAGMENT_SHADER, fragSrc);

    _shaderProgram = glCreateProgram();
    glAttachShader(_shaderProgram, vert);
    glAttachShader(_shaderProgram, frag);
    glLinkProgram(_shaderProgram);

    GLint ok;
    glGetProgramiv(_shaderProgram, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(_shaderProgram, 512, nullptr, log);
        INK_ERROR << "GL shader link error: " << log;
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
}

void OpenGLRenderer::createWindow(const char* title, const wma::WindowBackend& wBackend)
{
    _windowManagerApi = wma::createWindowManager(
        wBackend, _windowDetails, wma::GraphicsAPI::OpenGL
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
    if (_shaderProgram) glDeleteProgram(_shaderProgram);
    _shaderProgram = 0;
    _vertexMgr.reset();
    _indexMgr.reset();
    _uniformMgr.reset();
    _textureMgr.reset();
    _windowManagerApi.reset();
    _isInitialized = false;
}

// --- Resource creation ---

VertexBufferHandle OpenGLRenderer::createVertexBuffer(std::vector<gfx::Vertex2D>&& vertices)
{
    return _vertexMgr->createVertexBuffer(std::move(vertices));
}

VertexBufferHandle OpenGLRenderer::createVertexBuffer(std::vector<gfx::Vertex3D>&& vertices)
{
    return _vertexMgr->createVertexBuffer(std::move(vertices));
}

IndexBufferHandle OpenGLRenderer::createIndexBuffer(std::vector<u16>&& indices)
{
    return _indexMgr->createIndexBuffer(std::move(indices));
}

IndexBufferHandle OpenGLRenderer::createIndexBuffer(std::vector<u32>&& indices)
{
    return _indexMgr->createIndexBuffer(std::move(indices));
}

TextureHandle OpenGLRenderer::createSolidColorTexture(u8 r, u8 g, u8 b, u8 a)
{
    return _textureMgr->createSolidColorTexture(r, g, b, a);
}

// --- Frame lifecycle ---

void OpenGLRenderer::beginFrame()
{
    auto* wd = _windowManagerApi->getWindowDetails();
    auto* flags = _windowManagerApi->getWindowFlags();

    if (flags->resized) {
        wma::WindowFlags* wf = flags;
        while (wf->resized) {
            wf->resized = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        handleWindowChanges();
    }
}

void OpenGLRenderer::beginRenderPass()
{
    glClearColor(_clearR, _clearG, _clearB, _clearA);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    _uniformMgr->bind(_shaderProgram);
    _uniformMgr->update(_currentTransform);
}

void OpenGLRenderer::endRenderPass()
{
}

void OpenGLRenderer::endFrame()
{
    if (!_windowManagerApi) return;

    auto* window = static_cast<SDL_Window*>(_windowManagerApi->getWindowInstance());
    if (window) {
        SDL_GL_SwapWindow(window);
    }
}

// --- Drawing state ---

void OpenGLRenderer::setTransform(const gfx::TransformUBO& ubo)
{
    _currentTransform = ubo;
}

void OpenGLRenderer::bindVertexBuffer(VertexBufferHandle handle)
{
    _currentVertexBuffer = handle;
    _vertexMgr->bind(handle);
}

void OpenGLRenderer::bindIndexBuffer(IndexBufferHandle handle)
{
    _currentIndexBuffer = handle;
    _indexMgr->bind(handle);
}

void OpenGLRenderer::bindTexture(TextureHandle handle)
{
    _currentTexture = handle;
    _textureMgr->bind(handle, 0);
    glUniform1i(glGetUniformLocation(_shaderProgram, "textureSampler"), 0);
}

void OpenGLRenderer::drawIndexed(u32 indexCount, u32 instanceCount)
{
    auto* idxData = _indexMgr->get(_currentIndexBuffer);
    if (!idxData) return;

    if (instanceCount > 1) {
        glDrawElementsInstanced(GL_TRIANGLES, indexCount, idxData->type, nullptr, instanceCount);
    } else {
        glDrawElements(GL_TRIANGLES, indexCount, idxData->type, nullptr);
    }
}

void OpenGLRenderer::draw(u32 vertexCount, u32 instanceCount)
{
    if (instanceCount > 1) {
        glDrawArraysInstanced(GL_TRIANGLES, 0, vertexCount, instanceCount);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    }
}

void OpenGLRenderer::setClearColor(f32 r, f32 g, f32 b, f32 a)
{
    _clearR = r; _clearG = g; _clearB = b; _clearA = a;
}

} // namespace gl
} // namespace aura3d
