#include "aura/Renderer/OpenGL/OpenGLRenderer.h"

#include <glad/glad.h>
#include <stdexcept>

#include <glm/gtc/type_ptr.hpp>

#include "aura/Core/AuraMath.h"
#include "aura/Core/Files.h"
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

    createWindow(settings->windowTitle().c_str(), settings->windowBackend());
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

void OpenGLRenderer::setSamplerUnits(GLuint program)
{
    //! Sampler uniforms are program state, so the units are set once: albedo 0, then the material's maps.
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "textureSampler"), 0);
    glUniform1i(glGetUniformLocation(program, "normalSampler"), 1);
    glUniform1i(glGetUniformLocation(program, "metallicRoughnessSampler"), 2);
    glUniform1i(glGetUniformLocation(program, "emissiveSampler"), 3);
}

void OpenGLRenderer::uploadMaterial()
{
    //! Compared as handles plus a flag: an invalid handle's value is the largest u32, so no value is free
    //! to mean "nothing uploaded".
    if (_materialUploaded && _currentMaterialHandle == _uploadedMaterial)
        return;
    _uniformMgr->updateMaterial(materialUniforms(_currentMaterial));
    //! An absent map binds white, which its flag tells the shader to ignore.
    const std::array<TextureHandle, 3> maps{_currentMaterial.normalMap, _currentMaterial.metallicRoughness,
                                            _currentMaterial.emissiveMap};
    for (GLuint unit = 0; unit < maps.size(); ++unit)
        if (!_textureMgr->bind(maps[unit], unit + 1))
            _textureMgr->bind(_whiteTexture, unit + 1);
    _uploadedMaterial = _currentMaterialHandle;
    _materialUploaded = true;
}

void OpenGLRenderer::setLights(std::span<const gfx::Light> lights)
{
    IRenderer::setLights(lights);
    //! Uploaded now when a context exists, and again at every beginRenderPass, so lights set before
    //! initialize() are not lost.
    if (_uniformMgr)
        _uniformMgr->updateLight(lighting());
}

RendererCaps OpenGLRenderer::caps() const noexcept
{
#ifndef AURA_GLES
    const bool compute = hasCompute();
#else
    const bool compute = false;
#endif
    const RendererCaps caps =
        RendererCaps::Pbr | RendererCaps::Instancing | RendererCaps::RenderTargets | RendererCaps::CustomPipelines;
    return compute ? caps | RendererCaps::Compute : caps;
}

void OpenGLRenderer::createSceneProgram()
{
    buildScenePrograms(gl_shader3d_vert, gl_shader3d_instanced_vert, gl_shader3d_frag);
    glGenBuffers(1, &_instanceVbo);
}

void OpenGLRenderer::buildScenePrograms(const char *vert, const char *instancedVert, const char *frag)
{
    //! Both link before either replaces a running one, so a broken shader changes nothing.
    const GLuint scene = GlShaderManager::createProgram(vert, frag);
    GLuint instanced = 0;
    try
    {
        instanced = GlShaderManager::createProgram(instancedVert, frag);
    }
    catch (...)
    {
        glDeleteProgram(scene);
        throw;
    }
    for (const GLuint previous : {_sceneProgram, _instancedProgram})
        if (previous)
            glDeleteProgram(previous);

    _sceneProgram = scene;
    _modelLoc = glGetUniformLocation(_sceneProgram, "uModel");
    _normalLoc = glGetUniformLocation(_sceneProgram, "uNormal");
    _coverageLoc = glGetUniformLocation(_sceneProgram, "coverageOnly");
    _instancedProgram = instanced;
    _instancedCoverageLoc = glGetUniformLocation(_instancedProgram, "coverageOnly");
    //! Sampler units and block bindings are program state, so they are set once per program.
    for (const GLuint program : {_sceneProgram, _instancedProgram})
    {
        setSamplerUnits(program);
        GlUniformBufferManager::bindBlocks(program);
    }
    //! Uniform values died with the old programs.
    _sceneState = false;
    _modelUploaded = false;
    _materialUploaded = false;
    _coverage = -1;
}

bool OpenGLRenderer::reloadShaders(const std::filesystem::path &directory)
{
    if (!_isInitialized)
        return false;
    //! What cmake/EmbedShaderHeader.cmake does at build time: includes spliced in, and the GLES preamble.
    const auto source = [&](const char *name)
    {
        const auto read = [](const std::filesystem::path &path)
        {
            const auto bytes = readFileBytes(path);
            if (!bytes)
                throw std::runtime_error("cannot read " + path.string());
            std::string text(reinterpret_cast<const char *>(bytes->data()), bytes->size());
            std::erase(text, '\r');
            return text;
        };
        std::string text = read(directory / "opengl" / name);
        constexpr std::string_view kInclude = "#include \"";
        for (usize at = text.find(kInclude); at != std::string::npos; at = text.find(kInclude, at))
        {
            const usize close = text.find('"', at + kInclude.size());
            if (close == std::string::npos)
                break;
            const std::string body =
                read(directory / "common" / text.substr(at + kInclude.size(), close - at - kInclude.size()));
            text.replace(at, close + 1 - at, body);
            at += body.size();
        }
#ifdef AURA_GLES
        constexpr std::string_view kDesktop = "#version 330 core\n";
        if (text.starts_with(kDesktop))
            text.replace(0, kDesktop.size(), "#version 300 es\nprecision highp float;\n");
#endif
        return text;
    };
    try
    {
        const std::string vert = source("gl_shader3d.vert"), instancedVert = source("gl_shader3d_instanced.vert"),
                          frag = source("gl_shader3d.frag");
        buildScenePrograms(vert.c_str(), instancedVert.c_str(), frag.c_str());
        return true;
    }
    catch (const std::exception &error)
    {
        INK_ERROR << "OpenGLRenderer: shader reload failed, keeping the running shaders: " << error.what();
        return false;
    }
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
    _materialUploaded = false;
}

void OpenGLRenderer::cleanup()
{
    clearSharedResources();

    //! The managers own the GL objects, so they go while the context is still current.
#ifdef AURA_PROFILE_FRAME
    _gpuTimer.reset();
    _gpuTimerUnsupported = false;
#endif
    destroyCustomPipelines();
    if (_textureMgr)
        destroyRenderTargets();
    _batchMgr.reset();
    _targetMgr.reset();
    _vertexMgr.reset();
    _indexMgr.reset();
    _uniformMgr.reset();
    _textureMgr.reset();
    if (_sceneProgram)
        glDeleteProgram(_sceneProgram);
    _sceneProgram = 0;
    if (_instancedProgram)
        glDeleteProgram(_instancedProgram);
    if (_instanceVbo)
        glDeleteBuffers(1, &_instanceVbo);
#ifndef AURA_GLES
    for (const GlBuffer &buffer : _buffers)
        glDeleteBuffers(1, &buffer.id);
    for (const GLuint program : _computePrograms)
        glDeleteProgram(program);
    if (_computeConstants)
        glDeleteBuffers(1, &_computeConstants);
    _buffers.clear();
    _computePrograms.clear();
    _computeConstants = 0;
#endif
    _instancedProgram = _instanceVbo = 0;
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

bool OpenGLRenderer::updateVertexBuffer(VertexBufferHandle handle, std::span<const gfx::Vertex3D> vertices)
{
    const GlVertexBufferData *data = _vertexMgr ? _vertexMgr->get(handle) : nullptr;
    if (!data || vertices.size() > data->vertexCount)
        return false;
    //! GL orders the write after earlier draws itself; ARRAY_BUFFER is not VAO state, so nothing is disturbed.
    glBindBuffer(GL_ARRAY_BUFFER, data->vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertices.size_bytes()), vertices.data());
    return true;
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

TextureHandle OpenGLRenderer::createTexture(const TextureDesc &desc, std::span<const std::byte> pixels)
{
    if (desc.format == TextureFormat::Depth)
    {
        INK_ERROR << "OpenGLRenderer: TextureFormat::Depth is for render targets";
        return {};
    }
    return _textureMgr ? _textureMgr->createTexture(desc, pixels) : TextureHandle{};
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
    if (consumeResize())
        handleWindowChanges();
}

void OpenGLRenderer::beginRenderPass()
{
    AURA_FRAME_SCOPE(FramePhase::BeginPass);

    //! cleanup() can run mid-frame (the ESC key action calls it), which drops
    //! the managers while the frame loop is still executing.
    if (!_uniformMgr)
        return;

#ifdef AURA_PROFILE_FRAME
    if (_gpuTimingEnabled && !_gpuTimer && !_gpuTimerUnsupported)
    {
        auto timer = std::make_unique<GlTimerQuery>();
        _gpuTimerUnsupported = !timer->create();
        if (!_gpuTimerUnsupported)
            _gpuTimer = std::move(timer);
    }
    //! Before the target bind, so the clear is part of the measured frame.
    if (_gpuTimingEnabled && _gpuTimer)
        _gpuTimer->begin();
#endif

    _targetMgr->bind();
    _passTarget = 0;
    openPass();
}

void OpenGLRenderer::openPass()
{
    //! Depth writes must be on for the clear to reach the depth buffer.
    useSceneState();
    glClearColor(_clearR, _clearG, _clearB, _clearA);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    _uniformMgr->updateCamera(passTransform());
    _uniformMgr->updateLight(lighting());
}

void OpenGLRenderer::beginRenderPass(RenderTargetHandle target)
{
    if (!isValidHandle(target))
        return beginRenderPass();
    AURA_FRAME_SCOPE(FramePhase::BeginPass);
    if (!_uniformMgr || target.value() > _renderTargets.size())
        return;
    const RenderTarget &chosen = _renderTargets[target.value() - 1];
    glBindFramebuffer(GL_FRAMEBUFFER, chosen.fbo);
    glViewport(0, 0, static_cast<GLsizei>(chosen.size.x), static_cast<GLsizei>(chosen.size.y));
#ifndef AURA_GLES
    //! Linear targets store what the shaders write; an sRGB one encodes, like the window.
    if (chosen.srgb)
        glEnable(GL_FRAMEBUFFER_SRGB);
    else
        glDisable(GL_FRAMEBUFFER_SRGB);
#endif
    _passTarget = target.value();
    openPass();
}

glm::uvec2 OpenGLRenderer::targetSize(RenderTargetHandle target) const
{
    return isValidHandle(target) && target.value() <= _renderTargets.size() ? _renderTargets[target.value() - 1].size
                                                                            : glm::uvec2{0};
}

bool OpenGLRenderer::resizeRenderTarget(RenderTargetHandle handle, u32 width, u32 height)
{
    if (!_textureMgr || !isValidHandle(handle) || handle.value() > _renderTargets.size() || width == 0 || height == 0)
        return false;
    RenderTarget &target = _renderTargets[handle.value() - 1];
    if (target.size == glm::uvec2{width, height})
        return true;
    //! GL orders the respecification after every draw that used the old storage.
    const TextureHandle resized = target.format == TextureFormat::Depth
                                      ? _textureMgr->createDepthAttachment(width, height, target.colour)
                                      : _textureMgr->createAttachment(width, height, target.format, target.colour);
    if (!isValidHandle(resized))
        return false;
    if (target.depth)
    {
        glBindRenderbuffer(GL_RENDERBUFFER, target.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, static_cast<GLsizei>(width),
                              static_cast<GLsizei>(height));
    }
    target.size = {width, height};
    _textureMgr->invalidateBindings();
    return true;
}

RenderTargetHandle OpenGLRenderer::createDepthTarget(const RenderTargetDesc &desc)
{
    RenderTarget target;
    target.colour = _textureMgr->createDepthAttachment(desc.width, desc.height);
    if (!isValidHandle(target.colour))
        return {};
    target.size = {desc.width, desc.height};
    target.format = TextureFormat::Depth;

    glGenFramebuffers(1, &target.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, _textureMgr->get(target.colour)->texture,
                           0);
    //! Depth only: a colour draw buffer with nothing attached makes the framebuffer incomplete.
    const GLenum none = GL_NONE;
    glDrawBuffers(1, &none);
    glReadBuffer(GL_NONE);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!complete)
    {
        INK_ERROR << "OpenGLRenderer: depth target framebuffer is incomplete";
        glDeleteFramebuffers(1, &target.fbo);
        return {};
    }
    _renderTargets.push_back(target);
    return RenderTargetHandle{static_cast<u32>(_renderTargets.size())};
}

RenderTargetHandle OpenGLRenderer::createRenderTarget(const RenderTargetDesc &desc)
{
    if (!_textureMgr)
        return {};
    if (desc.format == TextureFormat::Depth)
        return createDepthTarget(desc);
    RenderTarget target;
    target.colour = _textureMgr->createAttachment(desc.width, desc.height, desc.format);
    if (!isValidHandle(target.colour))
        return {};
    target.size = {desc.width, desc.height};
    target.format = desc.format;
    target.srgb = desc.format == TextureFormat::Rgba8Srgb;

    glGenFramebuffers(1, &target.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           _textureMgr->get(target.colour)->texture, 0);
    if (desc.depth)
    {
        glGenRenderbuffers(1, &target.depth);
        glBindRenderbuffer(GL_RENDERBUFFER, target.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, static_cast<GLsizei>(desc.width),
                              static_cast<GLsizei>(desc.height));
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, target.depth);
    }
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!complete)
    {
        INK_ERROR << "OpenGLRenderer: render target framebuffer is incomplete";
        glDeleteFramebuffers(1, &target.fbo);
        if (target.depth)
            glDeleteRenderbuffers(1, &target.depth);
        return {};
    }
    _renderTargets.push_back(target);
    return RenderTargetHandle{static_cast<u32>(_renderTargets.size())};
}

TextureHandle OpenGLRenderer::renderTargetTexture(RenderTargetHandle target) const
{
    return isValidHandle(target) && target.value() <= _renderTargets.size() ? _renderTargets[target.value() - 1].colour
                                                                            : TextureHandle{};
}

bool OpenGLRenderer::readPixels(RenderTargetHandle target, std::span<u8> rgba)
{
    if (!isValidHandle(target) || target.value() > _renderTargets.size() ||
        _renderTargets[target.value() - 1].format == TextureFormat::Depth)
        return false;
    const RenderTarget &chosen = _renderTargets[target.value() - 1];
    const usize row = static_cast<usize>(chosen.size.x) * 4;
    if (rgba.size() != row * chosen.size.y)
        return false;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, chosen.fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    //! Target passes draw flipped (see passFlip()), so GL's first row is already the image's top.
    glReadPixels(0, 0, static_cast<GLsizei>(chosen.size.x), static_cast<GLsizei>(chosen.size.y), GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    return true;
}

glm::mat4 OpenGLRenderer::passFlip() const noexcept
{
    return _passTarget != 0 ? glm::scale(glm::mat4{1.0f}, glm::vec3{1.0f, -1.0f, 1.0f}) : glm::mat4{1.0f};
}

glm::mat4 OpenGLRenderer::clipCorrection(RenderTargetHandle target) const noexcept
{
    return isValidHandle(target) ? glm::scale(glm::mat4{1.0f}, glm::vec3{1.0f, -1.0f, 1.0f}) : glm::mat4{1.0f};
}

gfx::TransformUBO OpenGLRenderer::passTransform() const noexcept
{
    gfx::TransformUBO transform = _currentTransform;
    transform.proj = passFlip() * transform.proj;
    return transform;
}

void OpenGLRenderer::destroyRenderTargets() noexcept
{
    for (const RenderTarget &target : _renderTargets)
    {
        glDeleteFramebuffers(1, &target.fbo);
        if (target.depth)
            glDeleteRenderbuffers(1, &target.depth);
    }
    _renderTargets.clear();
    _passTarget = 0;
}

void OpenGLRenderer::endRenderPass()
{
    AURA_FRAME_SCOPE(FramePhase::EndPass);

    if (!_vertexMgr)
        return;

    flushBatches();
    //! A target pass ends in its own texture; only the window's is resolved to the screen.
    if (_passTarget == 0)
        _targetMgr->resolve();
    _passTarget = 0;
    leaveSceneState();
    _textureMgr->invalidateBindings();
    _materialUploaded = false;
#ifdef AURA_PROFILE_FRAME
    if (_gpuTimer)
        _gpuTimer->end();
#endif
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

#ifdef AURA_PROFILE_FRAME
GpuTimingStats OpenGLRenderer::gpuTiming() const noexcept
{
    return _gpuTimingEnabled && _gpuTimer ? _gpuTimer->stats() : GpuTimingStats{};
}
#endif

void OpenGLRenderer::setTransform(const gfx::TransformUBO &ubo)
{
    _currentTransform = ubo;
    //! The model reaches the program at draw time; the camera upload skips an unchanged one.
    if (_uniformMgr)
        _uniformMgr->updateCamera(passTransform());
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

void OpenGLRenderer::drawMeshInstanced(MeshHandle mesh, MaterialHandle material, std::span<const glm::mat4> models)
{
    AURA_FRAME_SCOPE(FramePhase::RecordScene);
    if (!_vertexMgr)
        return;
    if (isValidHandle(material))
        bindMaterial(material);
    const MeshRecord *record = getMesh(mesh);
    if (!record)
        return;
    const Frustum frustum = viewFrustum();
    _instanceScratch.clear();
    for (const glm::mat4 &model : models)
        if (!culled(*record, model, frustum))
            _instanceScratch.push_back({model, glm::mat4(gfx::normalMatrixOf(model))});
    if (_instanceScratch.empty())
        return;

    bindVertexBuffer(record->vertexBuffer);
    bindIndexBuffer(record->indexBuffer);
    const auto *indices = _indexMgr->get(record->indexBuffer);
    if (!indices)
        return;
    useSceneState();
    uploadMaterial();
    glUseProgram(_instancedProgram);
    glUniform1i(_instancedCoverageLoc, _coverage);

    //! Orphaned each call: the driver hands back fresh storage instead of waiting on the last draw.
    glBindBuffer(GL_ARRAY_BUFFER, _instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(_instanceScratch.size() * sizeof(Instance)),
                 _instanceScratch.data(), GL_STREAM_DRAW);
    attachInstances(_instanceVbo);
    glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(record->indexCount), indices->type, nullptr,
                            static_cast<GLsizei>(_instanceScratch.size()));
    detachInstances();
}

void OpenGLRenderer::attachInstances(GLuint buffer)
{
    //! Attached to the mesh's VAO for this draw only, and detached after.
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    for (GLuint column = 0; column < kInstanceColumns; ++column)
    {
        const usize offset = column < 4 ? offsetof(Instance, model) + column * sizeof(glm::vec4)
                                        : offsetof(Instance, normal) + (column - 4) * sizeof(glm::vec4);
        // NOLINTBEGIN(performance-no-int-to-ptr): GL takes buffer offsets as pointers.
        glVertexAttribPointer(kFirstInstanceAttribute + column, 4, GL_FLOAT, GL_FALSE, sizeof(Instance),
                              reinterpret_cast<const void *>(offset));
        // NOLINTEND(performance-no-int-to-ptr)
        glVertexAttribDivisor(kFirstInstanceAttribute + column, 1);
        glEnableVertexAttribArray(kFirstInstanceAttribute + column);
    }
}

void OpenGLRenderer::detachInstances()
{
    for (GLuint column = 0; column < kInstanceColumns; ++column)
        glDisableVertexAttribArray(kFirstInstanceAttribute + column);
    glUseProgram(_sceneProgram);
}

void OpenGLRenderer::drawIndexed(u32 indexCount, u32 instanceCount)
{
    auto *idxData = _indexMgr->get(_currentIndexBuffer);
    if (!idxData)
        return;

    useSceneState();
    uploadModel();
    uploadMaterial();
    const auto count = static_cast<GLsizei>(indexCount);
    if (instanceCount > 1)
        glDrawElementsInstanced(GL_TRIANGLES, count, idxData->type, nullptr, static_cast<GLsizei>(instanceCount));
    else
        glDrawElements(GL_TRIANGLES, count, idxData->type, nullptr);
}

void OpenGLRenderer::draw(u32 vertexCount, u32 instanceCount)
{
    useSceneState();
    uploadModel();
    uploadMaterial();
    const auto count = static_cast<GLsizei>(vertexCount);
    if (instanceCount > 1)
        glDrawArraysInstanced(GL_TRIANGLES, 0, count, static_cast<GLsizei>(instanceCount));
    else
        glDrawArrays(GL_TRIANGLES, 0, count);
}

glm::uvec2 OpenGLRenderer::renderTargetSize() const noexcept
{
    if (_passTarget != 0)
        return _renderTargets[_passTarget - 1].size;
    return _targetMgr ? _targetMgr->size() : glm::uvec2{0};
}

void OpenGLRenderer::drawBatch(std::span<const gfx::BatchVertex> vertices, std::span<const u32> indices,
                               TextureHandle texture, gfx::BatchSpace space)
{
    AURA_FRAME_SCOPE(space == gfx::BatchSpace::Screen ? FramePhase::RecordOverlay : FramePhase::RecordScene);

    if (_batchMgr && !vertices.empty() && !indices.empty() &&
        _batchMgr->draw(vertices, indices, texture, passFlip() * batchTransform(space)))
        leaveSceneState();
}

} // namespace gl
} // namespace aura3d
