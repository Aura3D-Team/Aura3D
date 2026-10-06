#version 330 core

// texelFetch decodes the sRGB target and the encode is done here: the window's own sRGB
// handling cannot be relied on (WebGL has none, drivers misreport it). highp because GLES
// samplers default to lowp, which would band the decoded darks.
uniform highp sampler2D target;

out vec4 outColor;

vec3 encode(vec3 linear)
{
    vec3 curve = 1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055;
    return mix(curve, linear * 12.92, lessThanEqual(linear, vec3(0.0031308)));
}

void main()
{
    vec4 color = texelFetch(target, ivec2(gl_FragCoord.xy), 0);
    outColor = vec4(encode(color.rgb), color.a);
}
