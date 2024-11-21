#version 450

// Inputs from the vertex shader
layout(location = 0) in vec3 vUV;         // 3D UV coordinates
layout(location = 1) in vec3 vNormal;     // Normal vector
layout(location = 2) in vec3 vPosition;   // World-space position

// Outputs to the framebuffer
layout(location = 0) out vec4 outColor;

// Uniform sampler for 3D texture
layout(binding = 1) uniform sampler3D uTexture3D; // 3D texture

// Uniforms for material properties
layout(binding = 2) uniform vec3 uColor;         // Base color
layout(binding = 3) uniform float uAlpha;       // Transparency
layout(binding = 4) uniform float uSpecular;    // Specular intensity
layout(binding = 5) uniform vec3 uLightPosition; // Light position
layout(binding = 6) uniform vec3 uLightColor;   // Light color

// Utility function for simple lighting
vec3 computeLighting(vec3 normal, vec3 fragPos) {
    vec3 lightDir = normalize(uLightPosition - fragPos);
    float diff = max(dot(normal, lightDir), 0.0); // Diffuse component
    vec3 reflectDir = reflect(-lightDir, normal);
    vec3 viewDir = normalize(-fragPos);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0) * uSpecular; // Specular component
    return diff * uLightColor + spec * uLightColor; // Combine diffuse and specular
}

void main() {
    // Sample the 3D texture
    vec4 texColor = texture(uTexture3D, vUV);

    // Compute lighting
    vec3 lighting = computeLighting(normalize(vNormal), vPosition);

    // Combine base color, texture, and lighting
    vec3 finalColor = (uColor * texColor.rgb) * lighting;

    // Output final color with transparency
    outColor = vec4(finalColor, texColor.a * uAlpha);
}

