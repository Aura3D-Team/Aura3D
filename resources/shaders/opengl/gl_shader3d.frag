#version 330 core

in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;
in vec3 fragPos;
flat in vec3 fragCameraPos;

out vec4 outColor;

uniform sampler2D textureSampler;
uniform sampler2D normalSampler;
uniform sampler2D metallicRoughnessSampler;
uniform sampler2D emissiveSampler;
// WebGL2 has no texture swizzles; remap linear R8 coverage here.
uniform bool coverageOnly;

struct GpuLight {
    vec4 positionRange;
    vec4 colorIntensity;
    vec4 directionCosOuter;
    vec4 params; // x cos(inner), y the LightType
};

// Mirrors aura3d::gfx::LightingUBO (std140).
layout(std140) uniform LightingBlock {
    uint  count;
    uint  pad0;
    uint  pad1;
    uint  pad2;
    GpuLight lights[8];
} lighting;

// Mirrors aura3d::gfx::MaterialUniforms (std140); the maps are texture units 1-3.
layout(std140) uniform MaterialBlock {
    vec4 tint;
    vec3 emissive;
    float roughness;
    float metallic;
    uint maps;
} material;

#include "pbr.glsl"

void main()
{
    vec4 texColor = texture(textureSampler, fragTexCoord);
    if (coverageOnly)
        texColor = vec4(1.0, 1.0, 1.0, texColor.r);
    vec4 albedo = texColor * fragColor * material.tint;

    vec3 normal = normalize(fragNormal);
    if ((material.maps & kNormalMap) != 0u)
        normal = perturbNormal(normal, fragPos, fragTexCoord, texture(normalSampler, fragTexCoord).rgb);
    float roughness = material.roughness;
    float metallic = material.metallic;
    if ((material.maps & kMetallicRoughnessMap) != 0u)
    {
        vec4 mr = texture(metallicRoughnessSampler, fragTexCoord);
        roughness *= mr.g;
        metallic *= mr.b;
    }
    vec3 emissive = material.emissive;
    if ((material.maps & kEmissiveMap) != 0u)
        emissive *= texture(emissiveSampler, fragTexCoord).rgb;

    Surface surface = Surface(albedo.rgb, normal, normalize(fragCameraPos - fragPos), fragPos, roughness, metallic);
    vec3 colour = shadeLights(surface) + emissive;
    outColor = vec4(colour, albedo.a);
}
