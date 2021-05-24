/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#version 450
#extension GL_GOOGLE_include_directive : require

#include "Light.glsl"

layout (location = 0) in vec2 in_UV;
layout (location = 1) in float cast_shadows;
layout (location = 2) in float max_cascade_dist0;
layout (location = 3) in float max_cascade_dist1;
layout (location = 4) in float max_cascade_dist2;
layout (location = 5) in mat4 shadow_coords0; // small area
layout (location = 9) in mat4 shadow_coords1; // medium area
layout (location = 13) in mat4 shadow_coords2; // large area

layout (location = 0) out vec4 outColor;

vec3 directLight(Material material, vec3 world_pos, vec3 camera_pos, vec3 material_normal, float ssao);

void main() 
{
	float depth = texture(sampler_depth, in_UV).x;

	// if the depth is maximum it hits the skybox
	if (depth == 0.0) {

		// Skybox
		vec3 frag_pos = getPosFromUV(in_UV, depth, screenSpace.invViewProj);
		outColor = vec4(texture(sampler_cube_map, normalize(frag_pos - ubo.cam_pos.xyz)).xyz, 1.0);
		
		// Fog
		if (screenSpace.effects3.y > 0.5) {
			// screenSpace.effects3.y -> fog on/off
			// screenSpace.effects2.w -> fog max height
			// screenSpace.effects2.x -> fog global thickness
			// screenSpace.effects3.x -> fog ground thickness

			float fogNear = 0.5;
			float fogFar = 1000.0;
			float depth = length(frag_pos - ubo.cam_pos.xyz);
			float groundHeightMin = -100.0;
			float groundHeightMax = screenSpace.effects2.w * 20.0;
			float globalThickness = 1.0;
			float groundThickness = 2.0;

			float depthFactor = clamp(1.0 - (fogFar - depth) / (fogFar - fogNear), 0.0, 1.0);
			float heightFactor = clamp(1.0 - (frag_pos.y - groundHeightMin) / (groundHeightMax - groundHeightMin), 0.0, 1.0);
			float globalFactor = depthFactor * globalThickness;
			float noiseFactor = 1.0; // texture(NoiseMap, position.xz).x;
			float groundFactor = heightFactor * depthFactor * noiseFactor * groundThickness;
			float fogFactor = clamp(groundFactor + globalFactor, 0.0, 1.0);
			
			outColor.xyz = mix(outColor.xyz, vec3(1.0), fogFactor);

			// Volumetric light
			if (screenSpace.effects1.y > 0.5)
				outColor.xyz += VolumetricLighting(ubo.lights[0], frag_pos, in_UV, shadow_coords1, fogFactor);
		}

		return;
	}
	vec3 frag_pos = getPosFromUV(in_UV, depth, screenSpace.invViewProj);
	vec3 normal = texture(sampler_normal, in_UV).xyz;
	vec3 metRough = texture(sampler_met_rough, in_UV).xyz;
	vec4 albedo = texture(sampler_albedo, in_UV);
	
	Material material;
	material.albedo = albedo.xyz;
	material.roughness = metRough.y;
	material.metallic = metRough.z;
	material.F0 = mix(vec3(0.04f), material.albedo, material.metallic);

	// Ambient
	float factor_occlusion = screenSpace.effects0.x > 0.5 ? texture(sampler_ssao_blur, in_UV).x : 1.0;
	float factor_sky_light = clamp(ubo.lights[0].color.a, 0.025f, 1.0f);
	factor_sky_light *= screenSpace.effects3.z > 0.5 ? 0.25 : 0.15;
	float ambient_light = factor_sky_light * factor_occlusion;
	vec3 fragColor = vec3(0.0);// 0.1 * material.albedo.xyz;

	// IBL
	IBL ibl;
	ibl.reflectivity = vec3(0.5);
	if (screenSpace.effects1.x > 0.5) {
		ibl = ImageBasedLighting(material, normal, normalize(frag_pos - ubo.cam_pos.xyz), sampler_cube_map, sampler_lut_IBL);
		fragColor += ibl.final_color * ambient_light;
	}

	// screenSpace.effects3.z -> shadow cast
	if (screenSpace.effects3.z > 0.5)
		fragColor += directLight(material, frag_pos, ubo.cam_pos.xyz, normal, factor_occlusion);

	for(int i = 1; i < NUM_LIGHTS; ++i)
		fragColor += compute_point_light(i, material, frag_pos, ubo.cam_pos.xyz, normal, factor_occlusion);

	outColor = vec4(fragColor, albedo.a) + texture(sampler_emission, in_UV);

	// SSR
	if (screenSpace.effects0.y > 0.5) {
		vec3 ssr = texture(sampler_ssr, in_UV).xyz * ibl.reflectivity;
		outColor += vec4(ssr, 0.0);// * (1.0 - material.roughness);
	}
	
	// Tone Mapping
	if (screenSpace.effects0.z > 0.5)
		outColor.xyz = Reinhard(outColor.xyz);

	// Fog
	if (screenSpace.effects3.y > 0.5) {
		// screenSpace.effects3.y -> fog on/off
		// screenSpace.effects2.w -> fog max height
		// screenSpace.effects2.x -> fog global thickness
		// screenSpace.effects3.x -> fog ground thickness
		
		float fogNear = 0.5;
		float fogFar = 1000.0;
		float depth = length(frag_pos - ubo.cam_pos.xyz);
		float groundHeightMin = -2.0;
		float groundHeightMax = screenSpace.effects2.w;
		float globalThickness = screenSpace.effects2.x;
		float groundThickness = screenSpace.effects3.x;

		float depthFactor = clamp(1.0 - (fogFar - depth) / (fogFar - fogNear), 0.0, 1.0);
		float heightFactor = clamp(1.0 - (frag_pos.y - groundHeightMin) / (groundHeightMax - groundHeightMin), 0.0, 1.0);
		float globalFactor = depthFactor * globalThickness;
		float noiseFactor = 1.0; // texture(NoiseMap, position.xz).x;
		float groundFactor = heightFactor * depthFactor * noiseFactor * groundThickness;
		float fogFactor = clamp(groundFactor + globalFactor, 0.0, 1.0);
		
		outColor.xyz = mix(outColor.xyz, vec3(1.0), fogFactor);

		// Volumetric light
		if (screenSpace.effects1.y > 0.5)
			outColor.xyz += VolumetricLighting(ubo.lights[0], frag_pos, in_UV, shadow_coords1, fogFactor);
	}
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

vec3 directLight(Material material, vec3 world_pos, vec3 camera_pos, vec3 material_normal, float ssao)
{
	// calculate shadow texture coords of each cascade
	vec4 s_coords0 = shadow_coords0 * vec4(world_pos, 1.0);
	s_coords0.xy = s_coords0.xy * 0.5 + 0.5;
	s_coords0 = s_coords0 / s_coords0.w;

	vec4 s_coords1 = shadow_coords1 * vec4(world_pos, 1.0);
	s_coords1.xy = s_coords1.xy * 0.5 + 0.5;
	s_coords1 = s_coords1 / s_coords1.w;

	vec4 s_coords2 = shadow_coords2 * vec4(world_pos, 1.0);
	s_coords2.xy = s_coords2.xy * 0.5 + 0.5;
	s_coords2 = s_coords2 / s_coords2.w;

	const float bias = 0.0007;
	const float power = 15.0;
	float lit = 0.0;
	float dist = distance(world_pos, camera_pos);

	if (dist < max_cascade_dist0) {
		for (int i = 0; i < 4 * cast_shadows; i++) {

			float cascade0 = texture(sampler_shadow_map0, vec3(s_coords0.xy + poissonDisk[i] * 0.0008, s_coords0.z + bias));
			float cascade1 = texture(sampler_shadow_map1, vec3(s_coords1.xy + poissonDisk[i] * 0.0008, s_coords1.z + bias));
			float mix_factor = pow(dist / max_cascade_dist0, power);

			lit += 0.25 * mix(cascade0, cascade1, mix_factor);
		}
	}
	else if (dist < max_cascade_dist1) {
		for (int i = 0; i < 4 * cast_shadows; i++) {

			float cascade1 = texture(sampler_shadow_map1, vec3(s_coords1.xy + poissonDisk[i] * 0.0008, s_coords1.z + bias));
			float cascade2 = texture(sampler_shadow_map2, vec3(s_coords2.xy + poissonDisk[i] * 0.0008, s_coords2.z + bias));
			float mix_factor = pow(dist / max_cascade_dist1, power);

			lit += 0.25 * mix(cascade1, cascade2, mix_factor);
		}
	}
	else {
		for (int i = 0; i < 4 * cast_shadows; i++) {

			float cascade2 = texture(sampler_shadow_map2, vec3(s_coords2.xy + poissonDisk[i] * 0.0008, s_coords2.z + bias));

			lit += 0.25 * cascade2;
		}
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

	return lighting * ubo.lights[0].color.a;
}
