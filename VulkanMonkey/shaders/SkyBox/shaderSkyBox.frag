#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 1) uniform samplerCube cubemapSampler;

layout(location = 0) in vec3 tex_coords;

layout(location = 0) out vec4 color;

void main() {
	color = texture(cubemapSampler, tex_coords);
}
