#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(set = 1, binding = 0) uniform shadowBufferObject {
	mat4 projection;
	mat4 view;
	mat4 model;
	float castShadows;
}sun;

const mat4 bias = mat4( 
  0.5, 0.0, 0.0, 0.0,
  0.0, 0.5, 0.0, 0.0,
  0.0, 0.0, 1.0, 0.0,
  0.5, 0.5, 0.0, 1.0 );

layout (location = 0) out vec2 outUV;
layout (location = 1) out float castShadows;
layout (location = 2) out mat4 shadow_coords;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main() 
{
	outUV = vec2(gl_VertexIndex & 2, (gl_VertexIndex << 1) & 2);
	gl_Position = vec4(outUV * 2.0f - 1.0f, 0.0f, 1.0f);

	shadow_coords = bias * sun.projection * sun.view;
	castShadows = sun.castShadows;
}