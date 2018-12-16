#version 450

layout(set = 0, binding = 1) uniform sampler2D tSampler;


layout(location = 0) in vec2 v_TexCoords;

layout(location = 0) out vec4 o_Color;

void main() {
	vec4 texel = texture(tSampler, v_TexCoords);
    o_Color = texel;
}
