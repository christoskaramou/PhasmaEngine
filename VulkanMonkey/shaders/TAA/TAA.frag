#version 450

#extension GL_GOOGLE_include_directive : require
#include "TAA.glsl"

layout(set = 0, binding = 0) uniform sampler2D previousFrame;
layout(set = 0, binding = 1) uniform sampler2D currentFrame;
layout(set = 0, binding = 2) uniform sampler2D depth;
layout(set = 0, binding = 3) uniform sampler2D velocity;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outTaa;

void main()
{
	outTaa = ResolveTAA(inUV, previousFrame, currentFrame, velocity, depth);
}