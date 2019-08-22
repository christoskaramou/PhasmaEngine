#ifndef LIGHT_H_
#define LIGHT_H_

#include "../Common/tonemapping.glsl"
#include "../Common/common.glsl"
#include "pbr.glsl"
#include "Material.glsl"

struct Light {
	vec4 color; // .a is the intensity
	vec4 position;
};

layout(constant_id = 0) const int NUM_LIGHTS = 1;
layout(set = 0, binding = 0) uniform sampler2D sampler_depth;
layout(set = 0, binding = 1) uniform sampler2D sampler_normal;
layout(set = 0, binding = 2) uniform sampler2D sampler_albedo;
layout(set = 0, binding = 3) uniform sampler2D sampler_met_rough;
layout(set = 0, binding = 4) uniform UBO { vec4 cam_pos; Light lights[NUM_LIGHTS + 1]; } ubo;
layout(set = 0, binding = 5) uniform sampler2D sampler_ssao_blur;
layout(set = 0, binding = 6) uniform sampler2D sampler_ssr;
layout(set = 0, binding = 7) uniform sampler2D sampler_emission;
layout(set = 0, binding = 8) uniform sampler2D sampler_lut_IBL;
layout(set = 0, binding = 9) uniform SS { mat4 invViewProj; vec4 effects0; vec4 effects1; vec4 effects2; vec4 effects3;} screenSpace;
layout(set = 1, binding = 1) uniform sampler2DShadow sampler_shadow_map0;
layout(set = 2, binding = 1) uniform sampler2DShadow sampler_shadow_map1;
layout(set = 3, binding = 1) uniform sampler2DShadow sampler_shadow_map2;
layout(set = 4, binding = 1) uniform samplerCube sampler_cube_map;

vec3 compute_point_light(int lightIndex, Material material, vec3 world_pos, vec3 camera_pos, vec3 material_normal, float ssao)
{
	vec3 light_dir_full = world_pos - ubo.lights[lightIndex].position.xyz;
	float light_dist = max(0.1, length(light_dir_full));
	if (light_dist > screenSpace.effects2.z) // max range
		return vec3(0.0);

	vec3 light_dir = normalize(-light_dir_full);
	float attenuation = 1 / (light_dist * light_dist);
	vec3 point_color = ubo.lights[lightIndex].color.xyz * attenuation;
	point_color *= screenSpace.effects2.y * ssao; // intensity

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

// From Panos Karabelas https://github.com/PanosK92/SpartanEngine/blob/master/Data/shaders/IBL.hlsl
float3 SampleEnvironment(samplerCube tex_environment, float3 dir, float mipLevel)
{
	ivec2 texDim = textureSize(tex_environment, int(mipLevel));
	float2 resolution = vec2(float(texDim.x), float(texDim.y));
	float2 texelSize = 1.0 / resolution;

	float dx 			= 1.0f * texelSize.x;
	float dy 			= 1.0f * texelSize.y;
	float dz 			= 0.5f * (texelSize.x + texelSize.y);

	float3 d0 			= textureLod(tex_environment, dir + float3(0.0, 0.0, 0.0), mipLevel).rgb;
	float3 d1 			= textureLod(tex_environment, dir + float3(-dx, -dy, -dz), mipLevel).rgb;
	float3 d2			= textureLod(tex_environment, dir + float3(-dx, -dy, +dz), mipLevel).rgb;
	float3 d3			= textureLod(tex_environment, dir + float3(-dx, +dy, +dz), mipLevel).rgb;
	float3 d4			= textureLod(tex_environment, dir + float3(+dx, -dy, -dz), mipLevel).rgb;
	float3 d5			= textureLod(tex_environment, dir + float3(+dx, -dy, +dz), mipLevel).rgb;
	float3 d6			= textureLod(tex_environment, dir + float3(+dx, +dy, -dz), mipLevel).rgb;
	float3 d7			= textureLod(tex_environment, dir + float3(+dx, +dy, +dz), mipLevel).rgb;

	return (d0 + d1 + d2 + d3 + d4 + d5 + d6 + d7) * 0.125f;
}

float3 GetSpecularDominantDir(float3 normal, float3 reflection, float roughness)
{
	const float smoothness = 1.0f - roughness;
	const float lerpFactor = smoothness * (sqrt(smoothness) + roughness);
	return lerp(normal, reflection, lerpFactor);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
	float smoothness = 1.0 - roughness;
    return F0 + (max(float3(smoothness, smoothness, smoothness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
} 

// https://www.unrealengine.com/blog/physically-based-shading-on-mobile
float3 EnvBRDFApprox(float3 specColor, float roughness, float NdV)
{
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f );
    const float4 c1 = float4(1.0f, 0.0425f, 1.0f, -0.04f );
    float4 r 		= roughness * c0 + c1;
    float a004 		= min(r.x * r.x, exp2(-9.28f * NdV)) * r.x + r.y;
    float2 AB 		= float2(-1.04f, 1.04f) * a004 + r.zw;
    return specColor * AB.x + AB.y;
}

float3 ImageBasedLighting(Material material, float3 normal, float3 camera_to_pixel, samplerCube tex_environment, sampler2D tex_lutIBL)
{
	float3 reflection 	= reflect(camera_to_pixel, normal);
	// From Sebastien Lagarde Moving Frostbite to PBR page 69
	reflection	= GetSpecularDominantDir(normal, reflection, material.roughness);

	float NdV 	= saturate(dot(-camera_to_pixel, normal));
	float3 F 	= FresnelSchlickRoughness(NdV, material.F0, material.roughness);

	float3 kS 	= F; 			// The energy of light that gets reflected
	float3 kD 	= 1.0f - kS;	// Remaining energy, light that gets refracted
	kD 			*= 1.0f - material.metallic;	

	// Diffuse
	float3 irradiance	= SampleEnvironment(tex_environment, normal, 8);
	float3 cDiffuse		= irradiance * material.albedo;

	// Specular
	float mipLevel 			= max(0.001f, material.roughness * material.roughness) * 11.0f; // max lod 11.0f
	float3 prefilteredColor	= SampleEnvironment(tex_environment, reflection, mipLevel);
	float2 envBRDF  		= texture(tex_lutIBL, float2(NdV, material.roughness)).xy;
	float3 cSpecular 		= prefilteredColor * (F * envBRDF.x + envBRDF.y);

	return kD * cDiffuse + cSpecular; 
}
// ----------------------------------------------------
// Reference for volumetric lighting:
// https://github.com/PanosK92/SpartanEngine/blob/master/Data/shaders/VolumetricLighting.hlsl

vec3 Dither_Valve(vec2 screen_pos)
{
	float _dot = dot(vec2(171.0, 231.0), screen_pos);
    vec3 dither = vec3(_dot, _dot, _dot);
    dither = fract(dither / vec3(103.0, 71.0, 97.0));
    
    return dither / 255.0;
}

// Mie scattering approximated with Henyey-Greenstein phase function.
float ComputeScattering(float dir_dot_l)
{
	float scattering = 0.95f;
	float result = 1.0f - scattering * scattering;
	float e = abs(1.0f + scattering * scattering - (2.0f * scattering) * dir_dot_l);
	result /= pow(e, 0.1f);
	return result;
}

vec3 VolumetricLighting(Light light, vec3 pos_world, vec2 uv, mat4 lightViewProj)
{
	float iterations = screenSpace.effects1.z; // 64 iterations default

	vec3 pixel_to_camera 			= ubo.cam_pos.xyz - pos_world;
	float pixel_to_camera_length 	= length(pixel_to_camera);
	vec3 ray_dir					= pixel_to_camera / pixel_to_camera_length;
	float step_length 				= pixel_to_camera_length / iterations;
	vec3 ray_step 					= ray_dir * step_length;
	vec3 ray_pos 					= pos_world;

	// Apply dithering as it will allows us to get away with a crazy low sample count ;-)
	ivec2 texDim = textureSize(sampler_albedo, 0);
	vec3 dither_value = Dither_Valve(uv * vec2(float(texDim.x), float(texDim.y))) * screenSpace.effects1.w; // dithering strength 400 default
	ray_pos += ray_step * dither_value;
	
	
	// screenSpace.effects2.w -> fog height position
	// screenSpace.effects2.x -> fog spread
	// screenSpace.effects3.x -> fog intensity

	vec3 fog = vec3(0.0f);
	float fog_height_factor = pos_world.y + screenSpace.effects2.w;
	fog_height_factor = clamp(exp(-(fog_height_factor * fog_height_factor * screenSpace.effects2.x)), 0.0, 1.0);
	fog_height_factor *= screenSpace.effects3.x * 10.0;

	for (int i = 0; i < iterations; i++)
	{
		// Compute position in light space
		vec4 pos_light = lightViewProj * vec4(ray_pos, 1.0f);
		pos_light /= pos_light.w;
		
		// Compute ray uv
		vec2 ray_uv = pos_light.xy * vec2(0.5f, 0.5f) + 0.5f;
		
		// Check to see if the light can "see" the pixel		
		float depth_delta = texture(sampler_shadow_map1, vec3(ray_uv, pos_light.z)).r;
		if (depth_delta > 0.0f)
		{
			fog += ComputeScattering(dot(ray_dir, normalize(-light.position.xyz)));
		}
		ray_pos += ray_step;
	}
	fog /= iterations;

	return fog * light.color.xyz * light.color.a * fog_height_factor;
}
// ----------------------------------------------------

#endif