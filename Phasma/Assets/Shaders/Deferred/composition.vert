#version 450

layout(set = 1, binding = 0) uniform shadow_buffer0 {
	mat4 projection;
	mat4 view;
	float cast_shadows;
	float max_cascade_dist0;
	float max_cascade_dist1;
	float max_cascade_dist2;
}sun0;

layout(set = 2, binding = 0) uniform shadow_buffer1 {
	mat4 projection;
	mat4 view;
	vec4 dummy;
}sun1;

layout(set = 3, binding = 0) uniform shadow_buffer2 {
	mat4 projection;
	mat4 view;
	vec4 dummy;
}sun2;

layout (location = 0) out vec2 out_UV;
layout (location = 1) out float cast_shadows;
layout (location = 2) out float max_cascade_dist0;
layout (location = 3) out float max_cascade_dist1;
layout (location = 4) out float max_cascade_dist2;
layout (location = 5) out mat4 shadow_coords0; // small area
layout (location = 9) out mat4 shadow_coords1; // medium area
layout (location = 13) out mat4 shadow_coords2; // large area

out gl_PerVertex
{
	vec4 gl_Position;
};

void main() 
{
	out_UV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(out_UV * 2.0f - 1.0f, 0.0f, 1.0f);

	shadow_coords0 = sun0.projection * sun0.view;
	shadow_coords1 = sun1.projection * sun1.view;
	shadow_coords2 = sun2.projection * sun2.view;
	cast_shadows = sun0.cast_shadows;
	max_cascade_dist0 = sun0.max_cascade_dist0;
	max_cascade_dist1 = sun0.max_cascade_dist1;
	max_cascade_dist2 = sun0.max_cascade_dist2;
}