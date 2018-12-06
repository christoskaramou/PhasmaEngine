#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (set = 1, binding = 0) uniform sampler2D tSampler;
layout (set = 1, binding = 1) uniform sampler2D normSampler;
layout (set = 1, binding = 2) uniform sampler2D specSampler;
layout (set = 1, binding = 3) uniform sampler2D alphaSampler;
layout (set = 1, binding = 4) uniform sampler2D roughSampler;
layout (set = 1, binding = 5) uniform sampler2D metalSampler;


layout (location = 0) in vec3 inWorldPos;
layout (location = 1) in vec2 inUV;
layout (location = 2) in vec3 inNormal;
layout (location = 3) in vec3 inTangent;
layout (location = 4) in vec3 inBitangent;
layout (location = 5) in vec3 inColor;

layout (location = 0) out vec4 outPosition;
layout (location = 1) out vec4 outNormal;
layout (location = 2) out vec4 outAlbedo;
layout (location = 3) out vec4 outSpecRoughMet;

void main() {
	float alpha = texture(alphaSampler, inUV).r;
	if(alpha < 0.8)
		discard;

	vec3 T = normalize(inTangent);
	vec3 B = normalize(inBitangent);
	vec3 N = normalize(inNormal);
	mat3 TBN = mat3(T, B, N);
	vec3 normSampler = normalize(texture(normSampler, inUV).rgb * 2.0 - 1.0);

	outPosition = vec4(inWorldPos, 1.0 / gl_FragCoord.w);
	outNormal = vec4(TBN * normSampler , 0.0f);
	outAlbedo = vec4(texture(tSampler, inUV).xyz, alpha);
	outSpecRoughMet = vec4(texture(specSampler, inUV).r, texture(roughSampler, inUV).r, texture(metalSampler, inUV).r, 1.0);
}
