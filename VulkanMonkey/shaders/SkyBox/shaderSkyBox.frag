#version 450

layout(set = 0, binding = 1) uniform samplerCube cubemapSampler;

layout(location = 0) in vec3 tex_coords;

layout(location = 0) out vec4 color;

void main() {
	color = texture(cubemapSampler, tex_coords);
}
