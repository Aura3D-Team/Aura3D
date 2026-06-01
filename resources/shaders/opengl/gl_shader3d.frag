#version 330 core

in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

out vec4 outColor;

uniform sampler2D textureSampler;

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
