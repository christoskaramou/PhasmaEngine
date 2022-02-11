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

struct DirectionalLight
{
    vec4 color;// .a is the intensity
    vec4 direction;
};

struct PointLight
{
    vec4 color;
    vec4 position;// .w = radius
};

struct SpotLight
{
    vec4 color;
    vec4 start;
    vec4 end;// .w = radius
};

layout(push_constant) uniform Constants {
    float max_cascade_dist0;
    float max_cascade_dist1;
    float max_cascade_dist2;
    float max_cascade_dist3;
    float cast_shadows;
    float dummy[11];
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
layout(set = 0, binding = 9) uniform SS { mat4 invViewProj; vec4 effects0; vec4 effects1; vec4 effects2; vec4 effects3; } screenSpace;
layout(set = 1, binding = 0) uniform sampler2DShadow sampler_shadow_map0;
layout(set = 1, binding = 1) uniform sampler2DShadow sampler_shadow_map1;
layout(set = 1, binding = 2) uniform sampler2DShadow sampler_shadow_map2;
layout(set = 1, binding = 3) uniform sampler2DShadow sampler_shadow_map3;
layout(set = 1, binding = 4) uniform shadow_buffer0 { mat4 cascades[SHADOWMAP_CASCADES]; } sun;
layout(set = 2, binding = 0) uniform samplerCube sampler_cube_map;

vec2 poissonDisk[8] = vec2[](
vec2(0.493393f, 0.394269f),
vec2(0.798547f, 0.885922f),
vec2(0.247322f, 0.92645f),
vec2(0.0514542f, 0.140782f),
vec2(0.831843f, 0.00955229f),
vec2(0.428632f, 0.0171514f),
vec2(0.015656f, 0.749779f),
vec2(0.758385f, 0.49617f));

#define POISSON_SAMPLES 8

float textureProjLinear(sampler2DShadow shadowSampler, vec4 sc, float offsetScaled)
{
    vec2 pixelPos = sc.xy * SHADOWMAP_SIZE + vec2(0.5f);
    vec2 f = fract(pixelPos);
    vec2 pos = (pixelPos - f) * SHADOWMAP_TEXEL_SIZE;

    float dx = offsetScaled;
    float dy = offsetScaled;

    float tlTexel = textureProj(shadowSampler, vec4(pos + vec2(-dx, +dy), sc.z, sc.w));
    float trTexel = textureProj(shadowSampler, vec4(pos + vec2(+dx, +dy), sc.z, sc.w));
    float blTexel = textureProj(shadowSampler, vec4(pos + vec2(-dx, -dy), sc.z, sc.w));
    float brTexel = textureProj(shadowSampler, vec4(pos + vec2(+dx, -dy), sc.z, sc.w));

    float mixA = mix(tlTexel, trTexel, f.x);
    float mixB = mix(blTexel, brTexel, f.x);

    return mix(mixA, mixB, 1-f.y);
}

float FilterPoisson(vec4 sc, sampler2DShadow shadowSampler)
{
    float offsetScaled = 0.75 * SHADOWMAP_TEXEL_SIZE;

    float shadowFactor = 0.0;

    for (int i = 0; i < POISSON_SAMPLES; i++)
        shadowFactor += textureProjLinear(shadowSampler, vec4(sc.xy + poissonDisk[i] * offsetScaled, sc.z, sc.w), offsetScaled);

    return shadowFactor / float(POISSON_SAMPLES);
}

float FilterPCF(vec4 sc, sampler2DShadow shadowSampler)
{
    float offsetScaled = 0.75 * SHADOWMAP_TEXEL_SIZE;
    float dx = offsetScaled;
    float dy = offsetScaled;

    float shadowFactor = 0.0;
    int count = 0;
    int range = 1;

    for (int x = -range; x <= range; x++) {
        for (int y = -range; y <= range; y++) {
            shadowFactor += textureProjLinear(shadowSampler, vec4(sc.xy + vec2(dx * x, dy * y), sc.z, sc.w), offsetScaled);
            count++;
        }
    }

    return shadowFactor / count;
}

const mat4 biasMat = mat4(
    0.5, 0.0, 0.0, 0.0,
    0.0, 0.5, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.5, 0.5, 0.0, 1.0
);

float SampleShadowMap(int i, float zBias, vec3 worldPos)
{
    vec4 coords = biasMat * sun.cascades[i] * vec4(worldPos, 1.0);
    coords.z += zBias;

    if (i == 0)
        return FilterPCF(coords, sampler_shadow_map0);
    else if (i == 1)
        return FilterPCF(coords, sampler_shadow_map1);
    else if (i == 2)
        return FilterPCF(coords, sampler_shadow_map2);
    else
        return FilterPCF(coords, sampler_shadow_map3);
}

float CalculateShadows(vec3 worldPos, float depth, float NdL)
{
    float shadow = 1.0;

    if (pushConst.cast_shadows > 0.5)
    {
        float zBias = SHADOWMAP_TEXEL_SIZE * tan(acos(NdL));// cosTheta is dot( n,l ), clamped between 0 and 1
        zBias = clamp(zBias, 0, SHADOWMAP_TEXEL_SIZE * 2.0f);

        float d[4] = float[](
            pushConst.max_cascade_dist0,
            pushConst.max_cascade_dist1,
            pushConst.max_cascade_dist2,
            pushConst.max_cascade_dist3);

        for (int i = 0; i < 4; i++)
        {
            if (depth < d[i])
            {
                shadow = SampleShadowMap(i, zBias, worldPos);

                // Blend between cascades
                float dist = d[i] - depth;
				float blendDist = 1.0;
                
                if (i < 3 && dist < blendDist)
                    shadow = mix(shadow, SampleShadowMap(i + 1, zBias, worldPos), 1.0 - dist / blendDist);

                break;
            }
        }
    }

    return shadow;
}

vec3 DirectLight(Material material, vec3 worldPos, vec3 cameraPos, vec3 materialNormal, float occlusion, float shadow)
{
    float roughness = material.roughness * 0.75 + 0.25;

    // Compute directional light.
    vec3 lightDir = ubo.sun.direction.xyz;
    vec3 L = lightDir;
    vec3 V = normalize(cameraPos - worldPos);
    vec3 H = normalize(V + L);
    vec3 N = materialNormal;

    float NoV = clamp(dot(N, V), 0.001, 1.0);
    float NoL = clamp(dot(N, L), 0.001, 1.0);
    float HoV = clamp(dot(H, V), 0.001, 1.0);
    float LoV = clamp(dot(L, V), 0.001, 1.0);

    vec3 F0 = ComputeF0(material.albedo, material.metallic);
    vec3 specularFresnel = Fresnel(F0, HoV);
    vec3 specRef = ubo.sun.color.xyz * NoL * shadow * CookTorranceSpecular(N, H, NoL, NoV, specularFresnel, roughness);
    vec3 diffRef = ubo.sun.color.xyz * NoL * shadow * (1.0 - specularFresnel) * (1.0 / PI);

    vec3 reflectedLight = specRef;
    vec3 diffuseLight = diffRef * material.albedo * (1.0 - material.metallic);
    vec3 lighting = reflectedLight + diffuseLight;

    return lighting * ubo.sun.color.a;
}

vec3 ComputePointLight(int lightIndex, Material material, vec3 worldPos, vec3 cameraPos, vec3 materialNormal, float occlusion)
{
    vec3 lightDirFull = worldPos - ubo.pointLights[lightIndex].position.xyz;
    float lightDist = max(0.1, length(lightDirFull));
    if (lightDist > screenSpace.effects2.z)// max range
    return vec3(0.0);

    vec3 lightDir = normalize(-lightDirFull);
    float attenuation = 1 / (lightDist * lightDist);
    vec3 pointColor = ubo.pointLights[lightIndex].color.xyz * attenuation;
    pointColor *= screenSpace.effects2.y * occlusion;// intensity

    float roughness = material.roughness * 0.75 + 0.25;

    vec3 L = lightDir;
    vec3 V = normalize(cameraPos - worldPos);
    vec3 H = normalize(V + L);
    vec3 N = materialNormal;

    float NoV = clamp(dot(N, V), 0.001, 1.0);
    float NoL = clamp(dot(N, L), 0.001, 1.0);
    float HoV = clamp(dot(H, V), 0.001, 1.0);
    float LoV = clamp(dot(L, V), 0.001, 1.0);

    vec3 F0 = ComputeF0(material.albedo, material.metallic);
    vec3 specularFresnel = Fresnel(F0, HoV);
    vec3 specRef = NoL * CookTorranceSpecular(N, H, NoL, NoV, specularFresnel, roughness);
    vec3 diffRef = NoL * (1.0 - specularFresnel) * (1.0 / PI);

    vec3 reflectedLight = specRef;
    vec3 diffuseLight = diffRef * material.albedo * (1.0 - material.metallic);

    vec3 res = pointColor * (reflectedLight + diffuseLight);

    return res;
}

// From Panos Karabelas https://github.com/PanosK92/SpartanEngine/blob/master/Data/shaders/IBL.hlsl
float3 SampleEnvironment(samplerCube tex_environment, float3 dir, float mipLevel)
{
    ivec2 texDim = textureSize(tex_environment, int(mipLevel));
    float2 resolution = vec2(float(texDim.x), float(texDim.y));
    float2 texelSize = 1.0 / resolution;

    float dx            = 1.0f * texelSize.x;
    float dy            = 1.0f * texelSize.y;
    float dz            = 0.5f * (texelSize.x + texelSize.y);

    float3 d0            = textureLod(tex_environment, dir + float3(0.0, 0.0, 0.0), mipLevel).rgb;
    float3 d1            = textureLod(tex_environment, dir + float3(-dx, -dy, -dz), mipLevel).rgb;
    float3 d2            = textureLod(tex_environment, dir + float3(-dx, -dy, +dz), mipLevel).rgb;
    float3 d3            = textureLod(tex_environment, dir + float3(-dx, +dy, +dz), mipLevel).rgb;
    float3 d4            = textureLod(tex_environment, dir + float3(+dx, -dy, -dz), mipLevel).rgb;
    float3 d5            = textureLod(tex_environment, dir + float3(+dx, -dy, +dz), mipLevel).rgb;
    float3 d6            = textureLod(tex_environment, dir + float3(+dx, +dy, -dz), mipLevel).rgb;
    float3 d7            = textureLod(tex_environment, dir + float3(+dx, +dy, +dz), mipLevel).rgb;

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
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.0f, -0.04f);
    float4 r        = roughness * c0 + c1;
    float a004        = min(r.x * r.x, exp2(-9.28f * NdV)) * r.x + r.y;
    float2 AB        = float2(-1.04f, 1.04f) * a004 + r.zw;
    return specColor * AB.x + AB.y;
}

struct IBL { float3 final_color; float3 reflectivity; };
IBL ImageBasedLighting(Material material, float3 normal, float3 camera_to_pixel, samplerCube tex_environment, sampler2D tex_lutIBL)
{
    IBL ibl;

    float3 reflection    = reflect(camera_to_pixel, normal);
    // From Sebastien Lagarde Moving Frostbite to PBR page 69
    reflection    = GetSpecularDominantDir(normal, reflection, material.roughness);

    float NdV    = saturate(dot(-camera_to_pixel, normal));
    float3 F    = FresnelSchlickRoughness(NdV, material.F0, material.roughness);

    float3 kS    = F;// The energy of light that gets reflected
    float3 kD    = 1.0f - kS;// Remaining energy, light that gets refracted
    kD            *= 1.0f - material.metallic;

    // Diffuse
    float3 irradiance    = SampleEnvironment(tex_environment, normal, 8);
    float3 cDiffuse        = irradiance * material.albedo;

    // Specular
    float mipLevel            = max(0.001f, material.roughness * material.roughness) * 11.0f;// max lod 11.0f
    float3 prefilteredColor    = SampleEnvironment(tex_environment, reflection, mipLevel);
    float2 envBRDF        = texture(tex_lutIBL, float2(NdV, material.roughness)).xy;
    ibl.reflectivity        = F * envBRDF.x + envBRDF.y;
    float3 cSpecular        = prefilteredColor * ibl.reflectivity;

    ibl.final_color = kD * cDiffuse + cSpecular;

    return ibl;
}
// ----------------------------------------------------
// Reference for volumetric lighting:
// https://github.com/PanosK92/SpartanEngine/blob/master/Data/shaders/VolumetricLighting.hlsl

vec3 DitherValve(vec2 screenPos)
{
    float _dot = dot(vec2(171.0, 231.0), screenPos);
    vec3 dither = vec3(_dot, _dot, _dot);
    dither = fract(dither / vec3(103.0, 71.0, 97.0));

    return dither / 255.0;
}

// https://www.shadertoy.com/view/MslGR8
float3 Dither(float2 uv)
{
    int2 texDim = textureSize(sampler_albedo, 0);
    float2 screenPos = uv * float2(float(texDim.x), float(texDim.y));

    // bit-depth of display. Normally 8 but some LCD monitors are 7 or even 6-bit.
    float ditherBit = 8.0;

    // compute grid position
    float gridPosition = frac(dot(screenPos.xy - float2(0.5, 0.5), float2(1.0/16.0, 10.0/36.0) + 0.25));

    // compute how big the shift should be
    float ditherShift = (0.25) * (1.0 / (pow(2.0, ditherBit) - 1.0));

    // shift the individual colors differently, thus making it even harder to see the dithering pattern
    //float3 ditherShiftRGB = float3(ditherShift, -ditherShift, ditherShift); //subpixel dithering (chromatic)
    float3 ditherShiftRGB = float3(ditherShift, ditherShift, ditherShift);//non-chromatic dithering

    // modify shift acording to grid position.
    ditherShiftRGB = lerp(2.0 * ditherShiftRGB, -2.0 * ditherShiftRGB, gridPosition);//shift acording to grid position.

    // return dither shift
    return 0.5/255.0 + ditherShiftRGB;
}

// Mie scattering approximated with Henyey-Greenstein phase function.
float ComputeScattering(float dirDotL)
{
    float scattering = 0.95f;
    float result = 1.0f - scattering * scattering;
    float e = abs(1.0f + scattering * scattering - (2.0f * scattering) * dirDotL);
    result /= pow(e, 0.1f);
    return result;
}

vec3 VolumetricLighting(DirectionalLight light, vec3 worldPos, vec2 uv, mat4 lightViewProj, float fogFactor)
{
    float iterations = screenSpace.effects1.z;

    vec3 pixelToCamera            = ubo.camPos.xyz - worldPos;
    float pixelToCameraLength    = length(pixelToCamera);
    vec3 rayDir                    = pixelToCamera / pixelToCameraLength;
    float stepLength                = pixelToCameraLength / iterations;
    vec3 rayStep                    = rayDir * stepLength;
    vec3 rayPos                    = worldPos;

    // Apply dithering as it will allows us to get away with a crazy low sample count ;-)
    ivec2 texDim = textureSize(sampler_albedo, 0);
    vec3 ditherValue = Dither(uv * vec2(float(texDim.x), float(texDim.y))) * screenSpace.effects1.w;// dithering strength 400 default
    rayPos += rayStep * ditherValue;


    // screenSpace.effects2.w -> fog height position
    // screenSpace.effects2.x -> fog spread
    // screenSpace.effects3.x -> fog intensity

    vec3 volumetricFactor = vec3(0.0f);
    for (int i = 0; i < iterations; i++)
    {
        // Compute position in light space
        vec4 posLight = lightViewProj * vec4(rayPos, 1.0f);
        posLight /= posLight.w;

        // Compute ray uv
        vec2 rayUV = posLight.xy * vec2(0.5f, 0.5f) + 0.5f;

        // Check to see if the light can "see" the pixel		
        float depthDelta = texture(sampler_shadow_map1, vec3(rayUV, posLight.z));
        if (depthDelta > 0.0f)
        {
            volumetricFactor += ComputeScattering(dot(rayDir, light.direction.xyz));
        }
        rayPos += rayStep;
    }
    volumetricFactor /= iterations;

    return volumetricFactor * light.color.xyz * light.color.a * fogFactor * 10.0;
}

#endif
