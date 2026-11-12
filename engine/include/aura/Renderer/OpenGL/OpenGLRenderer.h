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
    bool updateVertexBuffer(VertexBufferHandle handle, std::span<const gfx::Vertex3D> vertices) override;
    IndexBufferHandle createIndexBuffer(std::vector<u16> &&indices) override;
    IndexBufferHandle createIndexBuffer(std::vector<u32> &&indices) override;
    TextureHandle createSolidColorTexture(u8 r, u8 g, u8 b, u8 a = 255) override;
    TextureHandle createTextureFromPixels(const u8 *rgbaPixels, u32 width, u32 height) override;
    TextureHandle createTexture(const TextureDesc &desc, std::span<const std::byte> pixels) override;
    void drawMeshInstanced(MeshHandle mesh, MaterialHandle material, std::span<const glm::mat4> models) override;
    TextureHandle createDynamicTexture(u32 width, u32 height) override;
    TextureHandle createCoverageTexture(u32 width, u32 height) override;
    void updateTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height, const u8 *rgbaPixels) override;
    void updateCoverageTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height,
                                     const u8 *coverage) override;

    void endRenderPass() override;
    RenderTargetHandle createRenderTarget(const RenderTargetDesc &desc) override;
    bool resizeRenderTarget(RenderTargetHandle target, u32 width, u32 height) override;
    [[nodiscard]] glm::uvec2 targetSize(RenderTargetHandle target) const override;
    [[nodiscard]] TextureHandle renderTargetTexture(RenderTargetHandle target) const override;
    bool readPixels(RenderTargetHandle target, std::span<u8> rgba) override;

    void setTransform(const gfx::TransformUBO &ubo) override;
    void setLights(std::span<const gfx::Light> lights) override;
    bool reloadShaders(const std::filesystem::path &directory) override;
    [[nodiscard]] RendererCaps caps() const noexcept override;
    PipelineHandle createPipeline(const PipelineDesc &desc) override;
    [[nodiscard]] PipelineLimits pipelineLimits() const noexcept override;
    void drawPipeline(const PipelineDraw &draw) override;
    [[nodiscard]] glm::mat4 clipCorrection(RenderTargetHandle target = {}) const noexcept override;
#ifndef AURA_GLES
    BufferHandle createBuffer(usize bytes, std::span<const std::byte> initial = {}) override;
    bool updateBuffer(BufferHandle buffer, usize offset, std::span<const std::byte> bytes) override;
    bool readBuffer(BufferHandle buffer, usize offset, std::span<std::byte> out) override;
    ComputeHandle createCompute(const ComputeDesc &desc) override;
    void dispatch(ComputeHandle kernel, std::span<const BufferHandle> buffers, glm::uvec3 groups,
                  std::span<const std::byte> constants = {}) override;
    void drawMeshInstancedIndirect(MeshHandle mesh, MaterialHandle material, BufferHandle instances,
                                   BufferHandle arguments, usize argumentOffset = 0) override;
#endif
    void bindVertexBuffer(VertexBufferHandle handle) override;
    void bindIndexBuffer(IndexBufferHandle handle) override;
    void bindTexture(TextureHandle handle) override;
    void drawIndexed(u32 indexCount, u32 instanceCount = 1) override;
    void draw(u32 vertexCount, u32 instanceCount = 1) override;
    using IRenderer::drawBatch;
    void drawBatch(std::span<const gfx::BatchVertex> vertices, std::span<const u32> indices, TextureHandle texture,
                   gfx::BatchSpace space = gfx::BatchSpace::Screen) override;
    [[nodiscard]] glm::uvec2 renderTargetSize() const noexcept override;
#ifdef AURA_PROFILE_FRAME
    [[nodiscard]] GpuTimingStats gpuTiming() const noexcept override;
#endif

    RendererChoice getBackendType() const override
    {
        return RendererChoice::OPENGL;
    }

  protected:
    bool acquireFrame() override;
    bool presentFrame() override;
    void openRenderPass() override;
    void openRenderPass(RenderTargetHandle target) override;
    void createWindow(const char *title, const wma::WindowBackend &wBackend) override;

  private:
    void loadOpenGLEntryPoints();
    void createSceneProgram();
    //! The lit and instanced programs from these sources, replacing the current ones only if both link.
    void buildScenePrograms(const char *vert, const char *instancedVert, const char *frag);

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

    std::unique_ptr<GlVertexBufferManager> _vertexMgr;
    std::unique_ptr<GlIndexBufferManager> _indexMgr;
    std::unique_ptr<GlUniformBufferManager> _uniformMgr;
    std::unique_ptr<GlTextureManager> _textureMgr;
    std::unique_ptr<GlTargetManager> _targetMgr;

    struct RenderTarget
    {
        GLuint fbo = 0;
        GLuint depth = 0; //! Renderbuffer; 0 for a target without depth.
        TextureHandle colour;
        glm::uvec2 size{0};
        TextureFormat format = TextureFormat::Rgba8Srgb;
        bool srgb = false;
    };
    std::vector<RenderTarget> _renderTargets; //! handle - 1 == index
    //! The target the open pass draws into, or 0 for the window.
    u32 _passTarget = 0;
    static void setSamplerUnits(GLuint program);
    //! The bound material's block and maps, when the material changed since the last draw.
    void uploadMaterial();
    MaterialHandle _uploadedMaterial;
    bool _materialUploaded = false;
    //! Clears and readies whichever framebuffer is bound for drawing.
    void openPass();
    //! A target pass flips Y: GL stores a framebuffer's bottom row first, every uploaded texture its top
    //! row, and a target is sampled like one of those.
    [[nodiscard]] glm::mat4 passFlip() const noexcept;
    [[nodiscard]] gfx::TransformUBO passTransform() const noexcept;
    void destroyRenderTargets() noexcept;
    //! drawMeshInstanced(): gl_shader3d with per-instance matrices, and the buffer they stream through.
    GLuint _instancedProgram = 0;
    GLint _instancedCoverageLoc = -1;
    GLuint _instanceVbo = 0;
    struct Instance
    {
        glm::mat4 model;
        glm::mat4 normal; //! Columns 0-2 used.
    };
    static_assert(sizeof(Instance) == sizeof(InstanceTransform), "drawMeshInstancedIndirect() reads Instance");
    std::vector<Instance> _instanceScratch;
    //! Instance attributes 4-10 (model, then three normal-matrix columns) from @p buffer, on the bound VAO.
    static constexpr GLuint kFirstInstanceAttribute = 4;
    static constexpr GLuint kInstanceColumns = 7;
    void attachInstances(GLuint buffer);
    //! Undoes attachInstances() and returns to the scene program.
    void detachInstances();
#ifndef AURA_GLES
    //! createBuffer()'s buffers (uniform blocks from GL 3.1) and createCompute()'s programs (GL 4.3); neither is on
    //! GLES or WebGL.
    struct GlBuffer
    {
        GLuint id = 0;
        usize size = 0;
    };
    std::vector<GlBuffer> _buffers; //! handle - 1 == index
    [[nodiscard]] const GlBuffer *bufferOf(BufferHandle handle) const noexcept;
    std::vector<GLuint> _computePrograms; //! handle - 1 == index
    GLuint _computeConstants = 0;         //! The constants' std140 block, at uniform binding 3.
    [[nodiscard]] bool hasCompute() const noexcept;
#endif
    //! createPipeline()'s programs. Constants share one block at kPipelineConstantsBinding; BufferN follows it.
    struct CustomPipeline
    {
        GLuint program = 0;
        u32 textures = 0;
        u32 buffers = 0;
        u32 constantBytes = 0;
        VertexInput input = VertexInput::Mesh;
        PipelineState state = PipelineState::None;
    };
    static constexpr GLuint kPipelineConstantsBinding = 4;
    std::vector<CustomPipeline> _customPipelines; //! handle - 1 == index
    GLuint _pipelineConstants = 0;                //! The constants' std140 block.
    u32 _pipelineConstantsSize = 0;               //! Its size: the largest constantBytes of any pipeline.
    std::vector<std::byte> _pipelineScratch;      //! drawPipeline()'s constants, padded to the pipeline's size.
    GLuint _emptyVao = 0;                         //! Bound for a VertexInput::None draw: core profiles need one.
    void destroyCustomPipelines() noexcept;
    //! A TextureFormat::Depth target: its depth texture is RenderTarget::colour, and the framebuffer has no colour.
    RenderTargetHandle createDepthTarget(const RenderTargetDesc &desc);
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
};

} // namespace gl
} // namespace aura3d

#endif // OPENGL_RENDERER_H
