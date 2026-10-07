#ifndef OPENGL_RENDERER_H
#define OPENGL_RENDERER_H

#pragma once

#include <memory>

#include "aura/Renderer/IRenderer.h"
#include "aura/Renderer/OpenGL/GlAura/GlBatchManager/GlBatchManager.h"
#include "aura/Renderer/OpenGL/GlAura/GlIndexBufferManager/GlIndexBufferManager.h"
#include "aura/Renderer/OpenGL/GlAura/GlTargetManager/GlTargetManager.h"
#include "aura/Renderer/OpenGL/GlAura/GlTextureManager/GlTextureManager.h"
#ifdef AURA_PROFILE_FRAME
#include "aura/Renderer/OpenGL/GlAura/GlTimerQuery/GlTimerQuery.h"
#endif
#include "aura/Renderer/OpenGL/GlAura/GlUniformBufferManager/GlUniformBufferManager.h"
#include "aura/Renderer/OpenGL/GlAura/GlVertexBufferManager/GlVertexBufferManager.h"

namespace aura3d
{
namespace gl
{

class OpenGLRenderer : public IRenderer
{
  public:
    OpenGLRenderer(const wma::WindowDetails &windowDetails);
    virtual ~OpenGLRenderer();

    void initialize(AuraSettings *settings, const JobSystem *jobs) override;
    void handleWindowChanges() override;
    void cleanup() override;

    VertexBufferHandle createVertexBuffer(std::vector<gfx::Vertex3D> &&vertices) override;
    IndexBufferHandle createIndexBuffer(std::vector<u16> &&indices) override;
    IndexBufferHandle createIndexBuffer(std::vector<u32> &&indices) override;
    TextureHandle createSolidColorTexture(u8 r, u8 g, u8 b, u8 a = 255) override;
    TextureHandle createTextureFromPixels(const u8 *rgbaPixels, u32 width, u32 height) override;
    TextureHandle createDynamicTexture(u32 width, u32 height) override;
    TextureHandle createCoverageTexture(u32 width, u32 height) override;
    void updateTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height, const u8 *rgbaPixels) override;
    void updateCoverageTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height,
                                     const u8 *coverage) override;

    void beginFrame() override;
    void beginRenderPass() override;
    void endRenderPass() override;
    void endFrame() override;

    void setTransform(const gfx::TransformUBO &ubo) override;
    void setLight(const gfx::LightUBO &light) override;
    void bindVertexBuffer(VertexBufferHandle handle) override;
    void bindIndexBuffer(IndexBufferHandle handle) override;
    void bindTexture(TextureHandle handle) override;
    void drawIndexed(u32 indexCount, u32 instanceCount = 1) override;
    void draw(u32 vertexCount, u32 instanceCount = 1) override;
    using IRenderer::drawBatch;
    void drawBatch(std::span<const gfx::BatchVertex> vertices, std::span<const u32> indices, TextureHandle texture,
                   gfx::BatchSpace space = gfx::BatchSpace::Screen) override;
    [[nodiscard]] glm::uvec2 renderTargetSize() const noexcept override;
    void setClearColor(f32 r, f32 g, f32 b, f32 a = 1.0f) override;
#ifdef AURA_PROFILE_FRAME
    [[nodiscard]] GpuTimingStats gpuTiming() const noexcept override;
#endif

    wma::IWindowManager *getWindowManager() override
    {
        return _windowManagerApi.get();
    }
    RendererChoice getBackendType() const override
    {
        return RendererChoice::OPENGL;
    }

  protected:
    void createWindow(const char *title, const wma::WindowBackend &wBackend) override;

  private:
    void loadOpenGLEntryPoints();
    void createSceneProgram();

    //! Scene state is the scene program, depth test and writes, no blending, and the scene's
    //! VAO and texture. Batches and the resolve replace it; the next scene draw restores it.
    void useSceneState();
    void leaveSceneState();
    //! Draws the staged batches; call before anything that must see them.
    void flushBatches();
    //! Binds _currentTexture, white for an invalid handle, and the scene's coverage flag.
    void bindSceneTexture();
    //! Gives the scene program the current model and its normal matrix, if they changed.
    void uploadModel();

    std::unique_ptr<wma::IWindowManager> _windowManagerApi;
    std::unique_ptr<GlVertexBufferManager> _vertexMgr;
    std::unique_ptr<GlIndexBufferManager> _indexMgr;
    std::unique_ptr<GlUniformBufferManager> _uniformMgr;
    std::unique_ptr<GlTextureManager> _textureMgr;
    std::unique_ptr<GlTargetManager> _targetMgr;
    std::unique_ptr<GlBatchManager> _batchMgr;
#ifdef AURA_PROFILE_FRAME
    //! Created by the first pass that runs with GPU timing on.
    std::unique_ptr<GlTimerQuery> _gpuTimer;
    bool _gpuTimerUnsupported = false;
#endif

    bool _isInitialized = false;
    bool _sceneState = false;

    GLuint _sceneProgram = 0;
    GLint _modelLoc = -1;
    GLint _normalLoc = -1;
    GLint _coverageLoc = -1;
    //! What the scene program last received; unset until the first draw.
    glm::mat4 _uploadedModel{0.0f};
    bool _modelUploaded = false;
    GLint _coverage = -1;
    TextureHandle _whiteTexture;

    VertexBufferHandle _currentVertexBuffer;
    IndexBufferHandle _currentIndexBuffer;
    TextureHandle _currentTexture;
    f32 _clearR = 0.05f, _clearG = 0.05f, _clearB = 0.05f, _clearA = 1.0f;
};

} // namespace gl
} // namespace aura3d

#endif // OPENGL_RENDERER_H
