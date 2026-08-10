#include "aura/Renderer/OpenGL/OpenGLRenderer.h"

#include <glad/glad.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <stdexcept>
#include <thread>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/type_ptr.hpp>

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

void OpenGLRenderer::initialize(AuraSettings* settings)
{
    if (_isInitialized) return;

    createWindow(settings->getWindowTitle().c_str(), settings->getWindowBackend());
    loadOpenGLEntryPoints();
    compileBuiltInShaders();

    _vertexMgr  = std::make_unique<GlVertexBufferManager>();
    _indexMgr   = std::make_unique<GlIndexBufferManager>();
    _uniformMgr = std::make_unique<GlUniformBufferManager>();
    _textureMgr = std::make_unique<GlTextureManager>();

    _uniformMgr->create(_shaderProgram);
    createOverlay2DBuffers();

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

    // wma's SDL backend already creates and makes current a GL context for
    // the OpenGL path (SdlWindowManager::createWindow), so the common case
    // here is "reuse what's already current" -- only create + MakeCurrent
    // ourselves if nothing is current yet. A redundant second MakeCurrent
    // call on an already-current context fails outright under Emscripten's
    // SDL3 port (SDL_GetError() comes back empty, unlike a real GL error).
    SDL_GLContext context = SDL_GL_GetCurrentContext();
    if (!context) {
        context = SDL_GL_CreateContext(window);
        if (!context) {
            throw std::runtime_error(
                std::string("OpenGLRenderer: failed to create GL context: ") + SDL_GetError());
        }

        if (SDL_GL_MakeCurrent(window, context) != 0) {
            throw std::runtime_error(
                std::string("OpenGLRenderer: failed to make GL context current: ") + SDL_GetError());
        }
    }

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        throw std::runtime_error("OpenGLRenderer: gladLoadGLLoader failed");
    }

    INK_INFO << "OpenGL " << GLVersion.major << "." << GLVersion.minor
             << " | " << reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    handleWindowChanges();
}

namespace {

//! Compiles one stage, logging and returning 0 on failure.
GLuint compileShaderStage(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        INK_ERROR << "GL shader compile error: " << log;
    }
    return shader;
}

//! Compiles and links a vertex/fragment pair into a program.
GLuint linkShaderProgram(const char* vertexSource, const char* fragmentSource)
{
    const GLuint vert = compileShaderStage(GL_VERTEX_SHADER, vertexSource);
    const GLuint frag = compileShaderStage(GL_FRAGMENT_SHADER, fragmentSource);

    const GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        INK_ERROR << "GL shader link error: " << log;
    }

    //! The program keeps its own copy once linked, so the stages can go now.
    glDeleteShader(vert);
    glDeleteShader(frag);
    return program;
}

} // namespace

void OpenGLRenderer::compileBuiltInShaders()
{
    _shaderProgram = linkShaderProgram(GL_VERTEX_3D, GL_FRAGMENT_3D);

    /*
     * Point the 3D sampler at texture unit 0 once, here. Sampler uniforms are
     * part of the program object and survive until it is relinked, so there is
     * nothing to re-assert per draw -- the renderer binds every texture to
     * unit 0 and never moves it.
     */
    _sampler3DLoc = glGetUniformLocation(_shaderProgram, "textureSampler");
    if (_sampler3DLoc >= 0)
    {
        glUseProgram(_shaderProgram);
        glUniform1i(_sampler3DLoc, 0);
    }

    //! The overlay pipeline is a second, entirely separate program: unlit, no
    //! light block, and its projection supplied per batch rather than per frame.
    _overlay2DProgram = linkShaderProgram(GL_VERTEX_2D, GL_FRAGMENT_2D);
    _overlay2DProjLoc = glGetUniformLocation(_overlay2DProgram, "uProj");
    _overlay2DSamplerLoc = glGetUniformLocation(_overlay2DProgram, "textureSampler");

    //! Same reasoning for the overlay program's sampler.
    if (_overlay2DSamplerLoc >= 0)
    {
        glUseProgram(_overlay2DProgram);
        glUniform1i(_overlay2DSamplerLoc, 0);
    }

    glUseProgram(_shaderProgram);
}

void OpenGLRenderer::createOverlay2DBuffers()
{
    glGenVertexArrays(1, &_overlay2DVao);
    glGenBuffers(1, &_overlay2DVbo);
    glGenBuffers(1, &_overlay2DEbo);

    glBindVertexArray(_overlay2DVao);
    glBindBuffer(GL_ARRAY_BUFFER, _overlay2DVbo);

    //! Layout must match gfx::Vertex2D and the 2D shader's input locations.
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(gfx::Vertex2D),
                          reinterpret_cast<void*>(offsetof(gfx::Vertex2D, pos)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(gfx::Vertex2D),
                          reinterpret_cast<void*>(offsetof(gfx::Vertex2D, texCoord)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(gfx::Vertex2D),
                          reinterpret_cast<void*>(offsetof(gfx::Vertex2D, color)));
    glEnableVertexAttribArray(2);

    //! The element buffer binding is VAO state, so bind it while the VAO is
    //! current and it is restored automatically on every later bind.
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _overlay2DEbo);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
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
    clearSharedResources();

    if (_shaderProgram) glDeleteProgram(_shaderProgram);
    _shaderProgram = 0;

    if (_overlay2DProgram) glDeleteProgram(_overlay2DProgram);
    if (_overlay2DVao) glDeleteVertexArrays(1, &_overlay2DVao);
    if (_overlay2DVbo) glDeleteBuffers(1, &_overlay2DVbo);
    if (_overlay2DEbo) glDeleteBuffers(1, &_overlay2DEbo);
    _overlay2DProgram = 0;
    _overlay2DVao = _overlay2DVbo = _overlay2DEbo = 0;
    _overlay2DVboBytes = _overlay2DEboBytes = 0;
    _overlay2DProjLoc = _overlay2DSamplerLoc = _sampler3DLoc = -1;
    //! The texture pool dies with _textureMgr below, so drop the cached handle.
    _white2DTexture = INVALID_HANDLE;

    _vertexMgr.reset();
    _indexMgr.reset();
    _uniformMgr.reset();
    _textureMgr.reset();
    _windowManagerApi.reset();
    _isInitialized = false;
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

TextureHandle OpenGLRenderer::createTextureFromPixels(const u8* rgbaPixels, u32 width, u32 height)
{
    return _textureMgr->createTextureFromPixels(rgbaPixels, width, height);
}

TextureHandle OpenGLRenderer::createDynamicTexture(u32 width, u32 height)
{
    if (!_textureMgr) return INVALID_HANDLE;
    return _textureMgr->createDynamicTexture(width, height);
}

void OpenGLRenderer::updateTextureRegion(TextureHandle handle, u32 x, u32 y,
                                         u32 width, u32 height, const u8* rgbaPixels)
{
    if (!_textureMgr) return;
    _textureMgr->updateRegion(handle, x, y, width, height, rgbaPixels);
}

void OpenGLRenderer::beginFrame()
{
    auto* wd = _windowManagerApi->getWindowDetails();
    auto* flags = _windowManagerApi->getWindowFlags();

    if (flags->resized) {
        flags->resized = false;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        handleWindowChanges();
    }
}

void OpenGLRenderer::beginRenderPass()
{
    //! cleanup() can run mid-frame (the ESC key action calls it), which drops
    //! the managers while the frame loop is still executing.
    if (!_uniformMgr) 
        return;

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
    if (!_vertexMgr) 
        return;

    _vertexMgr->unbind();
    glFlush();
}

void OpenGLRenderer::endFrame()
{
    if (!_windowManagerApi) return;

    auto* window = static_cast<SDL_Window*>(_windowManagerApi->getWindowInstance());
    if (window) {
        SDL_GL_SwapWindow(window);
    }
}

void OpenGLRenderer::setTransform(const gfx::TransformUBO& ubo)
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

void OpenGLRenderer::setLight(const gfx::LightUBO& light)
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
    _textureMgr->bind(handle, 0);

    /*
     * The sampler uniform is *program* state: it keeps its value until the
     * program is relinked, so it is set once at link time (see
     * compileBuiltInShaders) rather than re-asserted here.
     */
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

void OpenGLRenderer::drawBatch2D(std::span<const gfx::Vertex2D> vertices,
                                 std::span<const u32> indices,
                                 TextureHandle texture)
{
    if (!_overlay2DProgram || !_textureMgr || vertices.empty() || indices.empty())
        return;

    //! An untextured batch still samples, so stand in an opaque white texel and
    //! let the vertex colour come through unchanged.
    TextureHandle sampled = texture;
    if (!isValidHandle(sampled)) {
        if (!isValidHandle(_white2DTexture))
            _white2DTexture = _textureMgr->createSolidColorTexture(255, 255, 255, 255);
        sampled = _white2DTexture;
    }

    const wma::WindowDetails* wd = _windowManagerApi->getWindowDetails();
    const f32 width = static_cast<f32>(wd->width);
    const f32 height = static_cast<f32>(wd->height);
    if (width <= 0.0f || height <= 0.0f)
        return;

    /*
     * Window pixels -> clip space. Passing height as `bottom` and 0 as `top`
     * inverts the Y axis, which is what puts pixel (0,0) at the top-left corner
     * even though GL's NDC grows upwards. _NO because this is the desktop/ES
     * [-1,1] depth convention; the depth range is irrelevant here since the
     * overlay writes a constant z = 0 and depth testing is off.
     */
    const glm::mat4 projection = glm::orthoRH_NO(0.0f, width, height, 0.0f, -1.0f, 1.0f);

    glUseProgram(_overlay2DProgram);
    glUniformMatrix4fv(_overlay2DProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
    //! The sampler was pointed at unit 0 when the program was linked and is
    //! program state, so it needs no per-batch re-assertion.
    _textureMgr->bind(sampled, 0);

    /*
     * The overlay owns its VAO directly rather than going through
     * _vertexMgr, so the managers' bind caches cannot see this switch. Tell
     * them, or the next scene draw skips a VAO/EBO bind it genuinely needs and
     * renders the overlay's geometry with the scene's shader.
     */
    glBindVertexArray(_overlay2DVao);
    _vertexMgr->invalidateBinding();
    _indexMgr->invalidateBinding();

    glBindBuffer(GL_ARRAY_BUFFER, _overlay2DVbo);

    /*
     * Orphan then refill. Handing the driver a fresh store with a null pointer
     * lets it hand back new memory instead of stalling until the previous
     * frame's draw has finished reading the old one. The allocation only ever
     * grows, so a steady-state overlay settles after a few frames.
     */
    const size_t vertexBytes = vertices.size_bytes();
    _overlay2DVboBytes = std::max(_overlay2DVboBytes, vertexBytes);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(_overlay2DVboBytes), nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertexBytes), vertices.data());

    const size_t indexBytes = indices.size_bytes();
    _overlay2DEboBytes = std::max(_overlay2DEboBytes, indexBytes);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(_overlay2DEboBytes), nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(indexBytes), indices.data());

    /*
     * Overlay state: composite over whatever is already in the colour buffer,
     * and ignore depth entirely so the batch is never occluded by the scene.
     */
    const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    //! The whole batch in one call -- the point of the exercise.
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, nullptr);

    glDisable(GL_BLEND);
    if (depthWasEnabled)
        glEnable(GL_DEPTH_TEST);

    //! Hand the 3D program and VAO state back, so a following scene draw needs
    //! no knowledge that an overlay ran.
    _vertexMgr->unbind();
    glUseProgram(_shaderProgram);
}

void OpenGLRenderer::setClearColor(f32 r, f32 g, f32 b, f32 a)
{
    _clearR = r; _clearG = g; _clearB = b; _clearA = a;
}

} // namespace gl
} // namespace aura3d
