#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoords;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inColor;
layout(location = 4) in ivec4 inBoneIDs;
layout(location = 5) in vec4 inWeights;

layout( set = 0, binding = 0 ) uniform UniformBuffer {
	mat4 projection;
	mat4 lightView;
	float dummy[16];
}ubo;

layout( set = 1, binding = 0 ) uniform UniformBuffer2 {	
	mat4 model;
	mat4 dummy1;
	mat4 dummy2;
	mat4 dummy3;
}ubo2;

void main() {
	gl_Position = ubo.projection * ubo.lightView * ubo2.model * vec4(inPosition, 1.0);
}
