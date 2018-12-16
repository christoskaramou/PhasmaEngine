#version 450

layout(set = 2, binding = 0) uniform UniformBufferObject {
	mat4 projection;
	mat4 view;
	mat4 model;
} ubo;

layout(set = 0, binding = 0) uniform shadowBufferObject {
	mat4 projection;
	mat4 view;
	mat4 model;
	float castShadows;
}sun;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoords;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;
layout(location = 5) in vec4 inColor;

layout (location = 0) out vec3 outFragPos;
layout (location = 1) out vec2 outUV;
layout (location = 2) out vec3 outNormal;
layout (location = 3) out vec3 outTangent;
layout (location = 4) out vec3 outBitangent;
layout (location = 5) out vec3 outColor;
layout (location = 6) out float castShadows;
layout (location = 7) out mat4 shadow_coords;

void main()
{
	vec4 inPos = vec4(inPosition, 1.0f);

	gl_Position = ubo.projection * ubo.view * ubo.model * inPos;

	// Fragment in world space
	outFragPos = (ubo.model * inPos).xyz;
	// UV
	outUV = inTexCoords;
	// Normal in world space
	outNormal = inNormal;
	// Tangent in world space
	outTangent = inTangent;
	// Bitangent in world space
	outBitangent = inBitangent;
	// Color (not in use)
	outColor = inColor.rgb;

	shadow_coords = sun.projection * sun.view;
	castShadows = sun.castShadows;
}