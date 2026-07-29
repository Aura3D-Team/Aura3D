#version 330 core

in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;
in vec3 fragPos;

out vec4 outColor;

uniform sampler2D textureSampler;

// Mirrors aura3d::gfx::LightUBO (std140).
layout(std140) uniform LightBlock {
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

    vec4 texColor = texture(textureSampler, fragTexCoord);
    outColor = texColor * fragColor * vec4(light.color.rgb * lighting, 1.0);
}
