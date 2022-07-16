#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "FXAA.glsl"

layout (set = 0, binding = 0) uniform sampler2D frameSampler;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;

void main()
{
    outColor.xyz = FXAA(frameSampler, inUV);
    outColor.w = texture(frameSampler, inUV).w;
}
