#version 450
#extension GL_GOOGLE_include_directive : require

#include "../Common/common.glsl"

layout (set = 0, binding = 0) uniform sampler2D frameSampler;
layout (set = 0, binding = 1) uniform sampler2D depthSampler;
layout (set = 0, binding = 2) uniform sampler2D velocitySampler;
layout (set = 0, binding = 3) uniform UniformBufferObject { mat4 projection; mat4 view; mat4 previousView; mat4 invViewProj; } ubo;
layout(push_constant) uniform Constants { vec4 values; } pushConst;


layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;

const int max_samples = 16;

void main() 
{
	vec2 UV = inUV;
	vec2 velocity = dilate_Depth3X3(velocitySampler, depthSampler, UV).xy;
	velocity *= pushConst.values.z; // strength 
	
	if (length2(velocity) < FLT_EPS){
		outColor = texture(frameSampler, UV);
		return;
	}	
	
	velocity *= pushConst.values.x; // fix for low and high fps giving different velocities
	velocity *= 0.01666666; // scale the effect 1/60
	
	vec3 color = texture(frameSampler, UV).xyz;
	float totalWeight = 0.0;
	float curWeight = 1.0;
	float factor = 1.0 / max_samples;
	vec2 step = velocity * factor;
	UV += velocity * 0.5; // make samples centered from (UV+velocity/2) to (UV-velocity/2) instead of (UV) to (UV-velocity)
	for (int i = 0; i < max_samples; i++, UV -= step)
	{
		if (!is_saturated(UV))
		{
			continue;
		}
		vec4 texCol = texture(frameSampler, UV);
		if (texCol.a > 0.001) // ignore transparent samples
		{
			curWeight -= factor * 0.5; // curWeight can go negative, but most of the times must stay higher than zero
			color += texCol.xyz * curWeight;
			totalWeight += curWeight;
		}
	}
	outColor = vec4(color / (totalWeight + 1), texture(frameSampler, inUV).w);
}