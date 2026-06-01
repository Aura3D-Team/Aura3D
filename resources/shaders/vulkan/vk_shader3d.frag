#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in vec3 fragNormal;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D textureSampler;

void main()
{
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 norm = normalize(fragNormal);
    float diffuse = max(dot(norm, lightDir), 0.0);
    float ambient = 0.15;
    float lighting = ambient + diffuse * 0.85;

    vec4 texColor = texture(textureSampler, fragTexCoord);
    outColor = texColor * fragColor * vec4(vec3(lighting), 1.0);
}
