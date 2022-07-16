#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "../Common/common.glsl"

layout(push_constant) uniform Constants {
    mat4 inv_view_proj;
    //vec4 light_dir;
} constants;

layout (set = 0, binding = 0) uniform sampler2D sampler_color;
layout (set = 0, binding = 1) uniform sampler2D sampler_depth;

layout (location = 0) in vec2 in_UV;

layout (location = 0) out vec4 outColor;

void main()
{
    vec3 position = GetPosFromUV(in_UV, texture(sampler_depth, in_UV).x, constants.inv_view_proj);

    outColor.xyz = position;
    outColor.w = 1.f;
}
