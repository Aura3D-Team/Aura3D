#version 450

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

layout(location = 0) out vec4 outColor;

//! Set 0 is this pipeline's only descriptor set: one combined image sampler.
layout(set = 0, binding = 0) uniform sampler2D textureSampler;

void main()
{
    outColor = texture(textureSampler, fragTexCoord) * fragColor;
}
