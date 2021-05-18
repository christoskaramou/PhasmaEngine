#version 450

const int MAX_NUM_JOINTS = 128;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoords;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inColor;
layout(location = 4) in ivec4 inJoint;
layout(location = 5) in vec4 inWeights;

layout( set = 0, binding = 0 ) uniform UniformBuffer0 {
	mat4 projection;
	mat4 lightView;
	float dummy[16];
}ubo;

layout( set = 1, binding = 0 ) uniform UniformBuffer1 {	
	mat4 matrix;
	mat4 previousMatrix;
	mat4 jointMatrix[MAX_NUM_JOINTS];
	float jointCount;
	float dummy[3];
}mesh;

layout( set = 2, binding = 0 ) uniform UniformBuffer2 {	
	mat4 matrix;
	mat4 dummy[3];
}model;

void main() {
	mat4 boneTransform = mat4(1.0);
	if (mesh.jointCount > 0.0){
		boneTransform  = 
		inWeights[0] * mesh.jointMatrix[inJoint[0]] + 
		inWeights[1] * mesh.jointMatrix[inJoint[1]] + 
		inWeights[2] * mesh.jointMatrix[inJoint[2]] + 
		inWeights[3] * mesh.jointMatrix[inJoint[3]]; 
	}

	gl_Position = ubo.projection * ubo.lightView * model.matrix * mesh.matrix * boneTransform * vec4(inPosition, 1.0);
}
