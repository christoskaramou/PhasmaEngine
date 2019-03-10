#version 450
#extension GL_GOOGLE_include_directive : require

#include "FXAA.glsl"

layout (set = 0, binding = 0) uniform sampler2D compositionSampler;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;
layout (location = 1) out vec4 outComposition;

void main() 
{
	outColor.xyz = FXAA(compositionSampler, inUV);
	outColor.w = texture(compositionSampler, inUV).w;
	outComposition = outColor;
}