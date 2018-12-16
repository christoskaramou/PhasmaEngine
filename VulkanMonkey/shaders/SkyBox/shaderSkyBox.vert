#version 450

layout(location = 0) in vec4 skybox_position;

layout(set = 0, binding = 0) uniform UniformBuffer {
	mat4 projection;
	mat4 view;
};

layout(location = 0) out vec3 tex_coords;

void main() {
	gl_Position = (projection * view * skybox_position).xyzz; // keep skybox at the far plane of the projection
	tex_coords = skybox_position.xyz;
}