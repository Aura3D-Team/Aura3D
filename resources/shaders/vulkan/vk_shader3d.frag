#version 450

/*
 * Enables the unsized-descriptor-array syntax below. Despite the name, this
 * only permits the *declaration* and variable indexing -- it does not by
 * itself emit a non-uniform-indexing SPIR-V capability, because no index is
 * wrapped in nonuniformEXT(). The compiled module therefore requires only
 * RuntimeDescriptorArray, matching the features VkDeviceManager enables.
 */
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPos;
layout(location = 4) flat in uint fragTextureIndex;
layout(location = 5) flat in vec3 fragCameraPos;
layout(location = 6) flat in uint fragMaterialIndex;

layout(location = 0) out vec4 outColor;

/*
 * Bindless texture table: one combined-image-sampler binding covering every
 * texture the renderer has created, indexed per draw. Unsized because its real
 * length is the device's update-after-bind limit, resolved at runtime. Every
 * index here comes from a push constant or the material table at a push-constant
 * index, so it is dynamically uniform and needs no nonuniformEXT.
 */
layout(set = 1, binding = 0) uniform sampler2D textureSamplers[];

struct GpuLight {
    vec4 positionRange;
    vec4 colorIntensity;
    vec4 directionCosOuter;
    vec4 params; // x cos(inner), y the LightType
};

// Mirrors aura3d::gfx::LightingUBO (std140).
layout(set = 2, binding = 0) uniform LightingBlock {
    uint  count;
    uint  pad0;
    uint  pad1;
    uint  pad2;
    GpuLight lights[8];
} lighting;

// Mirrors aura3d::gfx::MaterialUniforms; entry 0 is the default material.
struct Material {
    vec4 tint;
    vec3 emissive;
    float roughness;
    float metallic;
    uint maps;
    uint normalMap;
    uint metallicRoughnessMap;
    uint emissiveMap;
};
layout(std430, set = 2, binding = 1) readonly buffer MaterialTable {
    Material materials[];
};

#include "pbr.glsl"

void main()
{
    const Material material = materials[fragMaterialIndex];
    vec4 albedo = texture(textureSamplers[fragTextureIndex], fragTexCoord) * fragColor * material.tint;

    vec3 normal = normalize(fragNormal);
    if ((material.maps & kNormalMap) != 0u)
        normal = perturbNormal(normal, fragPos, fragTexCoord,
                               texture(textureSamplers[material.normalMap], fragTexCoord).rgb);
    float roughness = material.roughness;
    float metallic = material.metallic;
    if ((material.maps & kMetallicRoughnessMap) != 0u)
    {
        const vec4 mr = texture(textureSamplers[material.metallicRoughnessMap], fragTexCoord);
        roughness *= mr.g;
        metallic *= mr.b;
    }
    vec3 emissive = material.emissive;
    if ((material.maps & kEmissiveMap) != 0u)
        emissive *= texture(textureSamplers[material.emissiveMap], fragTexCoord).rgb;

    Surface surface = Surface(albedo.rgb, normal, normalize(fragCameraPos - fragPos), fragPos, roughness, metallic);
    vec3 colour = shadeLights(surface) + emissive;
    outColor = vec4(colour, albedo.a);
}
