#include "aura/Renderer/OpenGL/OpenGLRenderer.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <glad/glad.h>
#include <stdexcept>

#include <glm/gtc/type_ptr.hpp>

#include "aura/Core/Profiling/FrameProfiler.h"
#include "aura/Renderer/OpenGL/EmbeddedGlsl.h"
#include "aura/aura.h"

namespace aura3d
{
namespace gl
{

static_assert(sizeof(GLintptr) == sizeof(void *) && sizeof(GLsizeiptr) == sizeof(void *),
              "OpenGL buffer offsets and sizes must retain the platform's pointer width");

OpenGLRenderer::OpenGLRenderer(const wma::WindowDetails &windowDetails) : IRenderer(windowDetails)
{
    INK_INFO << "Renderer - OPENGL";
}

OpenGLRenderer::~OpenGLRenderer()
{
    cleanup();
}

void OpenGLRenderer::initialize(AuraSettings *settings, const JobSystem *jobs)
{
    if (_isInitialized)
        return;

    //! Unused: this backend has no CPU-side worker pool of its own to share.
    (void)jobs;

    createWindow(settings->getWindowTitle().c_str(), settings->getWindowBackend());
    loadOpenGLEntryPoints();
    compileBuiltInShaders();

    _vertexMgr = std::make_unique<GlVertexBufferManager>();
    _indexMgr = std::make_unique<GlIndexBufferManager>();
    _uniformMgr = std::make_unique<GlUniformBufferManager>();
    _textureMgr = std::make_unique<GlTextureManager>();

    _uniformMgr->create(_shaderProgram);
    createBatchBuffers();
    //! Sampled by untextured batches and invalid scene bindings, as on the other backends.
    _whiteTexture = _textureMgr->createSolidColorTexture(255, 255, 255, 255);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    //! Only batches blend, always as straight alpha, so the factors are set once.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    _isInitialized = true;
}

void OpenGLRenderer::loadOpenGLEntryPoints()
{
    if (!_windowManagerApi)
    {
        throw std::runtime_error("OpenGLRenderer: window manager not created before GLAD load");
    }

    auto *window = static_cast<SDL_Window *>(_windowManagerApi->getWindowInstance());
    if (!window)
    {
        throw std::runtime_error("OpenGLRenderer: SDL window handle is null");
    }

    // wma's SDL backend already creates and makes current a GL context for
    // the OpenGL path (SdlWindowManager::createWindow), so the common case
    // here is "reuse what's already current" -- only create + MakeCurrent
    // ourselves if nothing is current yet. A redundant second MakeCurrent
    // call on an already-current context fails outright under Emscripten's
    // SDL3 port (SDL_GetError() comes back empty, unlike a real GL error).
    SDL_GLContext context = SDL_GL_GetCurrentContext();
    if (!context)
    {
        context = SDL_GL_CreateContext(window);
        if (!context)
        {
            throw std::runtime_error(std::string("OpenGLRenderer: failed to create GL context: ") + SDL_GetError());
        }

        if (!SDL_GL_MakeCurrent(window, context))
        {
            throw std::runtime_error(std::string("OpenGLRenderer: failed to make GL context current: ") +
                                     SDL_GetError());
        }
    }

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)))
    {
        throw std::runtime_error("OpenGLRenderer: gladLoadGLLoader failed");
    }

    INK_INFO << "OpenGL " << GLVersion.major << "." << GLVersion.minor << " | "
             << reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    handleWindowChanges();
}

namespace
{

//! Compiles one stage, logging and returning 0 on failure.
GLuint compileShaderStage(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        INK_ERROR << "GL shader compile error: " << log;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

//! Compiles and links a vertex/fragment pair into a program.
GLuint linkShaderProgram(const char *vertexSource, const char *fragmentSource)
{
    const GLuint vert = compileShaderStage(GL_VERTEX_SHADER, vertexSource);
    const GLuint frag = compileShaderStage(GL_FRAGMENT_SHADER, fragmentSource);
    if (!vert || !frag)
    {
        glDeleteShader(vert);
        glDeleteShader(frag);
        throw std::runtime_error("OpenGLRenderer: built-in shader compilation failed");
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);
    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        glDeleteProgram(program);
        throw std::runtime_error(std::string("OpenGLRenderer: built-in shader link failed: ") + log);
    }

    return program;
}

} // namespace

void OpenGLRenderer::compileBuiltInShaders()
{
    _shaderProgram = linkShaderProgram(gl_shader3d_vert, gl_shader3d_frag);

    //! Both programs sample unit 0; sampler uniforms are program state, so they are set once.
    _coverage3DLoc = glGetUniformLocation(_shaderProgram, "coverageOnly");
    glUseProgram(_shaderProgram);
    glUniform1i(glGetUniformLocation(_shaderProgram, "textureSampler"), 0);

    _batchProgram = linkShaderProgram(gl_batch_vert, gl_batch_frag);
    _batchTransformLoc = glGetUniformLocation(_batchProgram, "uTransform");
    _batchCoverageLoc = glGetUniformLocation(_batchProgram, "coverageOnly");
    glUseProgram(_batchProgram);
    glUniform1i(glGetUniformLocation(_batchProgram, "textureSampler"), 0);

    glUseProgram(_shaderProgram);
}

void OpenGLRenderer::createBatchBuffers()
{
    glGenVertexArrays(1, &_batchVao);
    glGenBuffers(1, &_batchVbo);
    glGenBuffers(1, &_batchEbo);
    glBindVertexArray(_batchVao);
    glBindBuffer(GL_ARRAY_BUFFER, _batchVbo);

    // OpenGL requires byte offsets encoded as pointers when a VBO is bound.
    // NOLINTBEGIN(performance-no-int-to-ptr)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(gfx::BatchVertex),
                          reinterpret_cast<void *>(offsetof(gfx::BatchVertex, pos)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(gfx::BatchVertex),
                          reinterpret_cast<void *>(offsetof(gfx::BatchVertex, texCoord)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(gfx::BatchVertex),
                          reinterpret_cast<void *>(offsetof(gfx::BatchVertex, color)));
    glEnableVertexAttribArray(2);
    // NOLINTEND(performance-no-int-to-ptr)

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _batchEbo);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void OpenGLRenderer::createWindow(const char *title, const wma::WindowBackend &wBackend)
{
    _windowManagerApi = makeWindow(wBackend, _windowDetails, wma::GraphicsAPI::OpenGL);
    _windowManagerApi->createWindow(title);
}

void OpenGLRenderer::handleWindowChanges()
{
    const wma::WindowDetails *wd = _windowManagerApi->getWindowDetails();
    glViewport(0, 0, wd->width, wd->height);
}

void OpenGLRenderer::cleanup()
{
    clearSharedResources();

    if (_shaderProgram)
        glDeleteProgram(_shaderProgram);
    _shaderProgram = 0;

    if (_batchProgram)
        glDeleteProgram(_batchProgram);
    if (_batchVao)
        glDeleteVertexArrays(1, &_batchVao);
    for (GLuint *buffer : {&_batchVbo, &_batchEbo})
        if (*buffer)
            glDeleteBuffers(1, buffer);
    _batchProgram = 0;
    _batchVao = _batchVbo = _batchEbo = 0;
    _batchVboBytes = _batchEboBytes = 0;
    _batchTransformLoc = -1;
    _batchCoverageLoc = _coverage3DLoc = -1;
    _batchCoverage = _coverage3D = -1;
    //! The texture pool dies with _textureMgr below, so drop the cached handle.
    _whiteTexture = {};
    _batchState = false;

    _vertexMgr.reset();
    _indexMgr.reset();
    _uniformMgr.reset();
    _textureMgr.reset();
    _windowManagerApi.reset();
    _isInitialized = false;
}

VertexBufferHandle OpenGLRenderer::createVertexBuffer(std::vector<gfx::Vertex3D> &&vertices)
{
    return _vertexMgr->createVertexBuffer(std::move(vertices));
}

IndexBufferHandle OpenGLRenderer::createIndexBuffer(std::vector<u16> &&indices)
{
    return _indexMgr->createIndexBuffer(std::move(indices));
}

IndexBufferHandle OpenGLRenderer::createIndexBuffer(std::vector<u32> &&indices)
{
    return _indexMgr->createIndexBuffer(std::move(indices));
}

TextureHandle OpenGLRenderer::createSolidColorTexture(u8 r, u8 g, u8 b, u8 a)
{
    return _textureMgr->createSolidColorTexture(r, g, b, a);
}

TextureHandle OpenGLRenderer::createTextureFromPixels(const u8 *rgbaPixels, u32 width, u32 height)
{
    return _textureMgr->createTextureFromPixels(rgbaPixels, width, height);
}

TextureHandle OpenGLRenderer::createDynamicTexture(u32 width, u32 height)
{
    if (!_textureMgr)
        return {};
    return _textureMgr->createDynamicTexture(width, height);
}

TextureHandle OpenGLRenderer::createCoverageTexture(u32 width, u32 height)
{
    return _textureMgr ? _textureMgr->createCoverageTexture(width, height) : TextureHandle{};
}

void OpenGLRenderer::updateCoverageTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height,
                                                 const u8 *coverage)
{
    if (_textureMgr)
        _textureMgr->updateCoverageRegion(handle, x, y, width, height, coverage);
}

void OpenGLRenderer::updateTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height,
                                         const u8 *rgbaPixels)
{
    if (!_textureMgr)
        return;
    _textureMgr->updateRegion(handle, x, y, width, height, rgbaPixels);
}

void OpenGLRenderer::beginFrame()
{
    auto *flags = _windowManagerApi->getWindowFlags();

    if (flags->resized)
    {
        flags->resized = false;
        handleWindowChanges();
    }
}

void OpenGLRenderer::beginRenderPass()
{
    AURA_FRAME_SCOPE(FramePhase::BeginPass);

    //! cleanup() can run mid-frame (the ESC key action calls it), which drops
    //! the managers while the frame loop is still executing.
    if (!_uniformMgr)
        return;

    //! Depth writes must be on for the clear to reach the depth buffer.
    useSceneState();
    glClearColor(_clearR, _clearG, _clearB, _clearA);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    _uniformMgr->bind(_shaderProgram);
    _uniformMgr->update(_currentTransform);
    _uniformMgr->updateLight(_light);
}

void OpenGLRenderer::endRenderPass()
{
    /*
     * OpenGL has no render-pass object to close, but the pass boundary is still
     * the point at which recorded work must be handed to the driver. Flushing
     * here mirrors the Vulkan backend's vkCmdEndRenderPass and unbinds the VAO
     * so state does not leak into whatever the caller does next.
     */
    AURA_FRAME_SCOPE(FramePhase::EndPass);

    if (!_vertexMgr)
        return;

    _vertexMgr->unbind();
    glFlush();
}

void OpenGLRenderer::endFrame()
{
    /*
     * Present is where a vsynced GL frame actually spends its wall time: the
     * driver blocks inside SDL_GL_SwapWindow until the display is ready for the
     * buffer, so the whole pipeline's backpressure lands on this one scope and
     * nowhere else. WaitFence/Acquire/Submit stay at zero for this backend --
     * GL has no explicit counterpart to any of them, and reporting zero says
     * exactly that rather than inventing an attribution.
     */
    if (_windowManagerApi)
    {
        AURA_FRAME_SCOPE(FramePhase::Present);

        auto *window = static_cast<SDL_Window *>(_windowManagerApi->getWindowInstance());
        if (window)
        {
            SDL_GL_SwapWindow(window);
        }
    }

    //! Outside the scope above so the present is closed and counted before the
    //! frame is, and unconditional so a frame still closes when the window
    //! manager has already gone (a cleanup() mid-loop) rather than stalling the
    //! sample stream on a backend that is shutting down.
    AURA_FRAME_END();
}

void OpenGLRenderer::setTransform(const gfx::TransformUBO &ubo)
{
    _currentTransform = ubo;

    //! Callers set a new transform per object before each drawMesh (mirroring
    //! the Vulkan backend's per-draw push constants), so this must upload
    //! immediately, beginRenderPass's own upload only seeds the first draw
    //! of the frame, and without this every draw call after the first reused
    //! whatever transform was left over from the previous frame's last object.
    if (_uniformMgr)
        _uniformMgr->update(_currentTransform);
}

void OpenGLRenderer::setLight(const gfx::LightUBO &light)
{
    IRenderer::setLight(light);

    //! Uploaded immediately when a context already exists, and re-uploaded every
    //! beginRenderPass so a light set before initialize() is not lost.
    if (_uniformMgr)
        _uniformMgr->updateLight(_light);
}

void OpenGLRenderer::bindVertexBuffer(VertexBufferHandle handle)
{
    _currentVertexBuffer = handle;

    /*
     * A VAO switch carries the element-array binding with it, so the index
     * manager's cache stops describing reality the moment a different VAO
     * becomes current. bind() reports exactly that case.
     */
    if (_vertexMgr->bind(handle) && _indexMgr)
        _indexMgr->invalidateBinding();
}

void OpenGLRenderer::bindIndexBuffer(IndexBufferHandle handle)
{
    _currentIndexBuffer = handle;
    _indexMgr->bind(handle);
}

void OpenGLRenderer::bindTexture(TextureHandle handle)
{
    _currentTexture = handle;
    //! While batches own the program, the switch back binds it.
    if (!_batchState)
        bindSceneTexture();
}

void OpenGLRenderer::bindSceneTexture()
{
    const GlTextureData *data = _textureMgr->bind(_currentTexture, 0);
    if (!data)
        data = _textureMgr->bind(_whiteTexture, 0);
    const GLint coverage = data && data->coverageOnly ? 1 : 0;
    if (_coverage3D != coverage)
    {
        glUniform1i(_coverage3DLoc, coverage);
        _coverage3D = coverage;
    }
}

void OpenGLRenderer::useSceneState()
{
    if (!_batchState)
        return;

    _batchState = false;
    glUseProgram(_shaderProgram);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    //! The batch VAO replaced the scene's, along with its element-array binding.
    _vertexMgr->invalidateBinding();
    if (!_vertexMgr->bind(_currentVertexBuffer))
        _vertexMgr->unbind();
    _indexMgr->invalidateBinding();
    _indexMgr->bind(_currentIndexBuffer);
    bindSceneTexture();
}

void OpenGLRenderer::drawIndexed(u32 indexCount, u32 instanceCount)
{
    auto *idxData = _indexMgr->get(_currentIndexBuffer);
    if (!idxData)
        return;

    useSceneState();
    if (instanceCount > 1)
    {
        glDrawElementsInstanced(GL_TRIANGLES, indexCount, idxData->type, nullptr, instanceCount);
    }
    else
    {
        glDrawElements(GL_TRIANGLES, indexCount, idxData->type, nullptr);
    }
}

void OpenGLRenderer::draw(u32 vertexCount, u32 instanceCount)
{
    useSceneState();
    if (instanceCount > 1)
    {
        glDrawArraysInstanced(GL_TRIANGLES, 0, vertexCount, instanceCount);
    }
    else
    {
        glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    }
}

glm::uvec2 OpenGLRenderer::renderTargetSize() const noexcept
{
    //! The size handleWindowChanges() gives glViewport, so pixels map one to one.
    const wma::WindowDetails *wd = _windowManagerApi ? _windowManagerApi->getWindowDetails() : nullptr;
    return wd ? glm::uvec2{static_cast<u32>(std::max(wd->width, 0)), static_cast<u32>(std::max(wd->height, 0))}
              : glm::uvec2{0};
}

void OpenGLRenderer::drawBatch(std::span<const gfx::BatchVertex> vertices, std::span<const u32> indices,
                               TextureHandle texture, gfx::BatchSpace space)
{
    AURA_FRAME_SCOPE(space == gfx::BatchSpace::Screen ? FramePhase::RecordOverlay : FramePhase::RecordScene);

    if (!_batchProgram || !_textureMgr || vertices.empty() || indices.empty())
        return;

    //! Entered once per run of batches; the next scene draw leaves it (useSceneState()).
    if (!_batchState)
    {
        glUseProgram(_batchProgram);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        _batchState = true;
    }

    glUniformMatrix4fv(_batchTransformLoc, 1, GL_FALSE, glm::value_ptr(batchTransform(space)));
    const GlTextureData *data = _textureMgr->bind(texture, 0);
    if (!data)
        data = _textureMgr->bind(_whiteTexture, 0);
    const GLint coverage = data && data->coverageOnly ? 1 : 0;
    if (_batchCoverage != coverage)
    {
        glUniform1i(_batchCoverageLoc, coverage);
        _batchCoverage = coverage;
    }

    //! The batch owns its VAO, so the managers' bind caches stop describing reality.
    glBindVertexArray(_batchVao);
    _vertexMgr->invalidateBinding();
    _indexMgr->invalidateBinding();

    // Orphan the stores so uploads need not wait for preceding draws.
    glBindBuffer(GL_ARRAY_BUFFER, _batchVbo);
    _batchVboBytes = std::max(_batchVboBytes, vertices.size_bytes());
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(_batchVboBytes), nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertices.size_bytes()), vertices.data());

    _batchEboBytes = std::max(_batchEboBytes, indices.size_bytes());
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(_batchEboBytes), nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(indices.size_bytes()), indices.data());

    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, nullptr);
}

void OpenGLRenderer::setClearColor(f32 r, f32 g, f32 b, f32 a)
{
    _clearR = r;
    _clearG = g;
    _clearB = b;
    _clearA = a;
}

} // namespace gl
} // namespace aura3d
