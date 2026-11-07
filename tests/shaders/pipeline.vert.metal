#include <metal_stdlib>
using namespace metal;

// Metal's side of pipeline.vert: Constants at buffer(0), BufferN at buffer(1 + N), texN at texture(N) with sampler(N).

struct Constants
{
    float4x4 viewProj;
    float4 tint;
};

struct In
{
    float3 aPosition [[attribute(0)]];
    float2 aUv [[attribute(1)]];
};

struct Out
{
    float2 vUv [[user(locn0)]];
    float4 position [[position]];
};

vertex Out main0(In in [[stage_in]], constant Constants &constants [[buffer(0)]])
{
    Out out;
    out.vUv = in.aUv;
    out.position = constants.viewProj * float4(in.aPosition, 1.0);
    return out;
}
