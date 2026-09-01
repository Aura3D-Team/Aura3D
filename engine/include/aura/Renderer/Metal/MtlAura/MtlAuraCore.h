#ifndef MTLAURACORE_H
#define MTLAURACORE_H

#pragma once

#include <cstddef>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include "aura/Core/AuraCore.h"

/**
 * @brief Frames the renderer may have in flight at once.
 *
 * Same value, and the same reasoning, as Vulkan's MAX_FRAMES_IN_FLIGHT: the CPU
 * may run one frame ahead of the GPU, so every resource written per frame
 * (the overlay's dynamic vertex/index buffers) exists twice and a frame waits
 * on a semaphore before reusing its slot. Metal's own drawable pool is separate
 * and sized by CAMetalLayer::maximumDrawableCount().
 */
#define MTL_MAX_FRAMES_IN_FLIGHT 2

namespace aura3d {
namespace mtl {

/// Shader interface

/*
 * Argument-table slots. These are the contract with resources/shaders/metal/*:
 * a change here is a change to the MSL and a re-run of
 * scripts/gen_embedded_metallib.sh.
 *
 * Vertex and fragment stages have independent argument tables in Metal, which
 * is why the geometry buffer and the light buffer can both sit at slot 0
 * without colliding.
 */

//! Vertex stage, slot 0: the geometry, read via [[vertex_id]].
inline constexpr NS::UInteger kVertexGeometrySlot = 0;
//! Vertex stage, slot 1: TransformUniforms (3D) or Overlay2DUniforms (2D).
inline constexpr NS::UInteger kVertexUniformSlot = 1;
//! Fragment stage, slot 0: LightUniforms. Unused by the overlay pipeline.
inline constexpr NS::UInteger kFragmentLightSlot = 0;
//! Fragment stage texture/sampler slot 0: the albedo map.
inline constexpr NS::UInteger kFragmentAlbedoSlot = 0;

//! MSL entry points, as named in resources/shaders/metal/*.metal.
inline constexpr const char* kVertexFunction3D = "aura_vertex_3d";
inline constexpr const char* kFragmentFunction3D = "aura_fragment_3d";
inline constexpr const char* kVertexFunction2D = "aura_vertex_2d";
inline constexpr const char* kFragmentFunction2D = "aura_fragment_2d";

/// Formats

/**
 * @brief Colour format of the drawable, and therefore of every pipeline.
 *
 * BGRA8Unorm is CAMetalLayer's own default and the only format guaranteed
 * present on both macOS and iOS. Deliberately not the _sRGB variant: the
 * GLSL/CPU backends write linear values straight to an UNORM target, so
 * picking sRGB here would make the Metal backend the one outlier whose output
 * is gamma-encoded twice.
 */
inline constexpr MTL::PixelFormat kColorFormat = MTL::PixelFormatBGRA8Unorm;

/**
 * @brief Depth format of the scene pass.
 *
 * Depth32Float is supported on every Metal GPU family, unlike the packed
 * depth/stencil formats whose availability differs between Apple silicon and
 * the older Intel/AMD Macs. Nothing in the engine uses a stencil buffer, so
 * there is no reason to reach for a combined format.
 */
inline constexpr MTL::PixelFormat kDepthFormat = MTL::PixelFormatDepth32Float;

/**
 * @brief Format textures are uploaded in.
 *
 * IRenderer's texture entry points all hand over tightly packed RGBA8, so the
 * upload needs no swizzle -- unlike the drawable, whose format is dictated by
 * CAMetalLayer.
 */
inline constexpr MTL::PixelFormat kTextureFormat = MTL::PixelFormatRGBA8Unorm;

/// Uniform blocks

/**
 * @struct TransformUniforms
 * @brief Per-draw transform handed to the 3D vertex stage.
 *
 * Delivered with setVertexBytes() rather than out of a buffer: Metal copies the
 * bytes into the command buffer itself, which is exactly the role Vulkan push
 * constants play in VulkanRenderer::bindDrawState() -- and, unlike Vulkan's
 * guaranteed 128-byte budget, Metal's 4 KB limit is roomy enough to carry the
 * whole view/projection pair alongside the model matrix. That is why there is
 * no per-frame uniform buffer here at all.
 *
 * Layout must match the MSL struct of the same name in mtl_shader3d.metal.
 */
struct TransformUniforms {
    glm::mat4 model{1.0f};
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};
    /*
     * Inverse-transpose of the model matrix's rotation, widened to a mat4 for
     * uniform stride. Only the upper-left 3x3 is read; unlike the Vulkan path,
     * nothing is smuggled through the 4th column, because Metal binds textures
     * directly instead of indexing a bindless table.
     */
    glm::mat4 normalMatrix{1.0f};
};

/**
 * @struct Overlay2DUniforms
 * @brief Per-batch projection handed to the overlay vertex stage.
 *
 * Layout must match the MSL struct of the same name in mtl_shader2d.metal.
 */
struct Overlay2DUniforms {
    glm::mat4 proj{1.0f};
};

/*
 * The C++ and MSL sides of the shader interface are two hand-written copies of
 * the same layout, so lock them together here. MSL's plain float3/float4 are
 * 16-byte aligned while glm's are 4-byte aligned, which is why the shader
 * structs use packed_float3/packed_float2/packed_float4; these assertions are
 * what catch the day someone drops the "packed_", or adds a field to
 * gfx::Vertex3D without touching the MSL.
 */
static_assert(sizeof(gfx::Vertex3D) == 48, "MSL Vertex3D expects 48 tightly packed bytes");
static_assert(offsetof(gfx::Vertex3D, pos) == 0, "MSL Vertex3D::pos offset drifted");
static_assert(offsetof(gfx::Vertex3D, texCoord) == 12, "MSL Vertex3D::texCoord offset drifted");
static_assert(offsetof(gfx::Vertex3D, color) == 20, "MSL Vertex3D::color offset drifted");
static_assert(offsetof(gfx::Vertex3D, normal) == 36, "MSL Vertex3D::normal offset drifted");

static_assert(sizeof(gfx::Vertex2D) == 32, "MSL Vertex2D expects 32 tightly packed bytes");
static_assert(offsetof(gfx::Vertex2D, pos) == 0, "MSL Vertex2D::pos offset drifted");
static_assert(offsetof(gfx::Vertex2D, texCoord) == 8, "MSL Vertex2D::texCoord offset drifted");
static_assert(offsetof(gfx::Vertex2D, color) == 16, "MSL Vertex2D::color offset drifted");

static_assert(sizeof(gfx::LightUBO) == 48, "MSL LightUniforms expects 48 bytes including tail padding");
static_assert(offsetof(gfx::LightUBO, direction) == 0, "MSL LightUniforms::direction offset drifted");
static_assert(offsetof(gfx::LightUBO, intensity) == 12, "MSL LightUniforms::intensity offset drifted");
static_assert(offsetof(gfx::LightUBO, color) == 16, "MSL LightUniforms::color offset drifted");
static_assert(offsetof(gfx::LightUBO, ambient) == 32, "MSL LightUniforms::ambient offset drifted");

static_assert(sizeof(TransformUniforms) == 256, "MSL TransformUniforms expects four contiguous mat4s");
static_assert(sizeof(Overlay2DUniforms) == 64, "MSL Overlay2DUniforms expects one mat4");

/// Helpers

/**
 * @brief Wraps a freshly created Metal object in a reference-counting handle.
 *
 * Every metal-cpp entry point whose name begins with @c new or @c alloc returns
 * an object the caller owns exactly one reference to. NS::TransferPtr adopts
 * that reference without
 * adding another, so the object dies with the handle -- which is what keeps
 * this backend free of manual retain/release pairs.
 */
template <typename T>
[[nodiscard]] inline NS::SharedPtr<T> adopt(T* object) noexcept
{
    return NS::TransferPtr(object);
}

/**
 * @brief Wraps a borrowed (typically autoreleased) Metal object, retaining it.
 *
 * For the objects metal-cpp hands back *without* transferring ownership --
 * CA::MetalLayer::nextDrawable(), the descriptors' static factories, anything
 * the caller did not new/alloc. Retaining lifts the object out of the
 * surrounding autorelease pool, so it stays valid past the pool's drain.
 */
template <typename T>
[[nodiscard]] inline NS::SharedPtr<T> retain(T* object) noexcept
{
    return NS::RetainPtr(object);
}

/**
 * @brief Builds an owned NS::String from a C string.
 *
 * Returns an owning handle rather than the autoreleased NS::String::string(),
 * so callers need no live autorelease pool to hold a shader name or a label.
 */
[[nodiscard]] inline NS::SharedPtr<NS::String> makeString(const char* text)
{
    return adopt(NS::String::alloc()->init(text, NS::UTF8StringEncoding));
}

/**
 * @class ScopedAutoreleasePool
 * @brief RAII wrapper around an NS::AutoreleasePool.
 *
 * Several Metal entry points -- nextDrawable(), commandBuffer(),
 * renderCommandEncoder() -- hand back autoreleased objects, so a render loop
 * with no pool of its own accumulates one drawable, one command buffer and one
 * encoder per frame for the process's whole life. Wrapping each frame in a pool
 * is the standard fix, and doing it through a scope guard is what keeps the
 * drain from being skipped when a frame throws.
 *
 * Deliberately not an NS::SharedPtr: pools must be drained in strict LIFO order
 * and retaining one past its scope is invalid, so shared ownership is the wrong
 * model however convenient it looks.
 */
class ScopedAutoreleasePool {
public:
    ScopedAutoreleasePool() noexcept
        : _pool(NS::AutoreleasePool::alloc()->init()) {}

    ~ScopedAutoreleasePool()
    {
        if (_pool)
            _pool->release();
    }

    ScopedAutoreleasePool(const ScopedAutoreleasePool&) = delete;
    ScopedAutoreleasePool& operator=(const ScopedAutoreleasePool&) = delete;

    ScopedAutoreleasePool(ScopedAutoreleasePool&& other) noexcept
        : _pool(other._pool)
    {
        other._pool = nullptr;
    }

    ScopedAutoreleasePool& operator=(ScopedAutoreleasePool&& other) noexcept
    {
        if (this != &other)
        {
            if (_pool)
                _pool->release();

            _pool = other._pool;
            other._pool = nullptr;
        }
        return *this;
    }

private:
    NS::AutoreleasePool* _pool = nullptr;
};

/**
 * @brief Renders an NS::Error as a loggable string.
 *
 * @param[in] error The error metal-cpp wrote through an NS::Error** out-param;
 *        may be null, which every Metal call is allowed to leave it on success.
 * @return The localized description, or a fixed placeholder when @p error is
 *         null or carries no description -- so a failure is never reported as
 *         an empty message.
 */
[[nodiscard]] inline std::string describeError(NS::Error* error)
{
    if (!error)
        return "no error detail reported";

    const NS::String* description = error->localizedDescription();
    if (!description)
        return "error " + std::to_string(static_cast<long long>(error->code()));

    const char* utf8 = description->utf8String();
    return utf8 ? std::string(utf8) : "unprintable error description";
}

} // namespace mtl
} // namespace aura3d

#endif // MTLAURACORE_H
