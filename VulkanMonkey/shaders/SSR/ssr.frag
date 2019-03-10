#version 450
#extension GL_GOOGLE_include_directive : require

#include "../Common/common.glsl"

layout (set = 0, binding = 0) uniform sampler2D albedoSampler;
layout (set = 0, binding = 1) uniform sampler2D depthSampler;
layout (set = 0, binding = 2) uniform sampler2D normalSampler;
layout (set = 0, binding = 3) uniform sampler2D specRoughMetSampler; // TODO: remove this
layout (set = 0, binding = 4) uniform WorldCameraPos{ vec4 camPos; vec4 camFront; vec4 size; vec4 dummy1; mat4 projection; mat4 view; mat4 invProj; } ubo;
layout(push_constant) uniform Position { vec4 offset; } pos;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;

vec3 ScreenSpaceReflections(vec3 position, vec3 normal);

void main()
{
	vec3 position = getPosFromUV(inUV, texture(depthSampler, inUV).x, ubo.invProj, pos.offset);
	vec4 normal = ubo.view * vec4(texture(normalSampler, inUV).xyz, 0.0);

	outColor = vec4(ScreenSpaceReflections(position, normalize(normal.xyz)) , 1.0);
}

// Screen space reflections
vec3 ScreenSpaceReflections(vec3 position, vec3 normal)
{
	vec3 reflection = reflect(position, normal);

	float VdotR = max(dot(normalize(position), normalize(reflection)), 0.0);
	float fresnel = pow(VdotR, 5); // small hack, not fresnel at all

	vec3 step = reflection;
	vec3 newPosition = position + step;

	float loops = max(sign(VdotR), 0.0) * 30;
	for(int i=0; i<loops ; i++)
	{
		vec4 newViewPos = vec4(newPosition, 1.0);
		vec4 samplePosition = ubo.projection * newViewPos;
		samplePosition.xy = (samplePosition.xy / samplePosition.w) * 0.5 + 0.5;
		samplePosition.xy *= pos.offset.zw; // floating window size correction
		samplePosition.xy += pos.offset.xy; // floating window position correction

		vec2 checkBoundsUV = max(sign(samplePosition.xy * (1.0 - samplePosition.xy)), 0.0);
		if (checkBoundsUV.x * checkBoundsUV.y < 1.0){
			step *= 0.5;
			newPosition -= step;
			continue;
		}

		float currentDepth = abs(newViewPos.z);
		float sampledDepth = abs(getPosFromUV(samplePosition.xy, texture(depthSampler, samplePosition.xy).x, ubo.invProj, pos.offset).z);

		float delta = abs(currentDepth - sampledDepth);
		if(delta < 0.01f)
		{
			vec2 reverted = (samplePosition.xy - pos.offset.xy) / pos.offset.zw; // floating window correction
			vec2 fadeOnEdges = reverted * 2.0 - 1.0;
			fadeOnEdges = abs(fadeOnEdges);
			float fadeAmount = min(1.0 - fadeOnEdges.x, 1.0 - fadeOnEdges.y);

			return texture(albedoSampler, samplePosition.xy).xyz * fresnel * fadeAmount;
		}

		step *= 1.0 - 0.5 * max(sign(currentDepth - sampledDepth), 0.0);
		newPosition += step * (sign(sampledDepth - currentDepth) + 0.000001);
	}
	return vec3(0.0);
}