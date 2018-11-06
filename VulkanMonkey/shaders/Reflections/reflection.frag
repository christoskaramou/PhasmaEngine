#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (set = 0, binding = 0) uniform sampler2D albedoSampler;
layout (set = 0, binding = 1) uniform sampler2D positionSampler;
layout (set = 0, binding = 2) uniform sampler2D normalSampler;
layout (set = 0, binding = 3) uniform sampler2D depthSampler;
layout (set = 0, binding = 4) uniform WorldCameraPos{ vec4 camPos; vec4 camFront; vec4 size; vec4 dummy1; mat4 projection; mat4 view; } ubo;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;

const float near = 0.005;
const float far = 50.0;

// Screen space reflections
vec4 ScreenSpaceReflections(vec3 position, in vec3 normal)
{
	vec4 reflectedColor = vec4(0.0);

	vec4 projected = ubo.projection * vec4(position, 1.0);
	projected /= projected.w;
	vec3 viewReflection = reflect(normalize(position), normal);
	vec4 projectedRef = ubo.projection * vec4(viewReflection, 1.0);
	projectedRef /= projectedRef.w;

	vec3 step = normalize(projectedRef.xyz) * 0.05;
	vec3 newPosition = projectedRef.xyz + step;

	vec2 samplePosition = 0.5 * newPosition.xy + 0.5;
	for(int i=0; i<150; i++)
	{
		float sampledDepth = texture(depthSampler, samplePosition.xy).r;
		float currentDepth = newPosition.z;

		if(	samplePosition.x < 0.0 || samplePosition.x > 1.0 ||
			samplePosition.y < 0.0 || samplePosition.y > 1.0 ||
			newPosition.z < 0.0)
		{
			return vec4(0.0, 0.0, 0.0, 0.0);
		}

		float delta = abs(currentDepth - sampledDepth);
		if(delta < 0.0005f)
		{
			reflectedColor = texture(albedoSampler, samplePosition.xy);
		}
		if(currentDepth > sampledDepth)
		{
			newPosition -= step * 0.01;
			samplePosition = 0.5 * newPosition.xy + 0.5;
		}
		else if(currentDepth < sampledDepth)
		{
			newPosition += step * 0.01;
			samplePosition = 0.5 * newPosition.xy + 0.5;
		}
	}
	return reflectedColor;
}

void main()
{
	vec4 position = ubo.view * texture(positionSampler, inUV);
	vec4 normal = ubo.view * texture(normalSampler, inUV);
	outColor = texture(albedoSampler, inUV) + ScreenSpaceReflections(position.xyz, normalize(normal.xyz));
}