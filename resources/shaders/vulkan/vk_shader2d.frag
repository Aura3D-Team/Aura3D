#version 450

/*
 * Enables the unsized-descriptor-array syntax below. Despite the name, this
 * only permits the *declaration* and variable indexing -- it does not by
 * itself emit a non-uniform-indexing SPIR-V capability, because no index is
 * wrapped in nonuniformEXT(). The compiled module therefore requires only
 * RuntimeDescriptorArray, matching the features VkDeviceManager enables.
 */
#extension GL_EXT_nonuniform_qualifier : require

/*
 * Overlay 2D fragment stage: unlit on purpose.
 *
 * The result is texel * vertexColor and nothing else -- no light, no ambient
 * term -- so an overlay keeps exactly the colour it asked for regardless of
 * what the scene's lighting is doing. Glyph atlases store white RGB with
 * coverage in alpha, which makes vertex colour the sole source of text colour.
 */

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) flat in uint fragTextureIndex;

layout(location = 0) out vec4 outColor;

/*
 * Set 0 is this pipeline's only descriptor set: a bindless texture table
 * covering every texture the renderer has created, indexed per-batch by
 * fragTextureIndex instead of rebinding this descriptor per texture.
 * Unsized for the same reason as the 3D shader's -- see vk_shader3d.frag.
 */
layout(set = 0, binding = 0) uniform sampler2D textureSamplers[];

void main()
{
    outColor = texture(textureSamplers[fragTextureIndex], fragTexCoord) * fragColor;
}
