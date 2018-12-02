//
//
// PBR shader reference from Panos Karabelas (Directus3D)
// https://github.com/PanosK92/Directus3D/blob/90cf8de87f2cff29f1e9885c67c4b73efff457a2/Assets/Standard%20Assets/Shaders/BRDF.hlsl
//
//

#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

const float PI = 3.141592653589793f;

struct Material{
	vec3 color_diffuse;
	float roughness;
	vec3 color_specular;
	float alpha;
};

struct Light {
	vec4 color;
	vec4 position;
	vec4 attenuation; };

vec2 poissonDisk[8] = vec2[](
	vec2(0.493393f, 0.394269f),
	vec2(0.798547f, 0.885922f),
	vec2(0.247322f, 0.92645f),
	vec2(0.0514542f, 0.140782f),
	vec2(0.831843f, 0.00955229f),
	vec2(0.428632f, 0.0171514f),
	vec2(0.015656f, 0.749779f),
	vec2(0.758385f, 0.49617f));

layout(push_constant) uniform SS { vec4 effect; } screenSpace;
layout (constant_id = 0) const int NUM_LIGHTS = 1;
layout (set = 0, binding = 0) uniform sampler2D samplerPosition;
layout (set = 0, binding = 1) uniform sampler2D samplerNormal;
layout (set = 0, binding = 2) uniform sampler2D samplerAlbedo;
layout (set = 0, binding = 3) uniform sampler2D samplerSpecRoughMet;
layout (set = 0, binding = 4) uniform UBO { vec4 camPos; Light lights[NUM_LIGHTS+1]; } ubo;
layout (set = 0, binding = 5) uniform sampler2D ssaoBlurSampler;
layout (set = 0, binding = 6) uniform sampler2D ssrSampler;
layout (set = 1, binding = 1) uniform sampler2DShadow shadowMapSampler;


layout (location = 0) in vec2 inUV;
layout (location = 1) in float castShadows;
layout (location = 2) in mat4 shadow_coords;

layout (location = 0) out vec4 outColor;
layout (location = 1) out vec4 outComposition;

vec3 F_FresnelSchlick(float HdV, vec3 F0);
float G_SmithSchlickGGX(float NdotV, float NdotL, float a);
float D_GGX(float a, float NdotH);
vec3 Diffuse_OrenNayar(vec3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH);
vec3 BRDF(Material material, int i, vec3 normal, vec3 camera_to_pixel);
vec3 calculateShadow(int mainLight, vec3 fragPos, vec3 normal, vec3 albedo, float specular);
vec3 calculateColor(int light, vec3 fragPos, vec3 normal, vec3 albedo, float specular);

void main() 
{
	vec4 fragPos = texture(samplerPosition, inUV);
	vec3 normal = texture(samplerNormal, inUV).xyz;
	vec4 albedo = texture(samplerAlbedo, inUV);
	float oclusion = texture(ssaoBlurSampler, inUV).x;
	vec3 specRoughMet = texture(samplerSpecRoughMet, inUV).xyz;

	Material material;
	material.color_diffuse = (1.0f - specRoughMet.z) * albedo.xyz;
	material.roughness = specRoughMet.y;
	material.color_specular = mix(vec3(0.03f), albedo.xyz, specRoughMet.z);
	material.alpha = max(0.001f, material.roughness * material.roughness);

	// Ambient
	vec3 fragColor = 0.3 * albedo.xyz;

	// SSAO
	if (screenSpace.effect.x > 0.5f)
		fragColor *= oclusion;

	fragColor += calculateShadow(0, fragPos.xyz, normal, albedo.xyz, material.roughness);

	for(int i = 1; i < NUM_LIGHTS+1; ++i){
		vec3 L =  ubo.lights[i].position.xyz - fragPos.xyz;
		float atten = 1.0 / (1.0 + ubo.lights[i].attenuation.x * pow(length(L), 2));
		fragColor += BRDF(material, i, normal, fragPos.xyz - ubo.camPos.xyz) * atten;
	}

	outColor = vec4(fragColor, albedo.a);

	// SSR
	if (screenSpace.effect.y > 0.5)
		outColor += vec4(texture(ssrSampler, inUV).xyz, 0.0);

	outComposition = outColor;
}

vec3 calculateShadow(int mainLight, vec3 fragPos, vec3 normal, vec3 albedo, float specular)
{

	vec4 s_coords =  shadow_coords * vec4(fragPos.xyz, 1.0);
	s_coords = s_coords / s_coords.w;
	float shadow = 0.0;
	for (int i = 0; i < 8 * castShadows; i++)
		shadow += 0.125 * texture( shadowMapSampler, vec3( s_coords.xy + poissonDisk[i]*0.0008, s_coords.z-0.0001 ));

	// Light to fragment
	vec3 L = fragPos - ubo.lights[mainLight].position.xyz;

	// Viewer to fragment
	vec3 V = fragPos - ubo.camPos.xyz;

	// Diffuse part
	vec3 diff = ubo.lights[mainLight].color.xyz * albedo.xyz * ubo.lights[mainLight].color.a;

	// Specular part
	L = normalize(L);
	vec3 N = normalize(normal);
	vec3 R = reflect(-L, N);
	V = normalize(V);
	float RdotV = max(0.0, dot(R, V));
	vec3 spec = ubo.lights[mainLight].color.xyz * specular * pow(RdotV, 32.0);

	return shadow * (diff + spec);
}

vec3 calculateColor(int light, vec3 fragPos, vec3 normal, vec3 albedo, float specular)
{
	// Light to fragment
	vec3 L =  fragPos - ubo.lights[light].position.xyz;

	// Distance from light to fragment
	float dist = length(L);
	if (dist > ubo.lights[light].attenuation.x*5.0)
		return vec3(0.0);

	// Viewer to fragment
	vec3 V = fragPos - ubo.camPos.xyz;

	// Diffuse part
	L = normalize(L);
	vec3 N = normalize(normal);
	float LdotN = max(0.0, dot(L, N));
	vec3 diff = ubo.lights[light].color.xyz * albedo.xyz * LdotN;

	// Specular part
	vec3 R = reflect(-L, N);
	V = normalize(V);
	float RdotV = max(0.0, dot(R, V));
	vec3 spec = ubo.lights[light].color.xyz * specular * pow(RdotV, 32.0);
	
	// Attenuation
	float atten = 1.0 / (1.0 + ubo.lights[light].attenuation.x * pow(dist, 2));

	return atten * (diff + spec);
}

vec3 F_FresnelSchlick(float HdV, vec3 F0)
{
	return F0 + (1.0f - F0) * pow(1.0f - HdV, 5.0f);
}

float G_SmithSchlickGGX(float NdotV, float NdotL, float a)
{
    float k = a * 0.5f;
    float GV = NdotV / (NdotV * (1.0f - k) + k);
    float GL = NdotL / (NdotL * (1.0f - k) + k);

    return GV * GL;
}

float D_GGX(float a, float NdotH)
{
    // Isotropic ggx.
    float a2 = a*a;
    float NdotH2 = NdotH * NdotH;

    float denominator = NdotH2 * (a2 - 1.0f) + 1.0f;
    denominator *= denominator;
    denominator *= PI;

    return a2 / denominator;
}

// [Gotanda 2012, "Beyond a Simple Physically Based Blinn-Phong Model in Real-Time"]
vec3 Diffuse_OrenNayar(vec3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH)
{
	float a = Roughness * Roughness;
	float s  = a;
	float s2 = s * s;
	float VoL = 2 * VoH * VoH - 1; 	// double angle identity
	float Cosri = VoL - NoV * NoL;
	float C1 = 1 - 0.5 * s2 / (s2 + 0.33);
	float C2 = 0.45 * s2 / (s2 + 0.09) * Cosri * ( Cosri >= 0 ? 1.0 / max( NoL, NoV + 0.0001f ) : 1 );
	return DiffuseColor / PI * ( C1 + C2 ) * ( 1 + Roughness * 0.5 );
}

vec3 BRDF(Material material,int i, vec3 normal, vec3 camera_to_pixel)
{
	// Compute some commmon vectors
	vec3 h 	= normalize(ubo.lights[i].position.xyz - camera_to_pixel);
	float NdotV = abs(dot(normal, -camera_to_pixel)) + 1e-5;
    float NdotL = clamp(dot(normal, ubo.lights[i].position.xyz), 0.0f, 1.0f);   
    float NdotH = clamp(dot(normal, h), 0.0f, 1.0f);
    float VdotH = clamp(dot(-camera_to_pixel, h), 0.0f, 1.0f);
	
	 // BRDF Diffuse
    vec3 cDiffuse 	= Diffuse_OrenNayar(material.color_diffuse, material.roughness, NdotV, NdotL, VdotH);
	
	// BRDF Specular	
	vec3 F = F_FresnelSchlick(VdotH, material.color_specular);
    float G = G_SmithSchlickGGX(NdotV, NdotL, material.alpha);
    float D = D_GGX(material.alpha, NdotH);
	vec3 nominator = F * G * D;
	float denominator = 4.0f * NdotL * NdotV;
	vec3 cSpecular = nominator / max(0.001f, denominator);

	return ubo.lights[i].color.xyz * NdotL * (cDiffuse * (1.0f - cSpecular) + cSpecular);
}