#version 450
#extension GL_GOOGLE_include_directive : require

#include "../Common/common.glsl"

layout (set = 0, binding = 0) uniform sampler2D compositionSampler;
layout (set = 0, binding = 1) uniform sampler2D depthSampler;
layout (set = 0, binding = 2) uniform sampler2D velocitySampler;
layout (set = 0, binding = 3) uniform UniformBufferObject { mat4 projection; mat4 view; mat4 previousView; mat4 invViewProj; } ubo;
layout(push_constant) uniform Constants { vec4 fps; vec4 offset; } pushConst;


layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;

const int max_samples = 16;

void main() 
{
	vec2 UV = inUV;

	vec3 modelVelocity = (ubo.view * vec4(dilate_Average(velocitySampler, UV), 0.0)).xyz;
	//vec3 modelVelocity = (ubo.view * vec4(dilate_Depth3X3(velocitySampler, depthSampler, UV), 0.0)).xyz;

	vec3 worldPos = getPosFromUV(UV, texture(depthSampler, UV).x, ubo.invViewProj, pushConst.offset);
	vec3 currentPos = (ubo.view * vec4(worldPos, 1.0)).xyz;
	vec3 previousPos = (ubo.previousView * vec4(worldPos, 1.0)).xyz;
	vec3 viewVelocity = currentPos - previousPos;
	
	vec2 velocity = (viewVelocity + modelVelocity).xy;
	
	if (velocity.x + velocity.y == 0.0){
		outColor = texture(compositionSampler, UV);
		return;
	}	

	velocity *= 0.5; // -0.5 to 0.5;
	velocity *= pushConst.offset.zw; // floating window velocity aspect correction
	velocity /= 1.0 + (currentPos.z + previousPos.z) * 0.5; // "1 + depth" division, so far pixels wont blur so much
	velocity *= pushConst.fps.x; // fix for low and high fps for giving different velocities
	velocity *= 0.05; // scale the effect

	ivec2 texDim = textureSize(velocitySampler, 0);
	vec2 size = vec2(float(texDim.x), float(texDim.y));
	float speed = length(velocity * size);
	int samples = clamp(int(speed), 1, max_samples);

	vec3 color = vec3(0.0);
	int count = 0;
	for (int i = 0; i < samples; i++, UV += velocity / samples)
	{
		UV = clamp(UV, pushConst.offset.xy + 0.0005, pushConst.offset.xy + pushConst.offset.zw - 0.0005);
		if (texture(compositionSampler, UV).a > 0.001) // ignore transparent samples
		{
			color += texture(compositionSampler, UV).xyz;
			count++;
		}
	}
	outColor = vec4(color / count, texture(compositionSampler, inUV).w);
}