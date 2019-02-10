#version 450

layout (set = 1, binding = 0) uniform sampler2D tSampler; // diffuse
layout (set = 1, binding = 1) uniform sampler2D normSampler; // normals
layout (set = 1, binding = 2) uniform sampler2D rm_Sampler; // occlusion-roughness-metallic
layout (set = 1, binding = 3) uniform sampler2D alphaSampler; // alpha
layout (set = 1, binding = 4) uniform sampler2D e_sampler; // emissive
layout (set = 1, binding = 5) uniform sampler2D AO_Sampler; // ambient or AO

layout (location = 0) in vec2 inUV;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inTangent;
layout (location = 3) in vec3 inBitangent;
layout (location = 4) in vec3 inColor;
layout (location = 5) in vec4 baseColorFactor;
layout (location = 6) in vec3 emissiveFactor;
layout (location = 7) in vec4 metRoughAlphacut;
layout (location = 8) in vec4 velocity;

layout (location = 0) out float outDepth;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec4 outAlbedo;
layout (location = 3) out vec3 outRoughMet;
layout (location = 4) out vec4 outVelocity;

void main() {
	float alpha = metRoughAlphacut.a > 0.5 ? texture(alphaSampler, inUV).r : texture(tSampler, inUV).a;
	if(alpha < 0.9)
		discard;

	vec3 T = normalize(inTangent);
	vec3 B = normalize(inBitangent);
	vec3 N = normalize(inNormal);
	mat3 TBN = mat3(T, B, N);
	vec3 normSampler = normalize(texture(normSampler, inUV).rgb * 2.0 - 1.0);

	float ao = texture(AO_Sampler, inUV).r;

	outDepth = gl_FragCoord.z;
	outNormal = TBN * normSampler;
	outAlbedo = vec4(texture(tSampler, inUV).xyz * ao, alpha) * baseColorFactor
				+ vec4(texture(e_sampler, inUV).xyz * emissiveFactor, 0.0);
	outRoughMet = vec3(0.0, texture(rm_Sampler, inUV).z * metRoughAlphacut.y, texture(rm_Sampler, inUV).y * metRoughAlphacut.x);
	outVelocity = velocity;
}
