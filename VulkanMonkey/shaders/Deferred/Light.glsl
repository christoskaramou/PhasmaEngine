#ifndef LIGHT_H_
#define LIGHT_H_

#include "pbr.glsl"
#include "Material.glsl"

struct Light {
	vec4 color;
	vec4 position;
	vec4 attenuation;
};


layout(push_constant) uniform SS { vec4 effect; vec4 size; mat4 invViewProj; } screenSpace;
layout(constant_id = 0) const int NUM_LIGHTS = 1;
layout(set = 0, binding = 0) uniform sampler2D samplerDepth;
layout(set = 0, binding = 1) uniform sampler2D samplerNormal;
layout(set = 0, binding = 2) uniform sampler2D samplerAlbedo;
layout(set = 0, binding = 3) uniform sampler2D samplerRoughMet;
layout(set = 0, binding = 4) uniform UBO { vec4 camPos; Light lights[NUM_LIGHTS + 1]; } ubo;
layout(set = 0, binding = 5) uniform sampler2D ssaoBlurSampler;
layout(set = 0, binding = 6) uniform sampler2D ssrSampler;
//layout(set = 0, binding = 7) uniform sampler2D ssdoBlurSampler;
layout(set = 1, binding = 1) uniform sampler2DShadow shadowMapSampler0;
layout(set = 2, binding = 1) uniform sampler2DShadow shadowMapSampler1;
layout(set = 3, binding = 1) uniform sampler2DShadow shadowMapSampler2;
layout(set = 4, binding = 1) uniform samplerCube cubemapSampler;

vec3 compute_point_light(int lightIndex, Material material, vec3 world_pos, vec3 camera_pos, vec3 material_normal)
{
	vec3 light_dir_full = world_pos - ubo.lights[lightIndex].position.xyz;
	vec3 light_dir = normalize(-light_dir_full);
	float light_dist = max(0.1, length(light_dir_full));
	vec3 point_color = ubo.lights[lightIndex].color.xyz / (light_dist * light_dist);// *0.5);

	float roughness = material.roughness * 0.75 + 0.25;

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
	vec3 specref = NoL * cook_torrance_specular(N, H, NoL, NoV, specular_fresnel, roughness);
	vec3 diffref = NoL * (1.0 - specular_fresnel) * (1.0 / PI);

	vec3 reflected_light = specref;
	vec3 diffuse_light = diffref * material.albedo * (1.0 - material.metallic);

	vec3 res = point_color * (reflected_light + diffuse_light);

	return res;
}

#endif