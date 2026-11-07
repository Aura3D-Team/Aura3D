#include <metal_stdlib>
using namespace metal;

// Metal's side of fullscreen.vert: Constants at buffer(0), BufferN at buffer(1 + N), texN at texture(N) with sampler(N).

struct Constants
{
    float4 flip;
};

struct Out
{
    float2 vUv [[user(locn0)]];
    float4 position [[position]];
};

vertex Out main0(constant Constants &constants [[buffer(0)]], uint vertexId [[vertex_id]])
{
    float2 corner = float2(float((int(vertexId) << 1) & 2), float(int(vertexId) & 2));
    Out out;
    out.vUv = float2(corner.x, 1.0 - corner.y);
    out.position = float4(corner.x * 2.0 - 1.0, (corner.y * 2.0 - 1.0) * constants.flip.x, 0.0, 1.0);
    return out;
}
