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

vec4 ScreenSpaceReflections(vec3 position, vec3 normal);
mat4 viewNoTranslation = ubo.view;

void main()
{
	// keep the rotation of the view matrix and discard its translation for correct normals
	
	viewNoTranslation[3] = vec4(0.0, 0.0, 0.0, 1.0);

	vec4 position = texture(positionSampler, inUV);
	vec4 normal = texture(normalSampler, inUV);
	//outColor = ScreenSpaceReflections(position.xyz, normalize(normal.xyz));
	outColor = texture(albedoSampler, inUV) + ScreenSpaceReflections(position.xyz, normalize(normal.xyz));
	//outColor = normal;
}

// Screen space reflections
vec4 ScreenSpaceReflections(vec3 position, vec3 normal)
{
	vec3 camPos = ubo.camPos.xyz;
	vec4 reflectedColor = vec4(0.0);
	
	vec3 reflection = reflect(position - ubo.camPos.xyz, normal);

	vec3 step = reflection;
	vec3 newPosition = position + step;

	for(int i=0; i<20; i++)
	{
		vec4 newViewPos = ubo.view * vec4(newPosition, 1.0);
		vec4 samplePosition = ubo.projection * newViewPos;
		samplePosition /= samplePosition.w;
		samplePosition.xy = 0.5 * samplePosition.xy + 0.5;

		float currentDepth = newViewPos.z;
		float sampledDepth = texture(positionSampler, samplePosition.xy).w;

		if(	samplePosition.x < 0.0 || samplePosition.x > 1.0 ||
			samplePosition.y < 0.0 || samplePosition.y > 1.0 )
		{
			return vec4(0.0, 0.0, 0.0, 0.0);
		}
		
		if( newViewPos.z < 0.0 )
		{
			return vec4(0.0, 0.0, 0.0, 0.0);
		}

		float delta = abs(currentDepth - sampledDepth);
		if(delta < 0.01f)
		{
			reflectedColor = texture(albedoSampler, samplePosition.xy);
		}
		if(currentDepth > sampledDepth)
		{
			step *= 0.5;
			newPosition -= step;
		}
		else if(currentDepth < sampledDepth)
		{
			newPosition += step;
		}
	}
	return reflectedColor;
}