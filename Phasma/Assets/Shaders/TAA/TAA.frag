#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "TAA.glsl"

layout(set = 0, binding = 0) uniform sampler2D previousFrame;
layout(set = 0, binding = 1) uniform sampler2D currentFrame;
layout(set = 0, binding = 2) uniform sampler2D depth;
layout(set = 0, binding = 3) uniform sampler2D velocity;
layout(set = 0, binding = 4) uniform UniformBufferObject { vec4 values; vec4 sharp_values; mat4 invProj; } ubo;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outTaa;

#define EPSILON 0.00000001

float ComputeLuminance(vec3 color)
{
    // PAL and NTSC Y'=0.299R'+0.587G'+0.114B'
    // HDTV Y'=0.2126R'+0.7152G'+0.0722B'
    // HDR Y'=0.2627R'+0.6780G'+0.0593B'
    vec3 cLuma = color * vec3(0.2627, 0.6780, 0.0593);
    return cLuma.x + cLuma.y + cLuma.z;
}

void main()
{
    outTaa = ResolveTAA(inUV, previousFrame, currentFrame, velocity, depth, ubo.values.z, ubo.values.w, ubo.values.xy);
}
