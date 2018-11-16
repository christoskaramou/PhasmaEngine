#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoords;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;
layout(location = 5) in vec4 inColor;

layout( set = 0, binding = 0 ) uniform UniformBuffer {
	mat4 projection;
	mat4 lightView;
	mat4 model;
	mat4 dummy;
}ubo;

void main() {
	gl_Position = ubo.projection * ubo.lightView * ubo.model * vec4(inPosition, 1.0);
}
