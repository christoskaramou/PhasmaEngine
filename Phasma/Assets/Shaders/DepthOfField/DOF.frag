#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "DOF.glsl"

layout(push_constant) uniform Constants { vec4 values; } constants;

layout (set = 0, binding = 0) uniform sampler2D sampler_color;
layout (set = 0, binding = 1) uniform sampler2D sampler_depth;

layout (location = 0) in vec2 in_UV;

layout (location = 0) out vec4 outColor;

void main()
{
    outColor.xyz = depthOfField(sampler_color, sampler_depth, in_UV, constants.values.x, constants.values.y);
    outColor.w = texture(sampler_color, in_UV).w;
}
