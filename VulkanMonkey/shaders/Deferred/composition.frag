#version 450
#extension GL_GOOGLE_include_directive : require

#include "../Common/common.h"
#include "../Common/tonemapping.h"
#include "Light.h"

layout (location = 0) in vec2 inUV;
layout (location = 1) in float castShadows;
layout (location = 2) in float maxCascadeDist0;
layout (location = 3) in float maxCascadeDist1;
layout (location = 4) in float maxCascadeDist2;
layout (location = 5) in mat4 shadow_coords0; // small area
layout (location = 9) in mat4 shadow_coords1; // medium area
layout (location = 13) in mat4 shadow_coords2; // large area

layout (location = 0) out vec4 outColor;
layout (location = 1) out vec4 outComposition;

vec3 calculateShadowAndDirectLight(Material material, vec3 world_pos, vec3 camera_pos, vec3 material_normal);

void main() 
{
	vec3 fragPos = getPosFromUV(inUV, texture(samplerDepth, inUV).x, screenSpace.invViewProj, screenSpace.size);
	vec3 normal = texture(samplerNormal, inUV).xyz;
	vec4 albedo = texture(samplerAlbedo, inUV);
	float ssao = texture(ssaoBlurSampler, inUV).x;
	vec3 ssdo = texture(ssdoBlurSampler, inUV).xyz;
	
	vec3 roughMet = texture(samplerRoughMet, inUV).xyz;

	Material material;
	material.albedo = albedo.xyz;
	material.roughness = roughMet.z;
	material.metallic = roughMet.y;
	material.F0 = mix(vec3(0.04f), material.albedo, material.metallic);

	// Ambient	
	float ratio = 1.00 / 1.52;
	vec3 I = normalize(fragPos - ubo.camPos.xyz);
    vec3 R = reflect(I, normalize(normal));
	vec3 envColor = (texture(cubemapSampler, R).xyz * (1.0 - material.roughness) * material.metallic); // very fake enviroment reflectance
	vec3 fragColor = 0.3 * albedo.xyz + 0.2 * envColor;

	// SSAO
	if (screenSpace.effect.x > 0.5f)
		fragColor *= ssao;

	// SSDO
	float light_occlusion = 1.0;
	//if (screenSpace.effect.z > 0.5){
	//	vec3 lightDir = normalize(-ubo.lights[0].position.xyz);
	//	light_occlusion = 1.0-clamp(dot(lightDir, ssdo), 0.0, 1.0);
	//}
	fragColor += calculateShadowAndDirectLight(material, fragPos, ubo.camPos.xyz, normal) * light_occlusion;

	for(int i = 1; i < NUM_LIGHTS+1; ++i){
		light_occlusion = 1.0;
		//if (screenSpace.effect.z > 0.5){
		//	vec3 lightDir = ubo.lights[i].position.xyz - fragPos;
		//	light_occlusion = 1.0-clamp(dot(lightDir, ssdo), 0.0, 1.0);
		//}
		fragColor += compute_point_light(i, material, fragPos, ubo.camPos.xyz, normal) * light_occlusion;
	}

	outColor = vec4(fragColor, albedo.a);

	// SSR
	if (screenSpace.effect.y > 0.5)
		outColor += vec4(texture(ssrSampler, inUV).xyz, 0.0) * (1.0 - material.roughness);
	
	// Tone Mapping
	if (screenSpace.effect.z > 0.5)
		outColor.xyz = ACESFitted(outColor.xyz);
		//outColor.xyz = ToneMapReinhard(outColor.xyz, screenSpace.effect.w); // ToneMapReinhard(color, exposure value)
		//outColor.xyz = Uncharted2(outColor.xyz);

	outComposition = outColor;
}

vec2 poissonDisk[8] = vec2[](
	vec2(0.493393f, 0.394269f),
	vec2(0.798547f, 0.885922f),
	vec2(0.247322f, 0.92645f),
	vec2(0.0514542f, 0.140782f),
	vec2(0.831843f, 0.00955229f),
	vec2(0.428632f, 0.0171514f),
	vec2(0.015656f, 0.749779f),
	vec2(0.758385f, 0.49617f));

vec3 calculateShadowAndDirectLight(Material material, vec3 world_pos, vec3 camera_pos, vec3 material_normal)
{
	vec4 s_coords0 = shadow_coords0 * vec4(world_pos, 1.0);
	s_coords0.xy = s_coords0.xy * 0.5 + 0.5;
	s_coords0 = s_coords0 / s_coords0.w;
	vec4 s_coords1 = shadow_coords1 * vec4(world_pos, 1.0);
	s_coords1.xy = s_coords1.xy * 0.5 + 0.5;
	s_coords1 = s_coords1 / s_coords1.w;
	vec4 s_coords2 = shadow_coords2 * vec4(world_pos, 1.0);
	s_coords2.xy = s_coords2.xy * 0.5 + 0.5;
	s_coords2 = s_coords2 / s_coords2.w;

	float lit = 0.0;
	float dist = distance(world_pos, camera_pos);
	if (dist < maxCascadeDist0) {
		for (int i = 0; i < 4 * castShadows; i++) {
			float value = mix(texture(shadowMapSampler0, vec3(s_coords0.xy + poissonDisk[i] * 0.0008, s_coords0.z + 0.0001)), texture(shadowMapSampler1, vec3(s_coords1.xy + poissonDisk[i] * 0.0008, s_coords1.z + 0.0001)), (dist*dist) / (maxCascadeDist0*maxCascadeDist0));
			lit += 0.25 * value;
		}
	}
	else if (dist < maxCascadeDist1) {
		for (int i = 0; i < 4 * castShadows; i++) {
			float value = mix(texture(shadowMapSampler1, vec3(s_coords1.xy + poissonDisk[i] * 0.0008, s_coords1.z + 0.0001)), texture(shadowMapSampler2, vec3(s_coords2.xy + poissonDisk[i] * 0.0008, s_coords2.z + 0.0001)), (dist*dist) / (maxCascadeDist1*maxCascadeDist1));
			lit += 0.25 * value;
		}
	}
	else {
		for (int i = 0; i < 4 * castShadows; i++)
			lit += 0.25 * (texture(shadowMapSampler2, vec3(s_coords2.xy + poissonDisk[i] * 0.0008, s_coords2.z + 0.0001)));
	}

	float roughness = material.roughness * 0.75 + 0.25;

	// Compute directional light.
	vec3 light_dir_full = ubo.lights[0].position.xyz;
	vec3 light_dir = normalize(light_dir_full);
	vec3 L = light_dir;
	vec3 V = normalize(camera_pos - world_pos);
	vec3 H = normalize(V + L);
	vec3 N = material_normal;

	float NoV = clamp(dot(N, V), 0.001, 1.0);
	float NoL = clamp(dot(N, L), 0.001, 1.0);
	float HoV = clamp(dot(H, V), 0.001, 1.0);
	float LoV = clamp(dot(L, V), 0.001, 1.0);

	vec3 F0 = compute_F0(material.albedo, material.metallic);
	vec3 specular_fresnel = fresnel(F0, HoV);
	vec3 specref = ubo.lights[0].color.xyz * NoL * lit * cook_torrance_specular(N, H, NoL, NoV, specular_fresnel, roughness);
	vec3 diffref = ubo.lights[0].color.xyz * NoL * lit * (1.0 - specular_fresnel) * (1.0 / PI);

	vec3 reflected_light = specref;
	vec3 diffuse_light = diffref * material.albedo * (1.0 - material.metallic);
	vec3 lighting = reflected_light + diffuse_light;

	return lighting;
}
