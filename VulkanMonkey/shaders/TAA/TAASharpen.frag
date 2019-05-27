#version 450

#extension GL_GOOGLE_include_directive : require
#include "../Common/common.glsl"

layout(set = 0, binding = 0) uniform sampler2D taa;
layout(set = 0, binding = 1) uniform UniformBufferObject { vec4 values; vec4 sharp_values;} ubo;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;

void main()
{
	outColor = vec4(LumaSharpen(taa, inUV, ubo.sharp_values.x, ubo.sharp_values.y, ubo.sharp_values.z), texture(taa, inUV).a);
}