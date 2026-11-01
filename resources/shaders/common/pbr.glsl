// Metallic-roughness shading for the GLSL scene shaders, Vulkan and OpenGL alike; mtl_shader3d.metal
// mirrors it. GLSL 3.30 on purpose: OpenGL compiles it as is, glslc through #include.

const uint kNormalMap = 1u;
const uint kMetallicRoughnessMap = 2u;
const uint kEmissiveMap = 4u;
const float kPi = 3.14159265;
// aura3d::gfx::LightType, as LightingUBO::GpuLight::params.y carries it.
const float kLightDirectional = 0.0;
const float kLightAmbient = 3.0;

struct Surface {
    vec3 albedo;
    vec3 normal;
    vec3 view;     // towards the camera
    vec3 position;
    float roughness;
    float metallic;
};

// Tangent frame from screen-space derivatives, so meshes need no tangents. A glTF (OpenGL) normal
// map's +Y points up the image, against +v, hence the negated bitangent.
vec3 perturbNormal(vec3 n, vec3 p, vec2 uv, vec3 texel)
{
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, n);
    vec3 dp1perp = cross(n, dp1);
    vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;
    float scale = inversesqrt(max(max(dot(t, t), dot(b, b)), 1e-20));
    return normalize(mat3(t * scale, -b * scale, n) * (texel * 2.0 - 1.0));
}

// GGX distribution, height-correlated Smith visibility, Schlick Fresnel.
vec3 brdf(Surface s, vec3 toLight, vec3 radiance)
{
    float nDotL = dot(s.normal, toLight);
    if (nDotL <= 0.0)
        return vec3(0.0);
    vec3 h = normalize(toLight + s.view);
    float nDotV = max(dot(s.normal, s.view), 1e-4);
    float nDotH = max(dot(s.normal, h), 0.0);
    float vDotH = max(dot(s.view, h), 0.0);
    float alpha = max(s.roughness * s.roughness, 2e-3);
    float a2 = alpha * alpha;
    float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    float distribution = a2 / (kPi * d * d);
    float gv = nDotL * sqrt(nDotV * nDotV * (1.0 - a2) + a2);
    float gl = nDotV * sqrt(nDotL * nDotL * (1.0 - a2) + a2);
    float visibility = 0.5 / max(gv + gl, 1e-6);
    vec3 f0 = mix(vec3(0.04), s.albedo, s.metallic);
    vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - vDotH, 5.0);
    vec3 diffuse = (1.0 - fresnel) * (1.0 - s.metallic) * s.albedo / kPi;
    return (diffuse + fresnel * distribution * visibility) * radiance * nDotL;
}

// Radiance times pi: intensity 1 lights a matte, non-metallic surface as the Lambert shaders did.
vec3 shadeDirectional(Surface s, vec3 direction, vec3 radiance)
{
    return brdf(s, normalize(-direction), radiance * kPi);
}

vec3 shadePunctual(Surface s, vec4 positionRange, vec4 colorIntensity, vec4 directionCosOuter, float cosInner)
{
    vec3 toLight = positionRange.xyz - s.position;
    float distance2 = max(dot(toLight, toLight), 1e-4);
    toLight *= inversesqrt(distance2);
    // Inverse square, windowed to reach zero at the range.
    float ratio = distance2 / (positionRange.w * positionRange.w);
    float window = clamp(1.0 - ratio * ratio, 0.0, 1.0);
    float attenuation = window * window / distance2;
    if (directionCosOuter.w > -1.0)
        attenuation *= smoothstep(directionCosOuter.w, max(cosInner, directionCosOuter.w + 1e-4),
                                  dot(-toLight, directionCosOuter.xyz));
    return brdf(s, toLight, colorIntensity.rgb * colorIntensity.a * kPi * attenuation);
}

// No environment lighting yet: the flat ambient term the Lambert shaders had.
vec3 ambientTerm(Surface s, vec3 ambient)
{
    return ambient * s.albedo;
}

// Every light in the lighting block, in its order, so a scene sums the same way on every GPU backend.
vec3 shadeLights(Surface s)
{
    vec3 colour = vec3(0.0);
    for (uint i = 0u; i < min(lighting.count, 8u); ++i)
    {
        GpuLight light = lighting.lights[i];
        vec3 radiance = light.colorIntensity.rgb * light.colorIntensity.a;
        if (light.params.y == kLightDirectional)
            colour += shadeDirectional(s, light.directionCosOuter.xyz, radiance);
        else if (light.params.y == kLightAmbient)
            colour += ambientTerm(s, radiance);
        else
            colour += shadePunctual(s, light.positionRange, light.colorIntensity, light.directionCosOuter,
                                    light.params.x);
    }
    return colour;
}
