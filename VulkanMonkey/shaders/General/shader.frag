#version 450
#extension GL_ARB_separate_shader_objects : enable

const int numOfLights = 5;

struct Light {
	vec4 color;
	vec4 position;
	vec4 attenuation;
	vec4 dummy;
};

layout(set = 1, binding = 0) uniform sampler2D tSampler;
layout(set = 1, binding = 1) uniform sampler2D normSampler;
layout(set = 1, binding = 2) uniform sampler2D specSampler;
layout(set = 1, binding = 3) uniform sampler2D alphaSampler;
layout(set = 2, binding = 1) uniform sampler2DShadow shadowMapSampler1;
layout(set = 3, binding = 0) uniform Lights {
	Light light[numOfLights];
} lights;


layout(location = 0) in vec2 v_TexCoords;
layout(location = 1) in vec4 vertexCameraSpaceModel; // transformed to view * model * position
layout(location = 2) in mat4 view; // camera view matrix
layout(location = 6) in mat3 TBN; // to tangent space matrix
layout(location = 9) in vec4 shadow_coords1;
layout(location = 10) in float castShadows;

layout(location = 0) out vec4 o_Color;

//layout(push_constant) uniform Lights {
//	Light light[numOfLights];
//}lights;

const int specDamper = 40;
const float reflectivity = 0.588;

vec2 poissonDisk[16] = vec2[](
	vec2(0.493393f, 0.394269f),
	vec2(0.798547f, 0.885922f),
	vec2(0.247322f, 0.92645f),
	vec2(0.0514542f, 0.140782f),
	vec2(0.831843f, 0.00955229f),
	vec2(0.428632f, 0.0171514f),
	vec2(0.015656f, 0.749779f),
	vec2(0.758385f, 0.49617f),
	vec2(0.223487f, 0.562151f),
	vec2(0.0116276f, 0.406995f),
	vec2(0.241462f, 0.304636f),
	vec2(0.430311f, 0.727226f),
	vec2(0.981811f, 0.278359f),
	vec2(0.407056f, 0.500534f),
	vec2(0.123478f, 0.463546f),
	vec2(0.809534f, 0.682272f)
);
float D = 0.493393;
vec2 shadowTest[9] = vec2[](
	vec2(-D, -D),
	vec2(0.0f, -D),
	vec2(D, -D),
	vec2(-D, 0.0f),
	vec2(0.0, 0.0),
	vec2(D, 0.0),
	vec2(-D, D),
	vec2(0.0f, D),
	vec2(D, D)
);
float shadowTestWeights[9] = float[](
	 0.5, 1.0,  0.5,
	1.0,  4.0, 1.0,
	 0.5, 1.0,  0.5
);
void main() {
	vec4 s_coords1 = shadow_coords1 / shadow_coords1.w;

	float toCameraDistance = distance((view * vec4(0.0, 0.0, 0.0 ,1.0)).xyz, vertexCameraSpaceModel.xyz);

	float shadow = 1.0;
	if (castShadows > 0.5){
		for (int i=0;i<8;i++)
		{
			shadow -= 0.125 * ( 1.0 - texture( shadowMapSampler1, vec3( s_coords1.xy + poissonDisk[i]*0.0015, s_coords1.z-0.0001 )));
		}
	}
	//vec4 texel = vec4(0.0);
	//for (int i=0;i<9;i++)
	//{
	//	texel += 0.1111111 * shadowTestWeights[i] * texture(tSampler, v_TexCoords + shadowTest[i]*0.0015);
	//}
	vec4 texel = texture(tSampler, v_TexCoords);
	texel.a *= texture(alphaSampler, v_TexCoords).x;
	if (texel.a < 0.8){
		discard;
	}
	
	vec3 normalMap = normalize(texture(normSampler, v_TexCoords).rgb*2.0 - 1.0);

	vec3 totalDiffuse = vec3(0.0);
	vec3 totalSpecular = vec3(0.0);
	for(int i=0; i<numOfLights; i++){
		
		// to tangent space
		vec3 toLightVector = TBN * ((view * lights.light[i].position).xyz - vertexCameraSpaceModel.xyz);
		vec4 lightColor = lights.light[i].color;
		vec3 toCameraVector = TBN * (-vertexCameraSpaceModel.xyz);
		vec3 attenuation = lights.light[i].attenuation.xyz;

		float distance = length(toLightVector);
		float attFactor = attenuation.x * distance * distance + attenuation.y * distance + attenuation.z;

		vec3 nLight = normalize(toLightVector);

		// Diffuse
		float nDot = dot(nLight, normalMap);
		float brigtness = max(nDot, 0.0);
		float lightAlpha = 1.0;
		if (i > 0){
			lightAlpha = 1.0;
		}
		else{
			lightAlpha = lights.light[i].color.a;
		}
		totalDiffuse = totalDiffuse + (brigtness * lightColor.xyz) / attFactor * lightAlpha;
		if (i == 0){
			totalDiffuse *= shadow;
		}

		// Specular
		vec3 reflectedLightDirection = reflect(-nLight, normalMap);
		float specularFactor = dot(reflectedLightDirection, normalize(toCameraVector));
		specularFactor = max(specularFactor, 0.0);
		float dampedFactor = pow(specularFactor, specDamper);
		totalSpecular = totalSpecular + (dampedFactor * lightColor.xyz * reflectivity * texture(specSampler, v_TexCoords).xyz) / attFactor * lightAlpha;
	}
	totalDiffuse = max(totalDiffuse, 0.2);

    o_Color = vec4(totalDiffuse, 1.0) * texel + vec4(totalSpecular, 0.0) * shadow;
}
