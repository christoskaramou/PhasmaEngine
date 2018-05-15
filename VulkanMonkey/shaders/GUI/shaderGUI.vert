#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 i_Position;
layout(location = 1) in vec2 i_TexCoords;

layout(location = 0) out vec2 v_TexCoords;

void main() {
	
	gl_Position = vec4(i_Position, 1.0);
	
	v_TexCoords = i_TexCoords;
}