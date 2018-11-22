#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (set = 0, binding = 0) uniform sampler2D albedoSampler;
layout (set = 0, binding = 1) uniform sampler2D positionSampler;
layout (set = 0, binding = 2) uniform sampler2D normalSampler;
layout (set = 0, binding = 3) uniform sampler2D specSampler;
layout (set = 0, binding = 4) uniform WorldCameraPos{ vec4 camPos; vec4 camFront; vec4 size; vec4 dummy1; mat4 projection; mat4 view; } ubo;
layout(push_constant) uniform Position { vec4 offset; } pos;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;

vec3 ScreenSpaceReflections(vec3 position, vec3 normal);

void main()
{
	vec3 position = texture(positionSampler, inUV).xyz;
	vec3 normal = texture(normalSampler, inUV).xyz;
	float specular = texture(specSampler, inUV).r;

	// The swapchain image is loaded and not cleared in this render pass
	outColor = vec4(ScreenSpaceReflections(position, normalize(normal)), 0.5) * specular;
}

// Screen space reflections
vec3 ScreenSpaceReflections(vec3 position, vec3 normal)
{
	vec3 viewPos = position - ubo.camPos.xyz;
	vec3 reflection = reflect(viewPos, normal);

	float VdotR = max(dot(normalize(viewPos), normalize(reflection)), 0.0);
	float fresnel = pow(VdotR, 5);

	vec3 step = reflection;
	vec3 newPosition = position + step;

	float loops = max(sign(VdotR), 0.0) * 30;
	for(int i=0; i<loops ; i++)
	{
		vec4 newViewPos = ubo.view * vec4(newPosition, 1.0);
		vec4 samplePosition = ubo.projection * newViewPos;
		samplePosition.xy = samplePosition.xy / samplePosition.w * 0.5 + 0.5;
		samplePosition.xy *= pos.offset.zw; // floating window size correction
		samplePosition.xy += pos.offset.xy; // floating window position correction

		vec2 checkBoundsUV = max(sign(samplePosition.xy * (1.0 - samplePosition.xy)), 0.0);
		if (checkBoundsUV.x * checkBoundsUV.y < 1.0){
			step *= 0.5;
			newPosition -= step;
			continue;
		}

		float currentDepth = abs(newViewPos.z);
		float sampledDepth = abs(texture(positionSampler, samplePosition.xy).w);

		float delta = abs(currentDepth - sampledDepth);
		if(delta < 0.001f)
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