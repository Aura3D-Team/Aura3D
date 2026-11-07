#include <metal_stdlib>
using namespace metal;

// Metal's side of many.frag: Constants at buffer(0), BufferN at buffer(1 + N), texN at texture(N) with sampler(N).

struct Block
{
    float4 value;
};

struct In
{
    float2 vUv [[user(locn0)]];
};

fragment float4 main0(In in [[stage_in]],
                      constant Block &b0 [[buffer(1)]],
                      constant Block &b1 [[buffer(2)]],
                      constant Block &b2 [[buffer(3)]],
                      constant Block &b3 [[buffer(4)]],
                      constant Block &b4 [[buffer(5)]],
                      constant Block &b5 [[buffer(6)]],
                      texture2d<float> tex0 [[texture(0)]],
                      texture2d<float> tex1 [[texture(1)]],
                      texture2d<float> tex2 [[texture(2)]],
                      texture2d<float> tex3 [[texture(3)]],
                      texture2d<float> tex4 [[texture(4)]],
                      texture2d<float> tex5 [[texture(5)]],
                      texture2d<float> tex6 [[texture(6)]],
                      texture2d<float> tex7 [[texture(7)]],
                      texture2d<float> tex8 [[texture(8)]],
                      texture2d<float> tex9 [[texture(9)]],
                      sampler s0 [[sampler(0)]],
                      sampler s1 [[sampler(1)]],
                      sampler s2 [[sampler(2)]],
                      sampler s3 [[sampler(3)]],
                      sampler s4 [[sampler(4)]],
                      sampler s5 [[sampler(5)]],
                      sampler s6 [[sampler(6)]],
                      sampler s7 [[sampler(7)]],
                      sampler s8 [[sampler(8)]],
                      sampler s9 [[sampler(9)]])
{
    float t = tex0.sample(s0, in.vUv).r + tex1.sample(s1, in.vUv).r + tex2.sample(s2, in.vUv).r + tex3.sample(s3, in.vUv).r + tex4.sample(s4, in.vUv).r + tex5.sample(s5, in.vUv).r + tex6.sample(s6, in.vUv).r + tex7.sample(s7, in.vUv).r + tex8.sample(s8, in.vUv).r + tex9.sample(s9, in.vUv).r;
    return float4(t / 10.0, b0.value.x + b1.value.x + b2.value.x + b3.value.x + b4.value.x + b5.value.x, 0.0, 1.0);
}
