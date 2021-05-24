/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

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