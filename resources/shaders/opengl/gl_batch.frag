#version 330 core

in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 outColor;

uniform sampler2D textureSampler;
// WebGL2 has no texture swizzles; remap linear R8 coverage here.
uniform bool coverageOnly;

void main()
{
    vec4 texColor = texture(textureSampler, fragTexCoord);
    if (coverageOnly)
        texColor = vec4(1.0, 1.0, 1.0, texColor.r);
    outColor = texColor * fragColor;
}
