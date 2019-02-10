#version 450

layout(set = 1, binding = 0) uniform shadowBufferObject0 {
	mat4 projection;
	mat4 view;
	float castShadows;
	float dummy[15];
}sun0;

layout(set = 2, binding = 0) uniform shadowBufferObject1 {
	mat4 projection;
	mat4 view;
	float castShadows;
	float dummy[15];
}sun1;

layout(set = 3, binding = 0) uniform shadowBufferObject2 {
	mat4 projection;
	mat4 view;
	float castShadows;
	float dummy[15];
}sun2;

layout (location = 0) out vec2 outUV;
layout (location = 1) out float castShadows;
layout (location = 2) out mat4 shadow_coords0; // small area
layout (location = 6) out mat4 shadow_coords1; // medium area
layout (location = 10) out mat4 shadow_coords2; // large area

out gl_PerVertex
{
	vec4 gl_Position;
};

void main() 
{
	outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(outUV * 2.0f - 1.0f, 0.0f, 1.0f);

	shadow_coords0 = sun0.projection * sun0.view;
	shadow_coords1 = sun1.projection * sun1.view;
	shadow_coords2 = sun2.projection * sun2.view;
	castShadows = sun0.castShadows;
}