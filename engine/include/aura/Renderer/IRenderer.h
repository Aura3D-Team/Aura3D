#ifndef IRENDERER_H
#define IRENDERER_H

#pragma once

#include <memory>
#include <span>
#include <string>
#include <vector>

#include <wma/wma.hpp>
#include <glm/glm.hpp>

#include "aura/Utils/PlatformCompat.h"

#include "aura/Core/AuraCore.h"
#include "aura/Renderer/Material.h"
#include "aura/Renderer/RenderHandles.h"
#include "aura/Core/AuraSettings/AuraSettings.h"

/**
 * @brief List of available graphics backend renderers.
 * Uses X-Macros for synchronized enum and string conversions.
 */
#define RENDERER_LIST \
    X(SOFTWARE)       \
    X(OPENGL)         \
    X(VULKAN)         \

/**
 * @brief List of supported dimensions/modes for rendering.
 */
#define RENDERER_MODE_LIST \
    X(MODE_2D)             \
    X(MODE_3D)             \

namespace aura3d {

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
inline bool RendererChoiceFromString(const std::string& s, RendererChoice& out)
{
    std::string up;
    up.reserve(s.size());
    for (const char c : s) up += std::toupper(c);

#define X(name) if (up == #name) { out = RendererChoice::name; return true; }
    RENDERER_LIST
#undef X

        if (up == "CPU") { out = RendererChoice::SOFTWARE; return true; }

    return false;
}

/**
 * @brief Converts a RendererChoice enum value to its exact string representation.
 * * @param[in] c The renderer enum value.
 * @return const char* A static string literal matching the enum identifier, or "UNKNOWN".
 */
inline const char* RendererChoiceToString(RendererChoice c)
{
    switch (c)
    {
        case RendererChoice::SOFTWARE: return "SOFTWARE";
        case RendererChoice::OPENGL:   return "OPENGL";
        case RendererChoice::VULKAN:   return "VULKAN";
    }
    return "UNKNOWN";
}

/**
 * @class IRenderer
 * @brief Pure virtual interface defining a cross-platform graphics rendering context.
 * * Serves as the abstraction layer for resource allocation, windowing setups,
 * pipeline bindings, and frame cycle management regardless of the targeted backend API.
 */
class IRenderer {
public:

     /**
     * @brief Constructs the base IRenderer instance.
     * * @param[in] windowDetails Struct containing initial parameters like size and title.
     * @param[in] mode Specifies if this context handles 3D operations.
     */
    IRenderer(const wma::WindowDetails& windowDetails)
        : _windowDetails(windowDetails) {}

    /**
     * @brief Virtual destructor ensuring safe polymorphic cleanups.
     */
    virtual ~IRenderer() = default;

    /**
     * @brief Configures underlying graphics libraries and hardware contexts using engine settings.
     * * @param[in] settings Pointer to the foundational application runtime configuration.
     */
    virtual void initialize(AuraSettings* settings) = 0;

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
    virtual VertexBufferHandle createVertexBuffer(std::vector<gfx::Vertex3D>&& vertices) = 0;

     /**
     * @brief Allocates an indexed layout element buffer using 16-bit short tags.
     * @param[in] indices Array element list containing sequential mesh connections.
     * @return IndexBufferHandle Opaque identifier representation for drawing instructions.
     */
    virtual IndexBufferHandle createIndexBuffer(std::vector<u16>&& indices) = 0;

     /**
     * @brief Allocates an indexed layout element buffer using 32-bit unsigned identifiers.
     * @param[in] indices Array element list containing sequential mesh connections.
     * @return IndexBufferHandle Opaque identifier representation for drawing instructions.
     */
    virtual IndexBufferHandle createIndexBuffer(std::vector<u32>&& indices) = 0;

    /**
     * @brief Deploys a unified, solid-color monochromatic texture block directly to the GPU.
     * @param[in] r Red component value (0-255).
     * @param[in] g Green component value (0-255).
     * @param[in] b Blue component value (0-255).
     * @param[in] a Alpha transparency channel value (0-255, defaults to 255).
     * @return TextureHandle Opaque address binding reference tag.
     */
    virtual TextureHandle createSolidColorTexture(u8 r, u8 g, u8 b, u8 a = 255) = 0;

    /**
     * @brief Uploads tightly packed 8-bit RGBA pixels as a sampleable texture.
     *
     * This is the single texture-upload primitive each backend must provide;
     * every other texture entry point in this interface funnels through it.
     *
     * @param[in] rgbaPixels Pointer to @p width * @p height * 4 bytes, RGBA order.
     * @param[in] width Texture width in pixels.
     * @param[in] height Texture height in pixels.
     * @return TextureHandle Binding reference, or INVALID_HANDLE on failure.
     */
    virtual TextureHandle createTextureFromPixels(const u8* rgbaPixels, u32 width, u32 height) = 0;

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
     * @return TextureHandle for the new texture, or INVALID_HANDLE on failure.
     */
    virtual TextureHandle createDynamicTexture(u32 width, u32 height) = 0;

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
    virtual void updateTextureRegion(TextureHandle handle,
                                     u32 x, u32 y,
                                     u32 width, u32 height,
                                     const u8* rgbaPixels) = 0;

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
    virtual TextureHandle createTextureFromFile(const std::string& path);

    /**
     * @brief Generates the embedded magenta/black "missing texture" pattern.
     *
     * Requires no file I/O, so it is available on every platform.
     * @param[in] size Edge length in pixels.
     */
    virtual TextureHandle createCheckerboardTexture(u32 size = 64);

    /**
     * @brief Uploads a CPU-side mesh as a GPU-resident vertex + index buffer pair.
     * @param[in] mesh Geometry to upload; an empty mesh yields INVALID_HANDLE.
     * @return MeshHandle referencing the uploaded pair.
     */
    virtual MeshHandle createMesh(const gfx::Mesh3D& mesh);

    /**
     * @brief Draws a whole mesh, replacing the bind-VB / bind-IB / drawIndexed triple.
     *
     * This is the primary draw path; the lower-level bind/draw calls remain
     * available for advanced use.
     *
     * @param[in] mesh Mesh to draw.
     * @param[in] texture Optional texture; INVALID_HANDLE keeps the current binding.
     */
    virtual void drawMesh(MeshHandle mesh, TextureHandle texture = INVALID_HANDLE);

    /**
     * @struct DrawItem
     * @brief One drawable submitted through drawMeshes(): what to draw, with
     *        which surface, and where.
     */
    struct DrawItem {
        MeshHandle mesh = INVALID_HANDLE;
        //! INVALID_HANDLE leaves whatever bindMaterial()/bindTexture() last selected.
        MaterialHandle material = INVALID_HANDLE;
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
     * @param[in] items Drawables for this frame, drawn in the given order.
     */
    virtual void drawMeshes(std::span<const DrawItem> items);

    /**
     * @brief Registers a material so it can be bound by handle.
     */
    virtual MaterialHandle createMaterial(const Material& material);

    /**
     * @brief Makes @p handle the active material, binding its albedo texture.
     */
    virtual void bindMaterial(MaterialHandle handle);

    /**
     * @brief Sets the directional light consumed by the built-in shaders.
     *
     * Backends override this to push the data to the GPU; the base
     * implementation records it so getLight() always reflects the last value.
     */
    virtual void setLight(const gfx::LightUBO& light);

    /**
     * @brief Returns the directional light currently in effect.
     */
    const gfx::LightUBO& getLight() const { return _light; }

    /**
     * @brief Returns the material bound by the most recent bindMaterial() call.
     */
    const Material& getCurrentMaterial() const { return _currentMaterial; }

    /**
     * @brief Prepares hardware commands to execute a synchronized drawing pass cycle.
     */
    virtual void beginFrame() = 0;

    /**
     * @brief Signals target attachment boundaries that a visual group collection sequence is commencing.
     */
    virtual void beginRenderPass() = 0;

    /**
     * @brief Wraps structural and execution contexts for the current visual pass.
     */
    virtual void endRenderPass() = 0;

    /**
     * @brief Completes target operations and presents final image frame updates to monitor screens.
     */
    virtual void endFrame() = 0;

    /**
     * @brief Submits projection matrix variables to Uniform Buffer Objects.
     * @param[in] ubo Constant transformation memory blocks containing view values.
     */
    virtual void setTransform(const gfx::TransformUBO& ubo) = 0;

    /**
     * @brief Connects selected vector vertex details into the dynamic draw pipeline.
     * @param[in] handle Reference descriptor token identifying the intended vertex dataset.
     */
    virtual void bindVertexBuffer(VertexBufferHandle handle) = 0;

    /**
     * @brief Connects index reference tables into the dynamic draw pipeline.
     * @param[in] handle Reference descriptor token identifying the intended index arrangement.
     */
    virtual void bindIndexBuffer(IndexBufferHandle handle) = 0;

    /**
     * @brief Connects image texture mappings into active material execution environments.
     * @param[in] handle Reference descriptor token identifying the target texture layout map.
     */
    virtual void bindTexture(TextureHandle handle) = 0;

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

    /**
     * @brief Submits one batch of unlit 2D geometry as a single draw call.
     *
     * Runs on a dedicated overlay pipeline, independent of the 3D scene:
     *  - positions are window pixels with (0,0) at the top-left corner, mapped
     *    to clip space by an orthographic projection the backend derives from
     *    the current framebuffer size -- no camera is involved, so a caller
     *    never has to fold a scene view/projection out of its coordinates;
     *  - shading is unlit, @c texel * @c vertexColor, so overlays keep their
     *    exact colour whatever the scene's light is doing;
     *  - depth testing is off and straight alpha blending is on, so the batch
     *    composites over everything already drawn this pass;
     *  - the whole batch becomes one draw call, which is what makes a 500-glyph
     *    string cost the same as a single quad.
     *
     * Vertex and index data are copied into backend-owned dynamic buffers, so
     * the caller may reuse or destroy its arrays as soon as this returns.
     *
     * Must be called between beginRenderPass() and endRenderPass(), after the
     * scene's own draws. Leaves no 2D state bound: the next 3D draw rebinds its
     * own pipeline.
     *
     * @param[in] vertices Batch vertices in window-pixel space.
     * @param[in] indices Triangle list into @p vertices.
     * @param[in] texture Texture sampled by the batch; INVALID_HANDLE draws
     *        untextured (vertex colour only).
     */
    virtual void drawBatch2D(std::span<const gfx::Vertex2D> vertices,
                             std::span<const u32> indices,
                             TextureHandle texture) = 0;

    /**
     * @brief Sets the clear color for target framebuffers.
     * @param[in] r Red element normalized value (0.0f - 1.0f).
     * @param[in] g Green element normalized value (0.0f - 1.0f).
     * @param[in] b Blue element normalized value (0.0f - 1.0f).
     * @param[in] a Alpha channel transparency scale (0.0f - 1.0f, defaults to 1.0f).
     */
    virtual void setClearColor(f32 r, f32 g, f32 b, f32 a = 1.0f) = 0;

    /**
     * @brief Initializes execution loop parameters, executing callback functions inside standard frame limits.
     * * @param[in] onFrame Callable callback structure managing system updates per game tick iteration.
     */
    void run(move_only_function<void()> onFrame);

    /**
     * @brief Fetches access coordinates belonging to the overarching client operating window instance.
     * @return wma::IWindowManager* Raw address reference to backend window properties.
     */
    virtual wma::IWindowManager* getWindowManager() = 0;

    /**
     * @brief Identifies which rendering driver choice is managing the internal abstract interface.
     * @return RendererChoice Enumeration literal reflecting the API backend engine choice.
     */
    virtual RendererChoice getBackendType() const = 0;

    /**
     * @brief Returns the layout specs and sizes corresponding to the current window.
     * @return const wma::WindowDetails& Constant reference containing dimension attributes.
     */
    const wma::WindowDetails& getWindowDetails() const { return _windowDetails; }

protected:
    /**
     * @brief Low-level window factory function implemented by specialized API backends.
     * * @param[in] title Text label mapped to the active surface window frame header.
     * @param[in] wBackend Chosen core runtime environment protocol framework.
     */
    virtual void createWindow(const char* title, const wma::WindowBackend& wBackend) = 0;

    /**
     * @struct MeshRecord
     * @brief The vertex/index buffer pair a MeshHandle resolves to.
     */
    struct MeshRecord {
        VertexBufferHandle vertexBuffer = INVALID_HANDLE;
        IndexBufferHandle indexBuffer  = INVALID_HANDLE;
        u32 indexCount   = 0;
    };

    //! Resolves a 1-based MeshHandle, or nullptr when it does not refer to a mesh.
    const MeshRecord* getMesh(MeshHandle handle) const;

    //! Resolves a 1-based MaterialHandle, or nullptr when unknown.
    const Material* getMaterial(MaterialHandle handle) const;

    //! Drops every mesh/material record. Backends call this from cleanup(),
    //! since the underlying buffers die with the backend's own pools.
    void clearSharedResources();

    wma::WindowDetails _windowDetails;  //! Copy of current platform dimension attributes
    bool _running = false;              //! Control status tracker managing main loop life

    std::vector<MeshRecord> _meshes;    //! Mesh registry; handle == index + 1
    std::vector<Material> _materials;   //! Material registry; handle == index + 1
    gfx::LightUBO _light{};             //! Directional light for the built-in shaders
    Material _currentMaterial{};        //! Material bound by the last bindMaterial()

    /*
     * Transform from the last setTransform(). Lives here rather than being
     * duplicated in each backend because drawMeshes() needs to vary only the
     * model matrix while preserving the caller's view/proj. Every backend's
     * setTransform() override assigns it.
     */
    gfx::TransformUBO _currentTransform{};
};

} // namespace aura3d

#endif // IRENDERER_H
