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
