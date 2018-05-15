#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout(location = 0) in vec2 v_TexCoords;

layout(location = 0) out vec4 o_Color;

void main() {
	
	vec4 texel = texture(texSampler, v_TexCoords);
	if (texel.a < 0.2){
		discard;
	}
    o_Color = texel;
}
