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

#ifndef LIGHT_H_
#define LIGHT_H_

#include "../Common/tonemapping.glsl"
#include "../Common/common.glsl"
#include "pbr.glsl"
#include "Material.glsl"

#define MAX_POINT_LIGHTS 10
#define MAX_SPOT_LIGHTS 10

struct DirectionalLight
{
	vec4 color; // .a is the intensity
	vec4 direction;
};

struct PointLight
{
	vec4 color;
	vec4 position; // .w = radius
};
	
struct SpotLight
{
	vec4 color;
	vec4 start;
	vec4 end; // .w = radius
};

layout(push_constant) uniform Constants {
	float max_cascade_dist0;
	float max_cascade_dist1;
	float max_cascade_dist2;
	float cast_shadows;
} pushConst;
layout(set = 0, binding = 0) uniform sampler2D sampler_depth;
layout(set = 0, binding = 1) uniform sampler2D sampler_normal;
layout(set = 0, binding = 2) uniform sampler2D sampler_albedo;
layout(set = 0, binding = 3) uniform sampler2D sampler_met_rough;
layout(set = 0, binding = 4) uniform UBO
{
	vec4 camPos;
	DirectionalLight sun;
	PointLight pointLights[MAX_POINT_LIGHTS];
	SpotLight spotLights[MAX_SPOT_LIGHTS];
}
ubo;
layout(set = 0, binding = 5) uniform sampler2D sampler_ssao_blur;
layout(set = 0, binding = 6) uniform sampler2D sampler_ssr;
layout(set = 0, binding = 7) uniform sampler2D sampler_emission;
layout(set = 0, binding = 8) uniform sampler2D sampler_lut_IBL;
layout(set = 0, binding = 9) uniform SS { mat4 invViewProj; vec4 effects0; vec4 effects1; vec4 effects2; vec4 effects3;} screenSpace;
layout(set = 1, binding = 0) uniform sampler2DShadow sampler_shadow_map0;
layout(set = 1, binding = 1) uniform sampler2DShadow sampler_shadow_map1;
layout(set = 1, binding = 2) uniform sampler2DShadow sampler_shadow_map2;
layout(set = 1, binding = 3) uniform shadow_buffer0 { mat4 cascades[3]; } sun;
layout(set = 2, binding = 0) uniform samplerCube sampler_cube_map;

vec3 compute_point_light(int lightIndex, Material material, vec3 world_pos, vec3 camera_pos, vec3 material_normal, float ssao)
{
	vec3 light_dir_full = world_pos - ubo.pointLights[lightIndex].position.xyz;
	float light_dist = max(0.1, length(light_dir_full));
	if (light_dist > screenSpace.effects2.z) // max range
		return vec3(0.0);

	vec3 light_dir = normalize(-light_dir_full);
	float attenuation = 1 / (light_dist * light_dist);
	vec3 point_color = ubo.pointLights[lightIndex].color.xyz * attenuation;
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

struct IBL { float3 final_color; float3 reflectivity; };
IBL ImageBasedLighting(Material material, float3 normal, float3 camera_to_pixel, samplerCube tex_environment, sampler2D tex_lutIBL)
{
	IBL ibl;

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
	ibl.reflectivity		= F * envBRDF.x + envBRDF.y;
	float3 cSpecular 		= prefilteredColor * ibl.reflectivity;

	ibl.final_color = kD * cDiffuse + cSpecular;

	return ibl; 
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

// https://www.shadertoy.com/view/MslGR8
float3 Dither(float2 uv)
{
	int2 texDim = textureSize(sampler_albedo, 0);
	float2 screen_pos = uv * float2(float(texDim.x), float(texDim.y));

	// bit-depth of display. Normally 8 but some LCD monitors are 7 or even 6-bit.
	float dither_bit = 8.0; 
	
	// compute grid position
	float grid_position = frac(dot(screen_pos.xy - float2(0.5, 0.5), float2(1.0/16.0,10.0/36.0) + 0.25));
	
	// compute how big the shift should be
	float dither_shift = (0.25) * (1.0 / (pow(2.0, dither_bit) - 1.0));
	
	// shift the individual colors differently, thus making it even harder to see the dithering pattern
	//float3 dither_shift_RGB = float3(dither_shift, -dither_shift, dither_shift); //subpixel dithering (chromatic)
	float3 dither_shift_RGB = float3(dither_shift, dither_shift, dither_shift); //non-chromatic dithering
	
	// modify shift acording to grid position.
	dither_shift_RGB = lerp(2.0 * dither_shift_RGB, -2.0 * dither_shift_RGB, grid_position); //shift acording to grid position.
	
	// return dither shift
	return 0.5/255.0 + dither_shift_RGB; 
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

vec3 VolumetricLighting(DirectionalLight light, vec3 pos_world, vec2 uv, mat4 lightViewProj, float fog_factor)
{
	float iterations = screenSpace.effects1.z;

	vec3 pixel_to_camera 			= ubo.camPos.xyz - pos_world;
	float pixel_to_camera_length 	= length(pixel_to_camera);
	vec3 ray_dir					= pixel_to_camera / pixel_to_camera_length;
	float step_length 				= pixel_to_camera_length / iterations;
	vec3 ray_step 					= ray_dir * step_length;
	vec3 ray_pos 					= pos_world;

	// Apply dithering as it will allows us to get away with a crazy low sample count ;-)
	ivec2 texDim = textureSize(sampler_albedo, 0);
	vec3 dither_value = Dither(uv * vec2(float(texDim.x), float(texDim.y))) * screenSpace.effects1.w; // dithering strength 400 default
	ray_pos += ray_step * dither_value;


	// screenSpace.effects2.w -> fog height position
	// screenSpace.effects2.x -> fog spread
	// screenSpace.effects3.x -> fog intensity

	vec3 volumetricFactor = vec3(0.0f);
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
			volumetricFactor += ComputeScattering(dot(ray_dir, light.direction.xyz));
		}
		ray_pos += ray_step;
	}
	volumetricFactor /= iterations;

	return volumetricFactor * light.color.xyz * light.color.a * fog_factor * 10.0;
}
// ----------------------------------------------------

#endif