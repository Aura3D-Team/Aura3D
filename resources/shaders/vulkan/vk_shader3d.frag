#version 450

/*
 * Enables the unsized-descriptor-array syntax below. Despite the name, this
 * only permits the *declaration* and variable indexing -- it does not by
 * itself emit a non-uniform-indexing SPIR-V capability, because no index is
 * wrapped in nonuniformEXT(). The compiled module therefore requires only
 * RuntimeDescriptorArray, matching the features VkDeviceManager enables.
 */
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPos;
layout(location = 4) flat in uint fragTextureIndex;

layout(location = 0) out vec4 outColor;

/*
 * Bindless texture table: one combined-image-sampler binding covering every
 * texture the renderer has created, indexed per-draw by fragTextureIndex
 * instead of rebinding a descriptor per texture.
 *
 * Declared unsized (runtimeDescriptorArray) on purpose: the real slot count
 * is whatever VkDeviceManager::maxBindlessTextures() resolved against the
 * device's update-after-bind limits, which differs per GPU and cannot be
 * baked into the SPIR-V this engine ships prebuilt. A hardcoded size here
 * would fail to create the layout on any device whose limits are lower.
 *
 * No nonuniformEXT: fragTextureIndex comes from a push constant, so it is
 * constant across the whole draw call (dynamically uniform), which is
 * exactly the case the spec permits indexing without
 * shaderSampledImageArrayNonUniformIndexing. A future per-fragment material
 * lookup (deferred shading, GPU-driven batching) would need both that
 * feature and a nonuniformEXT() around the index.
 */
layout(set = 1, binding = 0) uniform sampler2D textureSamplers[];

// Mirrors aura3d::gfx::LightUBO (std140).
layout(set = 2, binding = 0) uniform LightBlock {
    vec3  direction;
    float intensity;
    vec4  color;
    float ambient;
} light;

void main()
{
    // direction is the way the light travels, so the vector towards it is negated.
    vec3 toLight = normalize(-light.direction);
    vec3 norm = normalize(fragNormal);

    float diffuse = max(dot(norm, toLight), 0.0) * light.intensity;
    float lighting = light.ambient + diffuse;

    vec4 texColor = texture(textureSamplers[fragTextureIndex], fragTexCoord);
    outColor = texColor * fragColor * vec4(light.color.rgb * lighting, 1.0);
}
