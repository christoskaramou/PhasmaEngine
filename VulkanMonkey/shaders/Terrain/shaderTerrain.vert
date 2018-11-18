#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform UniformBufferObject {
	mat4 projection;
	mat4 view;
} ubo;

layout(location = 0) in vec3 i_Position;
layout(location = 1) in vec2 i_TexCoords;
layout(location = 2) in vec3 i_Normal;
layout(location = 3) in vec3 i_Tangent;
layout(location = 5) in vec3 i_Bitangent;
layout(location = 5) in vec4 i_Color;

layout(location = 0) out vec2 v_TexCoords;

void main() {	
	gl_Position = ubo.projection * ubo.view * vec4(i_Position, 1.0);
	v_TexCoords = i_TexCoords;
}