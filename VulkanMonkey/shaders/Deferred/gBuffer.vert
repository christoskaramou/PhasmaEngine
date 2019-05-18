#version 450

const int MAX_NUM_JOINTS = 128;

layout(set = 0, binding = 0) uniform UniformBufferObject {
	mat4 matrix;
	mat4 previousMatrix;
	mat4 jointMatrix[MAX_NUM_JOINTS];
	float jointCount;
	float dummy[3];
} uboMesh;

layout(set = 1, binding = 5) uniform UniformBufferObject2 {
	vec4 baseColorFactor;
	vec4 emissiveFactor;
	float metallicFactor;
	float roughnessFactor;
	float alphaCutoff;
	float occlusionlMetalRoughness;
	float hasBones;
	float dummy[3];
} uboPrimitive;

layout(set = 2, binding = 0) uniform UniformBufferObject3 {
	mat4 matrix;
	mat4 view;
	mat4 projection;
	mat4 previousMatrix;
	mat4 previousView;
} uboModel;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoords;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inColor;
layout(location = 4) in ivec4 inJoint;
layout(location = 5) in vec4 inWeights;

layout (location = 0) out vec2 outUV;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec3 outColor;
layout (location = 3) out vec4 baseColorFactor;
layout (location = 4) out vec3 emissiveFactor;
layout (location = 5) out vec4 metRoughAlphacutOcl;
layout (location = 6) out vec4 posProj;
layout (location = 7) out vec4 posLastProj;
layout (location = 8) out vec4 outWorldPos;

void main() 
{
	mat4 boneTransform = mat4(1.0);
	if (uboMesh.jointCount > 0.0){
		boneTransform  = 
		inWeights[0] * uboMesh.jointMatrix[inJoint[0]] + 
		inWeights[1] * uboMesh.jointMatrix[inJoint[1]] + 
		inWeights[2] * uboMesh.jointMatrix[inJoint[2]] + 
		inWeights[3] * uboMesh.jointMatrix[inJoint[3]]; 
	}
	
	vec4 inPos = vec4(inPosition, 1.0f);
	
	mat3 mNormal = transpose(inverse(mat3(uboModel.matrix * uboMesh.matrix * boneTransform)));
	
	// UV
	outUV = inTexCoords;

	// Normal in world space
	outNormal = normalize(mNormal * inNormal);

	// Color
	outColor = inColor.rgb;

	// Factors
	baseColorFactor = uboPrimitive.baseColorFactor;
	emissiveFactor = uboPrimitive.emissiveFactor.xyz;
	metRoughAlphacutOcl = vec4(uboPrimitive.metallicFactor, uboPrimitive.roughnessFactor, uboPrimitive.alphaCutoff, uboPrimitive.occlusionlMetalRoughness);

	// Velocity
	mat4 projectionNoJitter = uboModel.projection;
	projectionNoJitter[2][0] = 0.0;
	projectionNoJitter[2][1] = 0.0;
	posProj = projectionNoJitter * uboModel.view * uboModel.matrix * uboMesh.matrix * inPos; // clip space
	posLastProj = projectionNoJitter * uboModel.previousView * uboModel.previousMatrix * uboMesh.previousMatrix * inPos; // clip space

	// WorldPos
	outWorldPos = uboModel.matrix * uboMesh.matrix * boneTransform * inPos;

	gl_Position = uboModel.projection * uboModel.view * outWorldPos;
}
