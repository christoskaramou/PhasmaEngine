#version 450

layout (set = 0, binding = 0) uniform sampler2D compositionSampler;
layout(push_constant) uniform Constants { float brightness; float intensity; float range; float exposure; } values;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;

void main() 
{
	vec4 color = texture(compositionSampler, inUV);
	float brightness = color.x * 0.2627 + color.y * 0.678 + color.z * 0.0593;
	outColor = vec4(color.xyz * pow(brightness, values.brightness), 1.0);
}