#ifndef IRENDERER_H
#define IRENDERER_H

#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <wma/wma.hpp>
#include <glm/glm.hpp>

#include "aura/Core/AuraCore.h"
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
    virtual void initialize(const AuraSettings* settings) = 0;

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
    void run(std::function<void()> onFrame);

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

    wma::WindowDetails _windowDetails;  /**< Copy of current platform dimension attributes. */
    bool _running = false;              /**< Control status tracker managing main loop life. */
};

} // namespace aura3d

#endif // IRENDERER_H
