#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPos;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D textureSampler;

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

    vec4 texColor = texture(textureSampler, fragTexCoord);
    outColor = texColor * fragColor * vec4(light.color.rgb * lighting, 1.0);
}
