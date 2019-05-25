#version 450
#extension GL_GOOGLE_include_directive : require

#include "../Common/common.glsl"

layout (set = 0, binding = 0) uniform sampler2D frameSampler;
layout (set = 0, binding = 1) uniform sampler2D depthSampler;
layout (set = 0, binding = 2) uniform sampler2D velocitySampler;
layout (set = 0, binding = 3) uniform UniformBufferObject { mat4 projection; mat4 view; mat4 previousView; mat4 invViewProj; } ubo;
layout(push_constant) uniform Constants { vec4 fps; } pushConst;


layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;

const int max_samples = 16;

void main() 
{
	vec2 UV = inUV;
	vec2 velocity = dilate_Average(velocitySampler, UV).xy;
	
	if (velocity.x + velocity.y == 0.0){
		outColor = texture(frameSampler, UV);
		return;
	}	

	velocity *= pushConst.fps.x; // fix for low and high fps giving different velocities
	velocity *= 0.01666666; // scale the effect 1/60

	vec3 color = vec3(0.0);
	int count = 0;
	UV -= velocity * 0.5; // make samples centered from (UV-velocity/2) to (UV+velocity/2) instead of (UV) to (UV+velocity)
	for (int i = 0; i < max_samples; i++, UV += velocity / max_samples)
	{
		if (!is_saturated(UV))
		{
			continue;
		}
		if (texture(frameSampler, UV).a > 0.001) // ignore transparent samples
		{
			color += texture(frameSampler, UV).xyz;
			count++;
		}
	}
	outColor = vec4(color / count, texture(frameSampler, inUV).w);
}