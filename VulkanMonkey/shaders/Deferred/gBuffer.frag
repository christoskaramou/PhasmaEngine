#version 450
#extension GL_GOOGLE_include_directive : require

#include "../Common/common.glsl"

layout (set = 1, binding = 0) uniform sampler2D bcSampler; // BaseColor
layout (set = 1, binding = 1) uniform sampler2D mrSampler; // MetallicRoughness
layout (set = 1, binding = 2) uniform sampler2D nSampler;  // Normal
layout (set = 1, binding = 3) uniform sampler2D oSampler;  // Occlusion
layout (set = 1, binding = 4) uniform sampler2D eSampler;  // Emissive

layout (location = 0) in vec2 inUV;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec4 inColor;
layout (location = 3) in vec4 baseColorFactor;
layout (location = 4) in vec3 emissiveFactor;
layout (location = 5) in vec4 metRoughAlphacutOcl;
layout (location = 6) in vec4 posProj;
layout (location = 7) in vec4 posLastProj;
layout (location = 8) in vec4 inWorldPos;

layout (location = 0) out float outDepth;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec4 outAlbedo;
layout (location = 3) out vec3 outMetRough;
layout (location = 4) out vec2 outVelocity;
layout (location = 5) out vec4 outEmissive;

void main() {
	vec4 basicColor = texture(bcSampler, inUV) + inColor; 
	if (basicColor.a < metRoughAlphacutOcl.z) discard; // needed because alpha blending is messed up when objects are not in order
	vec3 metRough = texture(mrSampler, inUV).xyz;
	vec3 emissive = texture(eSampler, inUV).xyz;
	float ao = texture(oSampler, inUV).r;

	outDepth = gl_FragCoord.z;
	outNormal = getNormal(inWorldPos.xyz, nSampler, inNormal, inUV);
	outAlbedo = vec4(basicColor.xyz * ao, basicColor.a) * baseColorFactor;
	outMetRough = vec3(0.0, metRough.y, metRough.z);
	outVelocity = (posProj.xy / posProj.w - posLastProj.xy / posLastProj.w) * vec2(0.5, 0.5); // ndc space
	outEmissive = vec4(emissive * emissiveFactor, 0.0);
}
