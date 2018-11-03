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
const bool fadeToEdges = false;
const bool toggleBlur = false;
const float user_pixelStepSize = 1.0f;

// Screen space reflections
vec4 ScreenSpaceReflections(vec2 UV, in vec3 normal)
{
	vec4 reflectedColor = vec4(0.0);
	vec3 UVPosition = vec3(UV, texture(depthSampler, UV).r);
	vec4 ndcPosition = vec4(2.0 * UVPosition.xy - 1.0, 0.99984, UVPosition.z);
	vec4 csPosition = inverse(ubo.projection) * ndcPosition;
	vec3 csReflection = reflect(vec3(csPosition.xy / csPosition.w, csPosition.z/csPosition.w), normal);
	vec4 ndcReflection = ubo.projection * vec4(csReflection, 1.0); ndcReflection.xy /= ndcReflection.w;
	vec3 step = normalize(ndcReflection.xyz) * 0.05;
	vec3 newPosition = UVPosition + step;
	//return vec4(vec3(csPosition.xy/ csPosition.w, csPosition.z), 1.0);

	vec2 samplePosition = 0.5 * newPosition.xy + 0.5;
	for(int i=0; i<150; i++)
	{
		float sampledDepth = texture(depthSampler, samplePosition.xy).r;
		float currentDepth = newPosition.z;

		if(	samplePosition.x < 0.0 || samplePosition.x > 1.0 ||
			samplePosition.y < 0.0 || samplePosition.y > 1.0 ||
			newPosition.z < 0.0)
		{
			return vec4(0.0, 1.0, 0.0, 1.0);
		}

		float delta = abs(currentDepth - sampledDepth);
		if(delta < 0.005f)
		{
			texture(albedoSampler, samplePosition.xy);
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
	vec4 normal = ubo.view * texture(normalSampler, inUV);
	outColor = texture(albedoSampler, inUV) + ScreenSpaceReflections(inUV, normalize(normal.xyz));
	//outColor = ScreenSpaceReflections(inUV, normalize(normal.xyz));
	//outColor = normal;
} 