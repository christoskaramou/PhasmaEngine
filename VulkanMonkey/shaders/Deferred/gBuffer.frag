#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (set = 1, binding = 0) uniform sampler2D tSampler;
layout (set = 1, binding = 1) uniform sampler2D normSampler;
layout (set = 1, binding = 2) uniform sampler2D specSampler;
layout (set = 1, binding = 3) uniform sampler2D alphaSampler;


layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec3 inWorldPos;
layout (location = 3) in vec3 inTangent;
layout (location = 4) in vec2 inUV;
layout (location = 5) in float depth;

layout (location = 0) out vec4 outPosition;
layout (location = 1) out vec4 outNormal;
layout (location = 2) out vec4 outAlbedo;
layout (location = 3) out vec4 outSpecular;
layout (location = 4) out float finalDepth;

void main() {
	float alpha = texture(alphaSampler, inUV).r;
	if(alpha < 0.8)
		discard;

	outPosition = vec4(inWorldPos, depth);

	vec3 N = normalize(inNormal);
	vec3 T = normalize(inTangent);
	vec3 B = normalize(cross(T, N));
	mat3 TBN = mat3(T, B, N);

	outNormal = vec4(normalize(TBN * (texture(normSampler, inUV).rgb * 2.0 - 1.0)), 1.0f);

	outAlbedo = texture(tSampler, inUV);
	outAlbedo.a = alpha;

	outSpecular = texture(specSampler, inUV);

	finalDepth = gl_FragCoord.z;
}
