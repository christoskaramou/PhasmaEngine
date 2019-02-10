#version 450

layout(set = 1, binding = 0) uniform UniformBufferObject {
	mat4 projection;
	mat4 view;
	mat4 model;
} ubo;

layout(set = 3, binding = 0) uniform shadowBufferObject0 {
	mat4 projection;
	mat4 view;
	float castShadows;
	float dummy[15];
}sun0;

layout(set = 4, binding = 0) uniform shadowBufferObject1 {
	mat4 projection;
	mat4 view;
	float castShadows;
	float dummy[15];
}sun1;

layout(set = 5, binding = 0) uniform shadowBufferObject2 {
	mat4 projection;
	mat4 view;
	float castShadows;
	float dummy[15];
}sun2;

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
layout (location = 7) out mat4 shadow_coords0; // small area
layout (location = 11) out mat4 shadow_coords1; // medium area
layout (location = 15) out mat4 shadow_coords2; // large area

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

	shadow_coords0 = sun0.projection * sun0.view;
	shadow_coords1 = sun1.projection * sun1.view;
	shadow_coords2 = sun2.projection * sun2.view;
	castShadows = sun0.castShadows;
}