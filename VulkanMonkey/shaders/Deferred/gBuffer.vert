#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(set = 0, binding = 0) uniform UniformBufferObject {
	mat4 projection;
	mat4 view;
	mat4 model;
} ubo;

layout (location = 0) in vec3 i_Position;
layout (location = 1) in vec3 i_Normal;
layout (location = 2) in vec2 i_TexCoords;
layout (location = 3) in vec4 i_Tangent;
layout (location = 4) in vec4 i_Color;

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec3 outWorldPos;
layout (location = 3) out vec3 outTangent;
layout (location = 4) out vec2 outUV;
layout (location = 5) out float depth;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main() 
{
	vec4 inPos = vec4(i_Position, 1.0f);

	gl_Position = ubo.projection * ubo.view * ubo.model * inPos;
	depth = gl_Position.z;

	// Normal in world space
	outNormal = i_Normal;
	
	outColor = i_Color.rgb;

	// Vertex position in world space
	outWorldPos = (ubo.model * inPos).xyz;

	// Tangent in world space
	outTangent = i_Tangent.xyz;

	outUV = i_TexCoords;
}
