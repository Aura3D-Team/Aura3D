#include <metal_stdlib>

using namespace metal;

// Packed fields match gfx::BatchVertex; native float3 would add padding.
struct BatchVertex {
    packed_float3 pos;
    packed_float2 texCoord;
    packed_float4 color;
};

struct BatchUniforms {
    float4x4 transform;
};

struct BatchVertexOut {
    float4 position [[position]];
    float2 texCoord;
    float4 color;
};

vertex BatchVertexOut aura_vertex_batch(uint vertexId [[vertex_id]],
                                        const device BatchVertex* vertices [[buffer(0)]],
                                        constant BatchUniforms& batch [[buffer(1)]])
{
    const BatchVertex vertexIn = vertices[vertexId];
    BatchVertexOut out;
    out.position = batch.transform * float4(float3(vertexIn.pos), 1.0f);
    out.texCoord = float2(vertexIn.texCoord);
    out.color = float4(vertexIn.color);
    return out;
}

fragment float4 aura_fragment_batch(BatchVertexOut in [[stage_in]],
                                    texture2d<float> albedo [[texture(0)]],
                                    sampler albedoSampler [[sampler(0)]])
{
    return albedo.sample(albedoSampler, in.texCoord) * in.color;
}
