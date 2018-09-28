#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 2, binding = 0) uniform UniformBufferObject {
	mat4 projection;
	mat4 view;
	mat4 model;
} ubo;

layout(set = 0, binding = 0) uniform shadowBufferObject1 {
	mat4 projection;
	mat4 view;
	mat4 model;
	float castShadows;
}light1;

layout(location = 0) in vec3 i_Position;
layout(location = 1) in vec3 i_Normal;
layout(location = 2) in vec2 i_TexCoords;
layout(location = 3) in vec4 i_Tangent;

layout(location = 0) out vec2 v_TexCoords;
layout(location = 1) out vec4 vertexCameraSpaceModel;
layout(location = 2) out mat4 view;
layout(location = 6) out mat3 TBN;
layout(location = 9) out vec4 shadow_coords1;
layout(location = 10) out float castShadows;


const mat4 bias = mat4( 
  0.5, 0.0, 0.0, 0.0,
  0.0, 0.5, 0.0, 0.0,
  0.0, 0.0, 1.0, 0.0,
  0.5, 0.5, 0.0, 1.0 );

void main() {

	view = ubo.view;
	mat4 cameraSpaceModel = view * ubo.model;
	vertexCameraSpaceModel =  cameraSpaceModel * vec4(i_Position, 1.0); // vertex world pos to model to camera position
	
	gl_Position = ubo.projection * vertexCameraSpaceModel; // perspective transformation

	// Tangent space --------------------
	vec3 surfNormal = (cameraSpaceModel * vec4(i_Normal, 0.0)).xyz;
	vec3 tangent = (cameraSpaceModel * vec4 (i_Tangent.xyz, 0.0)).xyz;

	vec3 norm = normalize(surfNormal);
	vec3 tang = normalize(tangent);
	vec3 bitang = normalize(cross(norm, tang));
	TBN = transpose(mat3(tang, bitang, norm));

	v_TexCoords = i_TexCoords;
	
	shadow_coords1 = bias * light1.projection * light1.view * light1.model * vec4(i_Position, 1.0);
	castShadows = light1.castShadows;
}