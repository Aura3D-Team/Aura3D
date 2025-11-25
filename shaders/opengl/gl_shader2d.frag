#version 330 core

in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 outColor;

uniform sampler2D textureSampler;

void main()
{
    vec4 texColor = texture(textureSampler, fragTexCoord);
    outColor = texColor * fragColor;
}
