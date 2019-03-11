#version 450
#extension GL_GOOGLE_include_directive : require

#include "../Common/tonemapping.glsl"

layout (set = 0, binding = 0) uniform sampler2D compositionSampler;
layout (set = 0, binding = 1) uniform sampler2D gaussianVerticalSampler;
layout(push_constant) uniform Constants { float brightness; float intensity; float range; float exposure; float useTonemap; } values;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;
layout (location = 1) out vec4 outComposition;

void main() 
{
	vec4 sceneColor = texture(compositionSampler, inUV);
	vec4 bloom = vec4(texture(gaussianVerticalSampler, inUV).xyz, 0.0);

	outColor = sceneColor + bloom * values.intensity;
	if (values.useTonemap > 0.5)
		outColor.xyz = SRGBtoLINEAR(TonemapFilmic(outColor.xyz, values.exposure));
	outComposition = outColor;
}