/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

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
	vec2 velocity = DilateDepth3X3(velocitySampler, depthSampler, UV).xy;
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
		if (!IsSaturated(UV))
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