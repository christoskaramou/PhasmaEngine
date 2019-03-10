#version 450

layout (set = 0, binding = 0) uniform sampler2D compositionSampler;
layout (set = 0, binding = 1) uniform sampler2D gaussianVerticalSampler;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;
layout (location = 1) out vec4 outComposition;

void main() 
{
	vec4 sceneColor = texture(compositionSampler, inUV);
	vec4 bloom = vec4(texture(gaussianVerticalSampler, inUV).xyz, 0.0);

	outColor = sceneColor + bloom * 0.2;
	outComposition = outColor;
}