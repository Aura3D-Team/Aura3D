#ifndef IRENDERER_H
#define IRENDERER_H

#pragma once

#include <algorithm>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <wma/wma.hpp>

#include "aura/Platform/PlatformCompat.h"

#include "aura/Core/AuraCore.h"
#include "aura/Core/AuraSettings/AuraSettings.h"
#include "aura/Core/Culling.h"
#include "aura/Core/Profiling/GpuTiming.h"
#include "aura/Renderer/Canvas.h"
#include "aura/Renderer/Compute.h"
#include "aura/Renderer/Material.h"
#include "aura/Renderer/Pipeline.h"
#include "aura/Renderer/RenderHandles.h"
#include "aura/Renderer/Texture.h"

//! Forward-declared, not included: only initialize()'s parameter type needs
//! the name, and every backend but the software rasteriser ignores it, so
//! there is no reason to pull JobSystem.h into a header this widely included.
namespace aura3d
{
class JobSystem;
}

//! Likewise: gpuDebugSource() returns a pointer, so the interface's definition
//! is only needed by the backends that implement it and by the report writer.
namespace aura3d
{
class IGpuDebugSource;
}

/**
 * @brief List of available graphics backend renderers.
 * Uses X-Macros for synchronized enum and string conversions.
 */
#define RENDERER_LIST                                                                                                  \
    X(SOFTWARE)                                                                                                        \
    X(OPENGL)                                                                                                          \
    X(VULKAN)                                                                                                          \
    X(METAL)

namespace aura3d
{

/**
 * @brief Strongly-typed enum representing the chosen graphics API backend.
 */
enum class RendererChoice
{
#define X(name) name,
    RENDERER_LIST
#undef X
};

/**
 * @brief Converts a string representation to a RendererChoice enum.
 * * Case-insensitive. Also maps "CPU" to RendererChoice::SOFTWARE.
 * * @param[in] s The input string to parse.
 * @param[out] out The destination enum variable.
 * @return true If the string matches a known backend, false otherwise.
 */
inline bool RendererChoiceFromString(const std::string &s, RendererChoice &out)
{
    std::string up;
    up.reserve(s.size());
    for (const char c : s)
        up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

#define X(name)                                                                                                        \
    if (up == #name)                                                                                                   \
    {                                                                                                                  \
        out = RendererChoice::name;                                                                                    \
        return true;                                                                                                   \
    }
    RENDERER_LIST
#undef X

    if (up == "CPU")
    {
        out = RendererChoice::SOFTWARE;
        return true;
    }

    return false;
}

/**
 * @brief Converts a RendererChoice enum value to its exact string representation.
 * * @param[in] c The renderer enum value.
 * @return const char* A static string literal matching the enum identifier, or "UNKNOWN".
 */
inline const char *RendererChoiceToString(RendererChoice c)
{
    switch (c)
    {
    case RendererChoice::SOFTWARE:
        return "SOFTWARE";
    case RendererChoice::OPENGL:
        return "OPENGL";
    case RendererChoice::VULKAN:
        return "VULKAN";
    case RendererChoice::METAL:
        return "METAL";
    }
    return "UNKNOWN";
}

/**
 * @class IRenderer
 * @brief Pure virtual interface defining a cross-platform graphics rendering context.
 * * Serves as the abstraction layer for resource allocation, windowing setups,
 * pipeline bindings, and frame cycle management regardless of the targeted backend API.
 */
/// What a backend supports beyond the API every backend has.
enum class RendererCaps : u8
{
    None = 0,
    Pbr = 1u << 0u,           //!< Material maps, metallic-roughness shading, point and spot lights.
    Instancing = 1u << 1u,    //!< drawMeshInstanced() is one instanced draw rather than a loop.
    RenderTargets = 1u << 2u, //!< createRenderTarget().
    Compute = 1u << 3u,       //!< Buffers, dispatch() and drawMeshInstancedIndirect(); software runs ComputeDesc::cpu.
    CustomPipelines = 1u << 4u, //!< createPipeline() and drawPipeline(): your own shaders.
};
INK_ENUM_FLAGS(RendererCaps) // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)

class IRenderer
{
  public:
    /**
     * @brief Constructs the base IRenderer instance.
     * * @param[in] windowDetails Struct containing initial parameters like size and title.
     */
    IRenderer(const wma::WindowDetails &windowDetails) : _windowDetails(windowDetails)
    {
    }

    /**
     * @brief Virtual destructor ensuring safe polymorphic cleanups.
     */
    virtual ~IRenderer() = default;

    /**
     * @brief Configures underlying graphics libraries and hardware contexts using engine settings.
     * * @param[in] settings Pointer to the foundational application runtime configuration.
     * @param[in] jobs The engine's shared worker pool (see JobSystem), constructed before any
     *            renderer and outliving every backend switch. Only the software rasteriser uses
     *            it today -- to dispatch rasterisation and presentation across the same pool
     *            JobSystem already owns rather than building one of its own -- but it is handed
     *            to every backend uniformly so that stays an implementation detail of CPURenderer,
     *            not a special case Engine has to know about.
     */
    virtual void initialize(AuraSettings *settings, const JobSystem *jobs) = 0;

    /**
     * @brief Refreshes viewport contexts and internal buffers following user sizing adjustments.
     */
    virtual void handleWindowChanges() = 0;

    /**
     * @brief Deallocates remaining graphics pipeline systems before shutdown.
     */
    virtual void cleanup() = 0;

    /**
     * @brief Generates a GPU vertex buffer optimized for 3D configurations.
     * @param[in] vertices Array container of target 3D structural coordinate pieces.
     * @return VertexBufferHandle Opaque pointer abstraction for pipeline binding.
     */
    virtual VertexBufferHandle createVertexBuffer(std::vector<gfx::Vertex3D> &&vertices) = 0;
    //! Overwrites @p handle's vertices from the start; updateMesh() has checked the count fits.
    virtual bool updateVertexBuffer(VertexBufferHandle handle, std::span<const gfx::Vertex3D> vertices);

    /**
     * @brief Allocates an indexed layout element buffer using 16-bit short tags.
     * @param[in] indices Array element list containing sequential mesh connections.
     * @return IndexBufferHandle Opaque identifier representation for drawing instructions.
     */
    virtual IndexBufferHandle createIndexBuffer(std::vector<u16> &&indices) = 0;

    /**
     * @brief Allocates an indexed layout element buffer using 32-bit unsigned identifiers.
     * @param[in] indices Array element list containing sequential mesh connections.
     * @return IndexBufferHandle Opaque identifier representation for drawing instructions.
     */
    virtual IndexBufferHandle createIndexBuffer(std::vector<u32> &&indices) = 0;

    /**
     * @brief Deploys a unified, solid-color monochromatic texture block directly to the GPU.
     * @param[in] r Red component value (0-255).
     * @param[in] g Green component value (0-255).
     * @param[in] b Blue component value (0-255).
     * @param[in] a Alpha transparency channel value (0-255, defaults to 255).
     * @return TextureHandle Opaque address binding reference tag.
     */
    virtual TextureHandle createSolidColorTexture(u8 r, u8 g, u8 b, u8 a = 255);

    /**
     * @brief Uploads tightly packed 8-bit RGBA pixels as a sampleable texture.
     *
     * This is the single texture-upload primitive each backend must provide;
     * every other texture entry point in this interface funnels through it.
     *
     * @param[in] rgbaPixels Pointer to @p width * @p height * 4 bytes, RGBA order.
     * @param[in] width Texture width in pixels.
     * @param[in] height Texture height in pixels.
     * @return TextureHandle Binding reference, or an invalid handle on failure.
     */
    virtual TextureHandle createTextureFromPixels(const u8 *rgbaPixels, u32 width, u32 height) = 0;

    /**
     * @brief Allocates an empty RGBA8 texture whose contents are meant to change.
     *
     * Distinct from createTextureFromPixels() because the backing store must
     * stay writable for the resource's whole life: the texture is allocated
     * once (and, on Vulkan, consumes descriptor-pool slots exactly once), then
     * refreshed in place with updateTextureRegion(). That is what makes a glyph
     * atlas that grows at runtime affordable -- recreating the texture per new
     * character would exhaust the descriptor pool within minutes.
     *
     * The initial contents are transparent black.
     *
     * @param[in] width Texture width in pixels.
     * @param[in] height Texture height in pixels.
     * @return TextureHandle for the new texture, or an invalid handle on failure.
     */
    virtual TextureHandle createDynamicTexture(u32 width, u32 height) = 0;

    /// One linear coverage byte per texel, initially zero; samples as (1, 1, 1, coverage).
    /// Backends without native support use the RGBA compatibility implementation.
    virtual TextureHandle createCoverageTexture(u32 width, u32 height);

    /// Tightly packed width * height coverage bytes. Out-of-bounds updates are rejected.
    virtual void updateCoverageTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height,
                                             const u8 *coverage);

    /**
     * @brief Overwrites a sub-rectangle of a texture in place.
     *
     * Only the named rectangle travels to the GPU, so adding one glyph to a
     * 2048x2048 atlas costs a few hundred bytes rather than 16 MB.
     *
     * @param[in] handle Texture from createDynamicTexture().
     * @param[in] x Left edge of the destination rectangle, in pixels.
     * @param[in] y Top edge of the destination rectangle, in pixels.
     * @param[in] width Rectangle width in pixels.
     * @param[in] height Rectangle height in pixels.
     * @param[in] rgbaPixels Tightly packed @p width * @p height * 4 bytes.
     *
     * @note A rectangle reaching outside the texture is rejected, not clamped.
     */
    virtual void updateTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height,
                                     const u8 *rgbaPixels) = 0;

    /**
     * @brief Loads a texture from disk (PNG/JPEG/TGA/BMP/...).
     *
     * Never throws and never fails: a missing or corrupt file logs a warning
     * and yields the magenta/black checkerboard instead, so a broken asset is
     * obvious on screen rather than fatal.
     *
     * @param[in] path Filesystem path to the image.
     * @return TextureHandle for the decoded image, or for the fallback pattern.
     */
    virtual TextureHandle createTextureFromFile(const std::string &path);

    /// The box around @p mesh's vertices in its own space; empty for an unknown handle. With
    /// transformed() it places the mesh in the world, for culling or collision of your own.
    [[nodiscard]] Aabb meshBounds(MeshHandle mesh) const;

    /**
     * @brief The full texture path: format, mip chain and sampler.
     *
     * @p pixels holds width * height * bytesPerTexel(format) bytes, rows tightly packed. A mipmapped
     * texture is immutable: updateTextureRegion() rejects it. The other create* functions keep their
     * historical state -- RGBA8 sampled as stored, one level -- so existing scenes render unchanged.
     *
     * @code
     * const TextureHandle albedo = renderer.createTexture(
     *     {.width = w, .height = h, .format = TextureFormat::Rgba8Srgb, .sampler = {.maxAnisotropy = 8}},
     *     std::as_bytes(std::span(pixels)));
     * @endcode
     */
    virtual TextureHandle createTexture(const TextureDesc &desc, std::span<const std::byte> pixels);

    /**
     * @brief An offscreen target: draws between beginRenderPass(target) and endRenderPass() land in it,
     *        and renderTargetTexture() samples the result in any later pass.
     *
     * Single-sampled whatever the window's MSAA, cleared to the clear colour at the start of every pass.
     * Screen batches and canvasView() use the target's size while it is bound.
     *
     * @code
     * const RenderTargetHandle mirror = renderer.createRenderTarget({.width = 512, .height = 512});
     * renderer.beginRenderPass(mirror);
     * drawScene(renderer);
     * renderer.endRenderPass();
     * renderer.beginRenderPass();
     * renderer.drawMesh(mirrorQuad, renderer.renderTargetTexture(mirror));
     * renderer.endRenderPass();
     * @endcode
     */
    virtual RenderTargetHandle createRenderTarget(const RenderTargetDesc &desc);
    [[nodiscard]] virtual TextureHandle renderTargetTexture(RenderTargetHandle target) const;
    /// New storage at @p width x @p height, its contents undefined; the handle and its texture handle stay valid,
    /// so a window-sized target follows the window. Between frames only, since the GPU may still use the old.
    virtual bool resizeRenderTarget(RenderTargetHandle target, u32 width, u32 height);

    /// The target's pixels as RGBA8 rows, top row first, as stored: an sRGB target reads back encoded,
    /// half floats clamp to [0, 1]. Waits for the GPU to go idle, so it is for tests and captures, not
    /// every frame. False when @p target is unknown or @p rgba is not width * height * 4 bytes.
    virtual bool readPixels(RenderTargetHandle target, std::span<u8> rgba);

    /// createTextureFromFile() with @p desc's format, mipmaps and sampler; the size comes from the file.
    /// An 8-bit image cannot fill Rgba16Float, so @p desc must name an Rgba8 format.
    TextureHandle createTextureFromFile(const std::string &path, TextureDesc desc);

    /**
     * @brief Generates the embedded magenta/black "missing texture" pattern.
     *
     * Requires no file I/O, so it is available on every platform.
     * @param[in] size Edge length in pixels.
     */
    virtual TextureHandle createCheckerboardTexture(u32 size = 64);

    /**
     * @brief Uploads a CPU-side mesh as a GPU-resident vertex + index buffer pair.
     *
     * Copies @p mesh's arrays, because the backends take ownership of what they
     * upload and the caller keeps its own copy. Prefer the rvalue overload when
     * the mesh is not needed afterwards -- a loaded model is typically several
     * megabytes of vertices, and that copy is pure waste when the source is
     * about to be destroyed anyway.
     *
     * @param[in] mesh Geometry to upload; an empty mesh yields an invalid handle.
     * @return MeshHandle referencing the uploaded pair.
     */
    virtual MeshHandle createMesh(const gfx::Mesh3D &mesh);

    /**
     * @brief Uploads a mesh the caller is finished with, moving its arrays
     *        into the backend rather than copying them.
     *
     * Not virtual, and deliberately so: it forwards to the same
     * createVertexBuffer/createIndexBuffer pair every backend already
     * overrides, so there is nothing backend-specific left for it to
     * customise.
     *
     * @param[in,out] mesh Geometry to upload; left empty on return.
     * @return MeshHandle referencing the uploaded pair.
     */
    MeshHandle createMesh(gfx::Mesh3D &&mesh);

    /**
     * @brief Replaces @p mesh's vertices, as many as it was created with; its indices stay. For skinned or
     *        otherwise deforming geometry, every frame if need be.
     *
     * A copy recorded into the frame on Vulkan and Metal, so call it between beginFrame() and the pass that
     * draws the mesh, with no pass open. The bounds culling uses follow.
     *
     * @return false for an unknown mesh, a different vertex count, or outside that window.
     */
    bool updateMesh(MeshHandle mesh, std::span<const gfx::Vertex3D> vertices);

    /**
     * @brief Draws a whole mesh, replacing the bind-VB / bind-IB / drawIndexed triple.
     *
     * This is the primary draw path; the lower-level bind/draw calls remain
     * available for advanced use.
     *
     * @param[in] mesh Mesh to draw.
     * @param[in] texture Optional texture; an invalid handle keeps the current binding.
     */
    virtual void drawMesh(MeshHandle mesh, TextureHandle texture = {});

    /**
     * @struct DrawItem
     * @brief One drawable submitted through drawMeshes(): what to draw, with
     *        which surface, and where.
     */
    struct DrawItem
    {
        MeshHandle mesh;
        //! An invalid handle leaves whatever bindMaterial()/bindTexture() last selected.
        MaterialHandle material;
        glm::mat4 model{1.0f};
    };

    /**
     * @brief Draws a batch of meshes given up front, rather than one
     *        setTransform/bindMaterial/drawMesh triple at a time.
     *
     * Semantically identical to that loop -- items are drawn in order, and the
     * base implementation is literally that loop -- but handing the whole list
     * over at once lets a backend do things a stateful, one-call-at-a-time API
     * cannot. The Vulkan backend splits the list across worker threads that
     * record into separate command buffers, which is only possible because no
     * item depends on renderer state left behind by the previous one.
     *
     * Prefer this for scene submission at high object counts; the per-item
     * calls remain available and unchanged.
     *
     * Items whose bounds lie wholly outside the camera set by setTransform() are
     * skipped; their material still becomes the current one.
     *
     * @param[in] items Drawables for this frame, drawn in the given order.
     */
    virtual void drawMeshes(std::span<const DrawItem> items);

    /**
     * @brief @p mesh once per entry of @p models, with @p material, culled like drawMeshes().
     *
     * The GPU backends draw every visible instance in one instanced call, so a forest of one tree
     * costs one draw; the software backend runs it through drawMeshes().
     */
    virtual void drawMeshInstanced(MeshHandle mesh, MaterialHandle material, std::span<const glm::mat4> models);

    /**
     * @brief Registers a material so it can be bound by handle.
     */
    virtual MaterialHandle createMaterial(const Material &material);

    /**
     * @brief Makes @p handle the active material, binding its albedo texture.
     */
    virtual void bindMaterial(MaterialHandle handle);

    /// Every light the built-in shaders use, replacing the last set; the first kMaxLights count. Until called, one
    /// default Light and a 0.15 ambient. A backend without RendererCaps::Pbr uses only directional and ambient ones.
    virtual void setLights(std::span<const gfx::Light> lights);

    /// What this backend can do beyond the common API.
    [[nodiscard]] virtual RendererCaps caps() const noexcept;

    /// @name Your own shaders (RendererCaps::CustomPipelines)
    /// @{
    /// Builds a pipeline from @p desc's shaders, for the window and for every render target: an invalid handle,
    /// logged, when the backend takes none or a shader is rejected.
    virtual PipelineHandle createPipeline(const PipelineDesc &desc);
    /// How many textures, uniform buffers and bytes of constants a pipeline may read here; all zero without
    /// RendererCaps::CustomPipelines.
    [[nodiscard]] virtual PipelineLimits pipelineLimits() const noexcept;
    /// Draws with @p draw's pipeline into the open pass. The built-in shaders' bindings are left as they were.
    virtual void drawPipeline(const PipelineDraw &draw);
    /// Multiply a projection by this when you draw a pipeline into @p target (invalid: the window): identity, except
    /// where a backend stores its targets upside down (OpenGL), so a target reads the same everywhere.
    [[nodiscard]] virtual glm::mat4 clipCorrection(RenderTargetHandle target = {}) const noexcept;
    /// @}

    /// @name Buffers, compute and indirect drawing (buffers: RendererCaps::CustomPipelines; compute: Compute)
    /// @{
    /// GPU memory that kernels write and draws read: a pipeline's uniform buffers (BufferN, up to 16 KiB), storage,
    /// per-instance data and draw arguments at once. The rest past @p initial is zero.
    virtual BufferHandle createBuffer(usize bytes, std::span<const std::byte> initial = {});
    /// Overwrites @p bytes at @p offset: a copy recorded into the frame between passes, like updateMesh(), or
    /// immediately (after the GPU goes idle) outside a frame.
    virtual bool updateBuffer(BufferHandle buffer, usize offset, std::span<const std::byte> bytes);
    /// What frames already ended wrote. Waits for the GPU, so it is for tests and tools.
    virtual bool readBuffer(BufferHandle buffer, usize offset, std::span<std::byte> out);
    virtual ComputeHandle createCompute(const ComputeDesc &desc);
    /// @p groups workgroups of @p kernel over @p buffers, with no pass open; what it writes is visible to the draws,
    /// dispatches and copies after it.
    virtual void dispatch(ComputeHandle kernel, std::span<const BufferHandle> buffers, glm::uvec3 groups,
                          std::span<const std::byte> constants = {});
    /// drawMeshInstanced() with its instances (InstanceTransform) and its DrawIndexedIndirect read from buffers a
    /// kernel may have filled, so the GPU decides how many; inside a pass, with no culling of its own. The software
    /// backend draws the whole mesh per instance whatever the arguments' index range.
    virtual void drawMeshInstancedIndirect(MeshHandle mesh, MaterialHandle material, BufferHandle instances,
                                           BufferHandle arguments, usize argumentOffset = 0);
    /// The index count to put in a DrawIndexedIndirect for @p mesh; 0 for an unknown mesh.
    [[nodiscard]] u32 meshIndexCount(MeshHandle mesh) const;
    /// @}
    /// @p target's size in pixels; zero for an unknown handle.
    [[nodiscard]] virtual glm::uvec2 targetSize(RenderTargetHandle target) const;

    [[nodiscard]] std::span<const gfx::Light> lights() const noexcept;

    /**
     * @brief Returns the material bound by the most recent bindMaterial() call.
     */
    const Material &getCurrentMaterial() const
    {
        return _currentMaterial;
    }

    /**
     * @brief Acquires the frame's render target (swapchain image / drawable)
     *        and readies the backend for a new frame.
     *
     * run() already calls this once per iteration -- do not call it yourself
     * if you are using run(), or the backend acquires two frames for one
     * present/endFrame(). Call it directly only when you drive the loop
     * yourself through getWindowManager() instead of run(), immediately
     * before that frame's first beginRenderPass().
     */
    virtual void beginFrame() = 0;

    /**
     * @brief Whether the last beginFrame() acquired a target to draw into.
     *
     * False when it could not: the swapchain was rebuilt for a resize, the
     * window is minimized, the surface is gone. The frame's draws then go
     * nowhere and endFrame() presents nothing. A loop that only draws on
     * change has to ask, or a change drawn into a skipped frame stays off
     * screen until the next one.
     */
    [[nodiscard]] virtual bool frameBegun() const noexcept
    {
        return true;
    }

    /**
     * @brief Whether the backend needs a frame even though nothing changed.
     *
     * True while a swapchain rebuild is pending, and from a rebuild or a
     * dropped present until the next frame reaches the screen. A loop that
     * only draws on change must draw anyway while this holds: the rebuild
     * runs inside beginFrame(), and the image it replaces is gone.
     */
    [[nodiscard]] virtual bool needsFrame() const noexcept
    {
        return false;
    }

    /**
     * @brief Opens a render pass: clears the target and readies it for draw
     *        calls.
     *
     * Unlike beginFrame()/endFrame(), always called by hand -- run() has no
     * opinion on how many passes a frame has. Pair with endRenderPass()
     * around every draw()/drawIndexed()/drawBatch() call, whether inside
     * run()'s callback or a hand-rolled loop.
     */
    virtual void beginRenderPass() = 0;
    /// The same into @p target, from createRenderTarget(); an invalid handle is the window.
    virtual void beginRenderPass(RenderTargetHandle target);

    /**
     * @brief Closes the render pass opened by beginRenderPass(), submitting
     *        its queued draws.
     */
    virtual void endRenderPass() = 0;

    /**
     * @brief Submits the frame and presents it to the window.
     *
     * run() already calls this once per iteration, matching its own
     * beginFrame() call -- see that comment. Call it directly only in a
     * hand-rolled loop, after the frame's last endRenderPass().
     */
    virtual void endFrame() = 0;

    /**
     * @brief Submits projection matrix variables to Uniform Buffer Objects.
     * @param[in] ubo Constant transformation memory blocks containing view values.
     */
    virtual void setTransform(const gfx::TransformUBO &ubo);

    /**
     * @brief Connects selected vector vertex details into the dynamic draw pipeline.
     * @param[in] handle Reference descriptor token identifying the intended vertex dataset.
     */
    virtual void bindVertexBuffer(VertexBufferHandle handle);

    /**
     * @brief Connects index reference tables into the dynamic draw pipeline.
     * @param[in] handle Reference descriptor token identifying the intended index arrangement.
     */
    virtual void bindIndexBuffer(IndexBufferHandle handle);

    /**
     * @brief Connects image texture mappings into active material execution environments.
     * @param[in] handle Reference descriptor token identifying the target texture layout map.
     */
    virtual void bindTexture(TextureHandle handle);

    /**
     * @brief Instructs backend pipelines to map visual constructs from indexed connection tables.
     * @param[in] indexCount Number of unique indices to process.
     * @param[in] instanceCount Total rendering loops for geometry instancing workflows (defaults to 1).
     */
    virtual void drawIndexed(u32 indexCount, u32 instanceCount = 1) = 0;

    /**
     * @brief Instructs backend pipelines to map raw vector array streams chronologically.
     * @param[in] vertexCount Number of vertices to evaluate.
     * @param[in] instanceCount Total rendering loops for geometry instancing workflows (defaults to 1).
     */
    virtual void draw(u32 vertexCount, u32 instanceCount = 1) = 0;

    /// One draw call of unlit straight-alpha triangles, in submission order with meshes,
    /// depth-tested less-or-equal without depth writes: draw opaque meshes first and UI last.
    /// An invalid texture samples white; both arrays may be reused on return.
    virtual void drawBatch(std::span<const gfx::BatchVertex> vertices, std::span<const u32> indices,
                           TextureHandle texture, gfx::BatchSpace space = gfx::BatchSpace::Screen) = 0;

    void drawBatch(const gfx::Canvas &canvas, TextureHandle texture = {},
                   gfx::BatchSpace space = gfx::BatchSpace::Screen);

    /// The current camera and render target, for gfx::Canvas::line() in world space.
    [[nodiscard]] gfx::CanvasView canvasView() const;

    /// Size of the render target in the pixels screen-space batches and line widths use.
    /// Backends report their live target; the base returns the window size it was built with.
    [[nodiscard]] virtual glm::uvec2 renderTargetSize() const noexcept;

    /**
     * @brief Sets the clear color for target framebuffers.
     * @param[in] r Red element normalized value (0.0f - 1.0f).
     * @param[in] g Green element normalized value (0.0f - 1.0f).
     * @param[in] b Blue element normalized value (0.0f - 1.0f).
     * @param[in] a Alpha channel transparency scale (0.0f - 1.0f, defaults to 1.0f).
     */
    virtual void setClearColor(f32 r, f32 g, f32 b, f32 a = 1.0f);

    /**
     * @brief Drives the render loop: polls window events, then wraps
     *        @p onFrame in beginFrame()/endFrame() every iteration, blocking
     *        until the window closes.
     *
     * @p onFrame is where beginRenderPass()/draw calls/endRenderPass()
     * belong -- beginFrame()/endFrame() are this function's job, not yours.
     * @param[in] onFrame Called once per frame, between beginFrame() and endFrame().
     */
    void run(move_only_function<void()> onFrame);

    /**
     * @brief Rebuilds the built-in 3D pipelines (lit, instanced) from the shaders under @p directory,
     * laid out as an app's staged resources/shaders: vulkan/ *.spv, opengl/ and common/ GLSL, metal/ MSL.
     *
     * Between frames only. What fails to compile or link leaves the running shader in place and is logged; run()
     * calls this by itself when renderer.shader_hot_reload is on and a file under paths.shaders changes.
     * @return true when every pipeline was rebuilt; false where the backend has no shaders (software) or any failed.
     */
    virtual bool reloadShaders(const std::filesystem::path &directory);

    /**
     * @brief Fetches access coordinates belonging to the overarching client operating window instance.
     * @return wma::IWindowManager* Raw address reference to backend window properties.
     */
    virtual wma::IWindowManager *getWindowManager();

    /**
     * @brief Identifies which rendering driver choice is managing the internal abstract interface.
     * @return RendererChoice Enumeration literal reflecting the API backend engine choice.
     */
    virtual RendererChoice getBackendType() const = 0;

    /**
     * @brief Returns the layout specs and sizes corresponding to the current window.
     * @return const wma::WindowDetails& Constant reference containing dimension attributes.
     */
    const wma::WindowDetails &getWindowDetails() const
    {
        return _windowDetails;
    }

    /**
     * @brief GPU allocation and timing counters, when this backend keeps them.
     *
     * Consumed by aura3d::DebugMode to fill the report's `gpu` section. The
     * default returns nullptr, which is the honest answer for a backend with no
     * device to measure (the software rasteriser) or no query mechanism wired
     * up yet (OpenGL); only VulkanRenderer overrides it, and only in a build
     * with AURA_ENABLE_DEBUG_MODE.
     *
     * Declared unconditionally so that application and report code needs no
     * `#ifdef` around a null check that is already the normal case.
     *
     * @return Owned by the renderer and valid until it is destroyed, or nullptr.
     */
    [[nodiscard]] virtual const IGpuDebugSource *gpuDebugSource() const noexcept
    {
        return nullptr;
    }

    /**
     * @brief GPU time of the last frame the GPU finished; see GpuTimingStats.
     *
     * Vulkan, OpenGL and Metal measure it in AURA_PROFILE_FRAME builds, once
     * setGpuTimingEnabled(true) or `renderer.gpu_timing` turns it on. Every other
     * build, and the software rasteriser, always report it unavailable, so
     * callers need no #ifdef.
     */
    [[nodiscard]] virtual GpuTimingStats gpuTiming() const noexcept;

    /// Off by default: reading a frame's queries back costs about a microsecond of CPU.
    void setGpuTimingEnabled(bool enabled) noexcept;
    [[nodiscard]] bool gpuTimingEnabled() const noexcept;

  public:
    using WindowFactory = std::function<std::unique_ptr<wma::IWindowManager>(
        wma::WindowBackend, const wma::WindowDetails &, wma::GraphicsAPI)>;
    /// Set before initialize(); retained for backend recreation.
    void setWindowFactory(WindowFactory factory)
    {
        _windowFactory = std::move(factory);
    }

  protected:
    //! Clears the window's resized flag; true when it was set.
    [[nodiscard]] bool consumeResize() noexcept;

    //! A GPU backend's recording state: no frame, a frame, or a render pass inside it.
    enum class FrameState : u8
    {
        Idle,
        Recording,
        InPass,
    };

    /// Clip transform the batch pipeline applies for @p space on this backend.
    [[nodiscard]] glm::mat4 batchTransform(gfx::BatchSpace space) const;

    /// A backend that cannot run on this machine (X11 without libX11, ...) is replaced by one that can.
    [[nodiscard]] std::unique_ptr<wma::IWindowManager>
    makeWindow(wma::WindowBackend backend, const wma::WindowDetails &details, wma::GraphicsAPI api);
    WindowFactory _windowFactory;

    /**
     * @brief Low-level window factory function implemented by specialized API backends.
     * * @param[in] title Text label mapped to the active surface window frame header.
     * @param[in] wBackend Chosen core runtime environment protocol framework.
     */
    virtual void createWindow(const char *title, const wma::WindowBackend &wBackend) = 0;

    /**
     * @struct MeshRecord
     * @brief The vertex/index buffer pair a MeshHandle resolves to.
     */
    struct MeshRecord
    {
        VertexBufferHandle vertexBuffer;
        IndexBufferHandle indexBuffer;
        u32 indexCount = 0;
        u32 vertexCount = 0;
        Aabb bounds; //! In the mesh's own space, for culling.
    };

    //! The camera's view volume from the last setTransform().
    [[nodiscard]] Frustum viewFrustum() const noexcept;
    //! @p mesh at @p model lies wholly outside @p frustum, so drawing it would draw nothing.
    [[nodiscard]] static bool culled(const MeshRecord &mesh, const glm::mat4 &model, const Frustum &frustum) noexcept;

    //! Resolves a 1-based MeshHandle, or nullptr when it does not refer to a mesh.
    const MeshRecord *getMesh(MeshHandle handle) const;

    //! Resolves a 1-based MaterialHandle, or nullptr when unknown.
    const Material *getMaterial(MaterialHandle handle) const;

    //! Drops every mesh/material record. Backends call this from cleanup(),
    //! since the underlying buffers die with the backend's own pools.
    void clearSharedResources();

    wma::WindowDetails _windowDetails; //! Copy of current platform dimension attributes
    //! Declared before anything a backend builds on it, and reset by each backend's cleanup().
    std::unique_ptr<wma::IWindowManager> _windowManagerApi;
    bool _running = false;             //! Control status tracker managing main loop life
    //! Backends create their queries lazily, on the first frame that sees this set.
    bool _gpuTimingEnabled = false;

    std::vector<MeshRecord> _meshes;  //! Mesh registry; handle == index + 1
    std::vector<DrawItem> _instanceItems; //! drawMeshInstanced()'s scratch, kept so a frame does not allocate
    std::vector<Material> _materials; //! Material registry; handle == index + 1
    std::vector<gfx::Light> _lights{gfx::Light{}, gfx::Light{.type = gfx::LightType::Ambient, .intensity = 0.15f}};
    //! The directional and punctual lights packed for a shader.
    [[nodiscard]] gfx::LightingUBO lighting() const noexcept;
    //! @p material's factors and map flags; Vulkan fills in the bindless indices itself.
    [[nodiscard]] static gfx::MaterialUniforms materialUniforms(const Material &material) noexcept;
    Material _currentMaterial{};      //! Material bound by the last bindMaterial()
    MaterialHandle _currentMaterialHandle; //! Its handle; invalid until a material is bound
    VertexBufferHandle _currentVertexBuffer;
    IndexBufferHandle _currentIndexBuffer;
    TextureHandle _currentTexture;
    f32 _clearR = 0.05f, _clearG = 0.05f, _clearB = 0.05f, _clearA = 1.0f;

    /*
     * Transform from the last setTransform(). Lives here rather than being
     * duplicated in each backend because drawMeshes() needs to vary only the
     * model matrix while preserving the caller's view/proj. Every backend's
     * setTransform() override assigns it.
     */
    gfx::TransformUBO _currentTransform{};

  private:
    //! Sizes of the coverage textures the base class emulates as RGBA, for backends with no native ones.
    std::unordered_map<TextureHandle, glm::uvec2> _coverageFallbacks;
};

} // namespace aura3d

#endif // IRENDERER_H
