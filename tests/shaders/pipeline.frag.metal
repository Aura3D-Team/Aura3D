#include <metal_stdlib>
using namespace metal;

// Metal's side of pipeline.frag: Constants at buffer(0), BufferN at buffer(1 + N), texN at texture(N) with sampler(N).

struct Constants
{
    float4x4 viewProj;
    float4 tint;
};

struct Buffer0
{
    float4 palette[4];
};

struct In
{
    float2 vUv [[user(locn0)]];
};

fragment float4 main0(In in [[stage_in]], constant Constants &constants [[buffer(0)]],
                      constant Buffer0 &buffer0 [[buffer(1)]], texture2d<float> tex0 [[texture(0)]],
                      sampler tex0Smplr [[sampler(0)]])
{
    float blue = tex0.sample(tex0Smplr, in.vUv).b * buffer0.palette[1].z + buffer0.palette[2].z;
    return float4(in.vUv * constants.tint.xy, blue, 1.0);
}
