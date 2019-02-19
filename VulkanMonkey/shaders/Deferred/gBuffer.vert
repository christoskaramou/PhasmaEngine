#version 450

const int MAX_BONES = 110;

layout(set = 0, binding = 0) uniform UniformBufferObject {
	mat4 projection;
	mat4 view;
	mat4 model;
	mat4 previousModel;
	mat4 bones[MAX_BONES];
} ubo;

layout(set = 1, binding = 6) uniform UniformBufferObject2 {
	vec4 baseColorFactor;
	vec4 emissiveFactor;
	float metallicFactor;
	float roughnessFactor;
	float alphaCutoff;
	float hasAlphaMap;
	float hasBones;
	float dummy[3];
} uboMesh;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoords;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;
layout(location = 5) in vec4 inColor;
layout(location = 6) in ivec4 inBoneIDs;
layout(location = 7) in vec4 inWeights;

layout (location = 0) out vec2 outUV;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec3 outTangent;
layout (location = 3) out vec3 outBitangent;
layout (location = 4) out vec3 outColor;
layout (location = 5) out vec4 baseColorFactor;
layout (location = 6) out vec3 emissiveFactor;
layout (location = 7) out vec4 metRoughAlphacut;
layout (location = 8) out vec4 velocity;

void main() 
{
	mat4 boneTransform = mat4(1.0);
	if (uboMesh.hasBones > 0.5){
		boneTransform  = ubo.bones[inBoneIDs[0]] * inWeights[0];
		boneTransform += ubo.bones[inBoneIDs[1]] * inWeights[1];
		boneTransform += ubo.bones[inBoneIDs[2]] * inWeights[2];
		boneTransform += ubo.bones[inBoneIDs[3]] * inWeights[3];
	}

	vec4 inPos = vec4(inPosition, 1.0f);
	gl_Position = ubo.projection * ubo.view * ubo.model * boneTransform * inPos;
	
	mat3 mNormal = transpose(inverse(mat3(ubo.model * boneTransform)));
	
	// UV
	outUV = inTexCoords;

	// Normal in world space
	outNormal = mNormal * inNormal;

	// Tangent in world space
	outTangent = mNormal * inTangent;

	// Bitangent in world space
	outBitangent = mNormal * inBitangent;

	// Color (not in use)	
	outColor = inColor.rgb;

	// Factors
	baseColorFactor = uboMesh.baseColorFactor;
	emissiveFactor = uboMesh.emissiveFactor.xyz;
	metRoughAlphacut = vec4(uboMesh.metallicFactor, uboMesh.roughnessFactor, uboMesh.alphaCutoff, uboMesh.hasAlphaMap);

	// Velocity
	velocity = (ubo.model * inPos) - (ubo.previousModel * inPos);
	velocity.w = 1.0;
}
