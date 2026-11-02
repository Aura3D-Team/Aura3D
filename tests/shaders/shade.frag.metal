#include <metal_stdlib>
using namespace metal;

// Metal's side of shade.frag: Constants at buffer(0), BufferN at buffer(1 + N), texN at texture(N) with sampler(N).

struct In
{
    float2 vUv [[user(locn0)]];
};

fragment float4 main0(In in [[stage_in]], texture2d<float> tex0 [[texture(0)]], sampler tex0Smplr [[sampler(0)]])
{
    float depth = tex0.sample(tex0Smplr, in.vUv).r;
    return float4(depth, depth < 0.9 ? 1.0 : 0.0, 0.0, 1.0);
}
