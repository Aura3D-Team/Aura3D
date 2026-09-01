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
