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

struct GpuLight {
    float4 positionRange;
    float4 colorIntensity;
    float4 directionCosOuter;
    float4 params; //! x cos(inner), y the LightType.
};

//! aura3d::gfx::LightType, as GpuLight::params.y carries it.
constant float kLightDirectional = 0.0f;
constant float kLightAmbient = 3.0f;

//! Mirrors aura3d::gfx::LightingUBO (std140-compatible, 528 bytes).
struct LightingUniforms {
    uint count;
    uint _pad[3];
    GpuLight lights[8];
};

//! Mirrors aura3d::gfx::MaterialUniforms (64 bytes); the maps are bound to texture slots 1-3.
struct MaterialUniforms {
    float4 tint;
    packed_float3 emissive;
    float roughness;
    float metallic;
    uint maps;
    uint _indices[3];
    uint _pad[3];
};

constant uint kNormalMap = 1u;
constant uint kMetallicRoughnessMap = 2u;
constant uint kEmissiveMap = 4u;
constant float kPi = 3.14159265f;

struct SceneVertexOut {
    float4 position [[position]];
    float2 texCoord;
    float4 color;
    float3 normal;
    float3 worldPos;
    float3 cameraPos [[flat]];
};

//! The view matrix is rigid, so its inverse rotation undoes the translation.
static float3 cameraPosition(float4x4 view)
{
    return -(transpose(float3x3(view[0].xyz, view[1].xyz, view[2].xyz)) * view[3].xyz);
}

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
    out.cameraPos = cameraPosition(transform.view);
    return out;
}

//! Mirrors aura3d::mtl::InstanceUniforms: drawMeshInstanced()'s per-instance model and normal matrices.
struct InstanceUniforms {
    float4x4 model;
    float4x4 normalMatrix;
};

//! aura_vertex_3d with the model per instance; transform still carries view and proj.
vertex SceneVertexOut aura_vertex_3d_instanced(uint vertexId [[vertex_id]],
                                               uint instanceId [[instance_id]],
                                               const device Vertex3D* vertices [[buffer(0)]],
                                               constant TransformUniforms& transform [[buffer(1)]],
                                               const device InstanceUniforms* instances [[buffer(2)]])
{
    const Vertex3D vertexIn = vertices[vertexId];
    const InstanceUniforms instance = instances[instanceId];
    const float4 worldPos = instance.model * float4(float3(vertexIn.pos), 1.0f);
    const float3x3 normalRotation = float3x3(instance.normalMatrix[0].xyz,
                                             instance.normalMatrix[1].xyz,
                                             instance.normalMatrix[2].xyz);

    SceneVertexOut out;
    out.position = transform.proj * transform.view * worldPos;
    out.texCoord = float2(vertexIn.texCoord);
    out.color = float4(vertexIn.color);
    out.normal = normalRotation * float3(vertexIn.normal);
    out.worldPos = worldPos.xyz;
    out.cameraPos = cameraPosition(transform.view);
    return out;
}

// The metallic-roughness shading of resources/shaders/common/pbr.glsl, line for line.
struct Surface {
    float3 albedo;
    float3 normal;
    float3 view;
    float3 position;
    float roughness;
    float metallic;
};

static float3 perturbNormal(float3 n, float3 p, float2 uv, float3 texel)
{
    const float3 dp1 = dfdx(p);
    const float3 dp2 = dfdy(p);
    const float2 duv1 = dfdx(uv);
    const float2 duv2 = dfdy(uv);
    const float3 dp2perp = cross(dp2, n);
    const float3 dp1perp = cross(n, dp1);
    const float3 t = dp2perp * duv1.x + dp1perp * duv2.x;
    const float3 b = dp2perp * duv1.y + dp1perp * duv2.y;
    const float scale = rsqrt(max(max(dot(t, t), dot(b, b)), 1e-20f));
    return normalize(float3x3(t * scale, -b * scale, n) * (texel * 2.0f - 1.0f));
}

static float3 brdf(Surface s, float3 toLight, float3 radiance)
{
    const float nDotL = dot(s.normal, toLight);
    if (nDotL <= 0.0f)
        return float3(0.0f);
    const float3 h = normalize(toLight + s.view);
    const float nDotV = max(dot(s.normal, s.view), 1e-4f);
    const float nDotH = max(dot(s.normal, h), 0.0f);
    const float vDotH = max(dot(s.view, h), 0.0f);
    const float alpha = max(s.roughness * s.roughness, 2e-3f);
    const float a2 = alpha * alpha;
    const float d = nDotH * nDotH * (a2 - 1.0f) + 1.0f;
    const float distribution = a2 / (kPi * d * d);
    const float gv = nDotL * sqrt(nDotV * nDotV * (1.0f - a2) + a2);
    const float gl = nDotV * sqrt(nDotL * nDotL * (1.0f - a2) + a2);
    const float visibility = 0.5f / max(gv + gl, 1e-6f);
    const float3 f0 = mix(float3(0.04f), s.albedo, s.metallic);
    const float3 fresnel = f0 + (1.0f - f0) * pow(1.0f - vDotH, 5.0f);
    const float3 diffuse = (1.0f - fresnel) * (1.0f - s.metallic) * s.albedo / kPi;
    return (diffuse + fresnel * distribution * visibility) * radiance * nDotL;
}

static float3 shadePunctual(Surface s, GpuLight light)
{
    float3 toLight = light.positionRange.xyz - s.position;
    const float distance2 = max(dot(toLight, toLight), 1e-4f);
    toLight *= rsqrt(distance2);
    const float ratio = distance2 / (light.positionRange.w * light.positionRange.w);
    const float window = clamp(1.0f - ratio * ratio, 0.0f, 1.0f);
    float attenuation = window * window / distance2;
    if (light.directionCosOuter.w > -1.0f)
        attenuation *= smoothstep(light.directionCosOuter.w, max(light.params.x, light.directionCosOuter.w + 1e-4f),
                                  dot(-toLight, light.directionCosOuter.xyz));
    return brdf(s, toLight, light.colorIntensity.rgb * light.colorIntensity.a * kPi * attenuation);
}

fragment float4 aura_fragment_3d(SceneVertexOut in [[stage_in]],
                                 constant LightingUniforms& lighting [[buffer(0)]],
                                 constant MaterialUniforms& material [[buffer(1)]],
                                 texture2d<float> albedo [[texture(0)]],
                                 sampler albedoSampler [[sampler(0)]],
                                 texture2d<float> normalMap [[texture(1)]],
                                 sampler normalSampler [[sampler(1)]],
                                 texture2d<float> metallicRoughnessMap [[texture(2)]],
                                 sampler metallicRoughnessSampler [[sampler(2)]],
                                 texture2d<float> emissiveMap [[texture(3)]],
                                 sampler emissiveSampler [[sampler(3)]])
{
    const float4 base = albedo.sample(albedoSampler, in.texCoord) * in.color * material.tint;
    float3 normal = normalize(in.normal);
    if ((material.maps & kNormalMap) != 0u)
        normal = perturbNormal(normal, in.worldPos, in.texCoord, normalMap.sample(normalSampler, in.texCoord).rgb);
    float roughness = material.roughness;
    float metallic = material.metallic;
    if ((material.maps & kMetallicRoughnessMap) != 0u)
    {
        const float4 mr = metallicRoughnessMap.sample(metallicRoughnessSampler, in.texCoord);
        roughness *= mr.g;
        metallic *= mr.b;
    }
    float3 emissive = float3(material.emissive);
    if ((material.maps & kEmissiveMap) != 0u)
        emissive *= emissiveMap.sample(emissiveSampler, in.texCoord).rgb;

    const Surface surface{base.rgb, normal, normalize(in.cameraPos - in.worldPos), in.worldPos, roughness, metallic};
    //! shadeLights() of pbr.glsl. Radiance times pi: intensity 1 lights a matte, non-metallic surface as the
    //! Lambert shader did.
    float3 colour = float3(0.0f);
    for (uint i = 0u; i < min(lighting.count, 8u); ++i)
    {
        const GpuLight light = lighting.lights[i];
        const float3 radiance = light.colorIntensity.rgb * light.colorIntensity.a;
        if (light.params.y == kLightDirectional)
            colour += brdf(surface, normalize(-light.directionCosOuter.xyz), radiance * kPi);
        else if (light.params.y == kLightAmbient)
            colour += radiance * surface.albedo;
        else
            colour += shadePunctual(surface, light);
    }
    return float4(colour + emissive, base.a);
}
