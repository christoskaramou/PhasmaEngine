#version 450

#extension GL_GOOGLE_include_directive : require
#include "TAA.glsl"

layout(set = 0, binding = 0) uniform sampler2D previousFrame;
layout(set = 0, binding = 1) uniform sampler2D composition;
layout(set = 0, binding = 2) uniform sampler2D depth;
layout(set = 0, binding = 3) uniform sampler2D velocity;
layout(set = 0, binding = 4) uniform UniformBufferObject { mat4 invVP; mat4 previousPV; vec4 values; } ubo;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;
layout (location = 1) out vec4 outComposition2;

void main()
{
	//outColor = mix(texture(composition, inUV), texture(previousFrame, inUV), ubo.values.z);
	outColor = ResolveTAA(inUV, previousFrame, composition, velocity, depth);
	outComposition2 = outColor;
}