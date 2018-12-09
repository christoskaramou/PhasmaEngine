#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(set = 0, binding = 0) uniform UniformBufferObject {
	mat4 projection;
	mat4 view;
	mat4 model;
} ubo;

layout(location = 0) in highp vec3 inPosition;
layout(location = 1) in vec2 inTexCoords;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;
layout(location = 5) in vec4 inColor;

layout (location = 0) out highp vec3 outWorldPos;
layout (location = 1) out vec2 outUV;
layout (location = 2) out vec3 outNormal;
layout (location = 3) out vec3 outTangent;
layout (location = 4) out vec3 outBitangent;
layout (location = 5) out vec3 outColor;

void main() 
{
	vec4 inPos = vec4(inPosition, 1.0f);
	vec4 viewPos = ubo.view * ubo.model * inPos;
	gl_Position = ubo.projection * viewPos;
	
	// Vertex position in world space
	outWorldPos = (ubo.model * inPos).xyz;
	// UV
	outUV = inTexCoords;
	// Normal in world space

	mat3 mNormal = transpose(inverse(mat3(ubo.model)));

	outNormal = mNormal * inNormal;
	// Tangent in world space
	outTangent = mNormal * inTangent;
	// Bitangent in world space
	outBitangent = mNormal * inBitangent;
	// Color (not in use)	
	outColor = inColor.rgb;
}
