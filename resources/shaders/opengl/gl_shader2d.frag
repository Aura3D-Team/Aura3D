#version 330 core

/*
 * Overlay 2D fragment stage: unlit on purpose. Mirrors EmbeddedShaders.h's
 * GL_FRAGMENT_2D; keep the two in sync.
 *
 * The result is texel * vertexColor and nothing else, so an overlay keeps
 * exactly the colour it asked for regardless of the scene's lighting. Glyph
 * atlases store white RGB with coverage in alpha, which makes vertex colour the
 * sole source of text colour.
 */

in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 outColor;

uniform sampler2D textureSampler;

void main()
{
    outColor = texture(textureSampler, fragTexCoord) * fragColor;
}
