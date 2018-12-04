#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (set = 0, binding = 0) uniform sampler2D compositionSampler;
layout (set = 0, binding = 1) uniform sampler2D positionSampler;
layout (set = 0, binding = 2) uniform UniformBufferObject { mat4 projection; mat4 view; mat4 previousView;} ubo;
layout(push_constant) uniform Constants { vec4 fps; } pushConst;


layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;

const int samples = 8;

void main() 
{
	vec2 UV = inUV;

	vec4 currentPos = ubo.projection * ubo.view * vec4(texture(positionSampler, UV).rgb, 1.0);
	currentPos.xy = currentPos.xy * 0.5 + 0.5;
	vec4 previousPos = ubo.projection * ubo.previousView * vec4(texture(positionSampler, UV).rgb, 1.0);
	previousPos.xy = previousPos.xy * 0.5 + 0.5;

	vec2 velocity = clamp((currentPos.xy - previousPos.xy) * pushConst.fps.x * 0.001, -0.001, 0.001); // TODO: clamp this

	vec4 color = vec4(0.0);
	for (int i = 0; i < samples; i++, UV += velocity)
	{
		color += texture(compositionSampler, UV);
	}
	outColor = color / samples;
}