#include "aura/Renderer/OpenGL/OpenGLRenderer.h"

#include <glad/glad.h>
#include <stdexcept>

#include <glm/gtc/type_ptr.hpp>

#include "aura/Core/AuraMath.h"
#include "aura/Core/Profiling/FrameProfiler.h"
#include "aura/Renderer/OpenGL/EmbeddedGlsl.h"
#include "aura/Renderer/OpenGL/GlAura/GlShaderManager/GlShaderManager.h"
#include "aura/aura.h"

namespace aura3d
{
namespace gl
{

OpenGLRenderer::OpenGLRenderer(const wma::WindowDetails &windowDetails) : IRenderer(windowDetails)
{
    INK_INFO << "Renderer - OPENGL";
}

OpenGLRenderer::~OpenGLRenderer()
{
    cleanup();
}

namespace
{

//! gladLoadGLLoader takes a plain function pointer, so the window rides in here for the call.
const wma::IWindowManager *loaderWindow = nullptr;

void *loadGLProc(const char *name)
{
    return loaderWindow->getGLProcAddress(name);
}

} // namespace

void OpenGLRenderer::initialize(AuraSettings *settings, const JobSystem *jobs)
{
    if (_isInitialized)
        return;

    //! Unused: this backend has no CPU-side worker pool of its own to share.
    (void)jobs;

    createWindow(settings->getWindowTitle().c_str(), settings->getWindowBackend());
    loadOpenGLEntryPoints();

    _vertexMgr = std::make_unique<GlVertexBufferManager>();
    _indexMgr = std::make_unique<GlIndexBufferManager>();
    _uniformMgr = std::make_unique<GlUniformBufferManager>();
    _textureMgr = std::make_unique<GlTextureManager>();
    _targetMgr = std::make_unique<GlTargetManager>();
    _batchMgr = std::make_unique<GlBatchManager>();

    createSceneProgram();
    _uniformMgr->create(_sceneProgram);
    //! Sampled by untextured batches and invalid scene bindings, as on the other backends.
    _whiteTexture = _textureMgr->createSolidColorTexture(255, 255, 255, 255);
    _batchMgr->create(*_textureMgr, _whiteTexture);
    _targetMgr->create();
    handleWindowChanges();

    //! Only batches blend, always as straight alpha, so the factors are set once.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    _isInitialized = true;
}

void OpenGLRenderer::loadOpenGLEntryPoints()
{
    //! Every wma backend creates the context with the window and leaves it current.
    if (!_windowManagerApi || !_windowManagerApi->getWindowInstance())
        throw std::runtime_error("OpenGLRenderer: no OpenGL window to load entry points from");

    loaderWindow = _windowManagerApi.get();
    const int loaded = gladLoadGLLoader(loadGLProc);
    loaderWindow = nullptr;
    if (!loaded)
        throw std::runtime_error("OpenGLRenderer: gladLoadGLLoader failed");

    INK_INFO << "OpenGL " << GLVersion.major << "." << GLVersion.minor << " | "
             << reinterpret_cast<const char *>(glGetString(GL_RENDERER));
}

void OpenGLRenderer::createSceneProgram()
{
    _sceneProgram = GlShaderManager::createProgram(gl_shader3d_vert, gl_shader3d_frag);
    _modelLoc = glGetUniformLocation(_sceneProgram, "uModel");
    _normalLoc = glGetUniformLocation(_sceneProgram, "uNormal");
    _coverageLoc = glGetUniformLocation(_sceneProgram, "coverageOnly");
    //! Sampler uniforms are program state, so unit 0 is set once.
    glUseProgram(_sceneProgram);
    glUniform1i(glGetUniformLocation(_sceneProgram, "textureSampler"), 0);
}

void OpenGLRenderer::createWindow(const char *title, const wma::WindowBackend &wBackend)
{
    _windowManagerApi = makeWindow(wBackend, _windowDetails, wma::GraphicsAPI::OpenGL);
    _windowManagerApi->createWindow(title);
}

void OpenGLRenderer::handleWindowChanges()
{
    if (!_targetMgr)
        return;
    //! Pixels, not the logical size: the two differ on HiDPI displays.
    const wma::FramebufferSize size = _windowManagerApi->getFramebufferSize();
    _targetMgr->resize({static_cast<u32>(size.width), static_cast<u32>(size.height)});
    _textureMgr->invalidateBindings();
}

void OpenGLRenderer::cleanup()
{
    clearSharedResources();

    //! The managers own the GL objects, so they go while the context is still current.
    _batchMgr.reset();
    _targetMgr.reset();
    _vertexMgr.reset();
    _indexMgr.reset();
    _uniformMgr.reset();
    _textureMgr.reset();
    if (_sceneProgram)
        glDeleteProgram(_sceneProgram);
    _sceneProgram = 0;
    _modelLoc = _normalLoc = _coverageLoc = _coverage = -1;
    _modelUploaded = false;
    _sceneState = false;
    //! The texture pool died with _textureMgr, so drop the cached handle.
    _whiteTexture = {};
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
    flushBatches();
    if (_textureMgr)
        _textureMgr->updateCoverageRegion(handle, x, y, width, height, coverage);
}

void OpenGLRenderer::updateTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height,
                                         const u8 *rgbaPixels)
{
    flushBatches();
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

    _targetMgr->bind();
    //! Depth writes must be on for the clear to reach the depth buffer.
    useSceneState();
    glClearColor(_clearR, _clearG, _clearB, _clearA);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    _uniformMgr->updateCamera(_currentTransform);
    _uniformMgr->updateLight(_light);
}

void OpenGLRenderer::endRenderPass()
{
    AURA_FRAME_SCOPE(FramePhase::EndPass);

    if (!_vertexMgr)
        return;

    flushBatches();
    _targetMgr->resolve();
    leaveSceneState();
    _textureMgr->invalidateBindings();
}

void OpenGLRenderer::endFrame()
{
    /*
     * Present is where a vsynced GL frame spends its wall time, so the pipeline's
     * backpressure lands on this one scope. WaitFence/Acquire/Submit stay at zero:
     * GL has no counterpart to any of them. Inside run() the window manager's loop
     * swaps right after this returns; a second swap here would show each frame
     * twice and halve a vsynced rate.
     */
    if (_windowManagerApi && !_running)
    {
        AURA_FRAME_SCOPE(FramePhase::Present);
        _windowManagerApi->swapBuffers();
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
    //! The model reaches the program at draw time; the camera upload skips an unchanged one.
    if (_uniformMgr)
        _uniformMgr->updateCamera(_currentTransform);
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

    //! A VAO switch carries the element-array binding with it, so the index manager's
    //! cache stops describing reality; bind() reports exactly that case.
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
    //! Otherwise the switch back to scene state binds it.
    if (_sceneState)
        bindSceneTexture();
}

void OpenGLRenderer::bindSceneTexture()
{
    const GlTextureData *data = _textureMgr->bind(_currentTexture, 0);
    if (!data)
        data = _textureMgr->bind(_whiteTexture, 0);
    const GLint coverage = data && data->coverageOnly ? 1 : 0;
    if (_coverage != coverage)
    {
        glUniform1i(_coverageLoc, coverage);
        _coverage = coverage;
    }
}

void OpenGLRenderer::useSceneState()
{
    flushBatches();
    if (_sceneState)
        return;

    _sceneState = true;
    glUseProgram(_sceneProgram);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    //! Another VAO replaced the scene's, along with its element-array binding.
    _vertexMgr->invalidateBinding();
    if (!_vertexMgr->bind(_currentVertexBuffer))
        _vertexMgr->unbind();
    _indexMgr->invalidateBinding();
    _indexMgr->bind(_currentIndexBuffer);
    bindSceneTexture();
}

void OpenGLRenderer::leaveSceneState()
{
    _sceneState = false;
    _vertexMgr->invalidateBinding();
    _indexMgr->invalidateBinding();
}

void OpenGLRenderer::flushBatches()
{
    if (_batchMgr && _batchMgr->flush())
        leaveSceneState();
}

void OpenGLRenderer::uploadModel()
{
    const glm::mat4 &model = _currentTransform.model;
    if (_modelUploaded && model == _uploadedModel)
        return;

    glUniformMatrix4fv(_modelLoc, 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix3fv(_normalLoc, 1, GL_FALSE, glm::value_ptr(gfx::normalMatrixOf(model)));
    _uploadedModel = model;
    _modelUploaded = true;
}

void OpenGLRenderer::drawIndexed(u32 indexCount, u32 instanceCount)
{
    auto *idxData = _indexMgr->get(_currentIndexBuffer);
    if (!idxData)
        return;

    useSceneState();
    uploadModel();
    if (instanceCount > 1)
        glDrawElementsInstanced(GL_TRIANGLES, indexCount, idxData->type, nullptr, instanceCount);
    else
        glDrawElements(GL_TRIANGLES, indexCount, idxData->type, nullptr);
}

void OpenGLRenderer::draw(u32 vertexCount, u32 instanceCount)
{
    useSceneState();
    uploadModel();
    if (instanceCount > 1)
        glDrawArraysInstanced(GL_TRIANGLES, 0, vertexCount, instanceCount);
    else
        glDrawArrays(GL_TRIANGLES, 0, vertexCount);
}

glm::uvec2 OpenGLRenderer::renderTargetSize() const noexcept
{
    return _targetMgr ? _targetMgr->size() : glm::uvec2{0};
}

void OpenGLRenderer::drawBatch(std::span<const gfx::BatchVertex> vertices, std::span<const u32> indices,
                               TextureHandle texture, gfx::BatchSpace space)
{
    AURA_FRAME_SCOPE(space == gfx::BatchSpace::Screen ? FramePhase::RecordOverlay : FramePhase::RecordScene);

    if (_batchMgr && !vertices.empty() && !indices.empty() &&
        _batchMgr->draw(vertices, indices, texture, batchTransform(space)))
        leaveSceneState();
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
