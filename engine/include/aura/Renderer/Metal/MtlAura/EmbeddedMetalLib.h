#ifndef EMBEDDED_METAL_LIB_H
#define EMBEDDED_METAL_LIB_H

#pragma once

/*
 * GENERATED FILE - do not edit by hand.
 * Regenerate with scripts/gen_embedded_metallib.sh after editing
 * resources/shaders/metal/*.
 */

/**
 * @brief 1 when this header carries a precompiled .metallib, 0 when it
 *        carries only MSL source.
 *
 * Only a macOS host with Xcode can produce the compiled form, so a header
 * regenerated on Linux (or committed from a CI run) legitimately reports 0.
 * MtlShaderLibraryManager handles both.
 */
#define AURA_METAL_HAS_EMBEDDED_METALLIB 0

namespace aura3d {
namespace mtl {

//! Every MSL source under resources/shaders/metal, concatenated in the
//! order gen_embedded_metallib.sh lists them. Compiled at runtime by
//! MtlShaderLibraryManager when no .metallib is embedded.
static const char mtl_library_source[] = R"AURA_MSL(
#include <metal_stdlib>

using namespace metal;

/*
 * Scene (3D) stages for the Metal backend, the counterpart of
 * resources/shaders/vulkan/vk_shader3d.{vert,frag}.
 *
 * Two deliberate differences from the Vulkan pair:
 *
 *  - No bindless texture table. Metal binds a texture straight to an argument
 *    slot, so there is no descriptor pool to exhaust and no per-draw array
 *    index to bit-cast through the transform (see vk_shader3d.vert's comment on
 *    normalMatrix's 4th column). MetalRenderer::bindTexture() just calls
 *    setFragmentTexture().
 *
 *  - Vertices are read straight out of a device buffer indexed by vertex_id
 *    rather than being declared as [[stage_in]] attributes. That keeps the
 *    vertex layout described in exactly one place (the packed structs below,
 *    which mirror aura3d::gfx::Vertex3D byte for byte) instead of splitting it
 *    between a struct and an MTLVertexDescriptor built in C++.
 *
 * Every struct here is layout-locked against its C++ original by the
 * static_asserts in MtlAuraCore.h; the packed_* types are what make the two
 * agree, since MSL's plain float3/float4 are 16-byte aligned while glm's are
 * not.
 */

//! Mirrors aura3d::gfx::Vertex3D (48 bytes: 12 + 8 + 16 + 12).
struct Vertex3D {
    packed_float3 pos;
    packed_float2 texCoord;
    packed_float4 color;
    packed_float3 normal;
};

//! Mirrors aura3d::mtl::TransformUniforms (256 bytes), delivered per draw
//! through setVertexBytes() -- Metal's equivalent of Vulkan push constants.
struct TransformUniforms {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
    float4x4 normalMatrix;
};

//! Mirrors aura3d::gfx::LightUBO (std140-compatible, 48 bytes).
struct LightUniforms {
    packed_float3 direction; //! Direction the light travels.
    float intensity;
    packed_float4 color;
    float ambient;
    float _pad[3]; //! Tail padding, present so the size matches the C++ struct.
};

struct SceneVertexOut {
    float4 position [[position]];
    float2 texCoord;
    float4 color;
    float3 normal;
    float3 worldPos;
};

vertex SceneVertexOut aura_vertex_3d(uint vertexId [[vertex_id]],
                                     const device Vertex3D* vertices [[buffer(0)]],
                                     constant TransformUniforms& transform [[buffer(1)]])
{
    const Vertex3D vertexIn = vertices[vertexId];
    const float4 worldPos = transform.model * float4(float3(vertexIn.pos), 1.0f);

    /*
     * Only the upper-left 3x3 of normalMatrix rotates a normal; the 4th row and
     * column exist purely so the C++ side can hand over four mat4s of uniform
     * stride.
     */
    const float3x3 normalRotation = float3x3(transform.normalMatrix[0].xyz,
                                             transform.normalMatrix[1].xyz,
                                             transform.normalMatrix[2].xyz);

    SceneVertexOut out;
    out.position = transform.proj * transform.view * worldPos;
    out.texCoord = float2(vertexIn.texCoord);
    out.color = float4(vertexIn.color);
    out.normal = normalRotation * float3(vertexIn.normal);
    out.worldPos = worldPos.xyz;
    return out;
}

fragment float4 aura_fragment_3d(SceneVertexOut in [[stage_in]],
                                 constant LightUniforms& light [[buffer(0)]],
                                 texture2d<float> albedo [[texture(0)]],
                                 sampler albedoSampler [[sampler(0)]])
{
    //! direction is the way the light travels, so the vector towards it is negated.
    const float3 toLight = normalize(-float3(light.direction));
    const float3 normal = normalize(in.normal);

    const float diffuse = max(dot(normal, toLight), 0.0f) * light.intensity;
    const float lighting = light.ambient + diffuse;

    const float4 texColor = albedo.sample(albedoSampler, in.texCoord);
    return texColor * in.color * float4(float4(light.color).rgb * lighting, 1.0f);
}

#include <metal_stdlib>

using namespace metal;

/*
 * Overlay (2D) stages for the Metal backend, the counterpart of
 * resources/shaders/vulkan/vk_shader2d.{vert,frag}.
 *
 * Unlit on purpose: the result is texel * vertexColor and nothing else -- no
 * light, no ambient term -- so an overlay keeps exactly the colour it asked for
 * regardless of what the scene's lighting is doing. Glyph atlases store white
 * RGB with coverage in alpha, which makes vertex colour the sole source of text
 * colour.
 *
 * Positions arrive as window pixels with (0,0) at the top-left corner. The
 * orthographic matrix the backend supplies is what maps them into clip space,
 * so this pipeline is entirely independent of the 3D scene's camera.
 */

//! Mirrors aura3d::gfx::Vertex2D (32 bytes: 8 + 8 + 16). See mtl_shader3d.metal
//! for why the packed_* types are mandatory here.
struct Vertex2D {
    packed_float2 pos;
    packed_float2 texCoord;
    packed_float4 color;
};

//! Mirrors aura3d::mtl::Overlay2DUniforms (64 bytes), delivered per batch
//! through setVertexBytes().
struct Overlay2DUniforms {
    float4x4 proj;
};

struct Overlay2DVertexOut {
    float4 position [[position]];
    float2 texCoord;
    float4 color;
};

vertex Overlay2DVertexOut aura_vertex_2d(uint vertexId [[vertex_id]],
                                         const device Vertex2D* vertices [[buffer(0)]],
                                         constant Overlay2DUniforms& overlay [[buffer(1)]])
{
    const Vertex2D vertexIn = vertices[vertexId];

    Overlay2DVertexOut out;
    out.position = overlay.proj * float4(float2(vertexIn.pos), 0.0f, 1.0f);
    out.texCoord = float2(vertexIn.texCoord);
    out.color = float4(vertexIn.color);
    return out;
}

fragment float4 aura_fragment_2d(Overlay2DVertexOut in [[stage_in]],
                                 texture2d<float> albedo [[texture(0)]],
                                 sampler albedoSampler [[sampler(0)]])
{
    return albedo.sample(albedoSampler, in.texCoord) * in.color;
}

)AURA_MSL";

} // namespace mtl
} // namespace aura3d

#endif // EMBEDDED_METAL_LIB_H
