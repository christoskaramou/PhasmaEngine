#ifndef LIGHT_H_
#define LIGHT_H_

#include "../Common/tonemapping.hlsl"
#include "../Common/common.hlsl"
#include "pbr.hlsl"
#include "Material.hlsl"

struct DirectionalLight
{
    float4 color; // .a is the intensity
    float4 direction;
};

struct PointLight
{
    float4 color;
    float4 position; // .w = radius
};

struct SpotLight
{
    float4 color;
    float4 start;
    float4 end; // .w = radius
};

struct PushConstants
{ 
    float max_cascade_dist[SHADOWMAP_CASCADES];
    float cast_shadows;
};
[[vk::push_constant]] PushConstants pc;

TexSamplerDecl(0, 0, Depth)
TexSamplerDecl(1, 0, Normal)
TexSamplerDecl(2, 0, Albedo)
TexSamplerDecl(3, 0, MetRough)
TexSamplerDecl(5, 0, SsaoBlur)
TexSamplerDecl(6, 0, Ssr)
TexSamplerDecl(7, 0, Emission)
TexSamplerDecl(8, 0, LutIBL)
[[vk::binding(1, 1)]] Texture2D Shadow[SHADOWMAP_CASCADES];
[[vk::binding(2, 1)]] SamplerComparisonState sampler_Shadow;
CubeSamplerDecl(0, 2, Cube)

[[vk::binding(4, 0)]] cbuffer UBO
{
    float4              cb_camPos;
    DirectionalLight    cb_sun;
    PointLight          cb_pointLights[MAX_POINT_LIGHTS];
    SpotLight           cb_spotLights[MAX_SPOT_LIGHTS];
};

[[vk::binding(9, 0)]] cbuffer ScreenSpace
{
    float4x4    cb_invViewProj; 
    float4      cb_effects0;
    float4      cb_effects1;
    float4      cb_effects2;
    float4      cb_effects3;
};

[[vk::binding(0, 1)]] cbuffer shadow_buffer
{
    float4x4 cb_cascades[SHADOWMAP_CASCADES];
};

static const float2 poissonDisk[8] = {
    float2(0.493393f, 0.394269f),
    float2(0.798547f, 0.885922f),
    float2(0.247322f, 0.92645f),
    float2(0.0514542f, 0.140782f),
    float2(0.831843f, 0.00955229f),
    float2(0.428632f, 0.0171514f),
    float2(0.015656f, 0.749779f),
    float2(0.758385f, 0.49617f)
};

#define POISSON_SAMPLES 8

float ShadowProjLinear(Texture2D shadowTex, SamplerComparisonState sampler_shadow, float4 sc, float offsetScaled)
{
    float2 pixelPos = sc.xy * SHADOWMAP_SIZE + 0.5f;
    float2 f = frac(pixelPos);
    float2 pos = (pixelPos - f) * SHADOWMAP_TEXEL_SIZE;

    float dx = offsetScaled;
    float dy = offsetScaled;

    float tlTexel = shadowTex.SampleCmpLevelZero(sampler_shadow, float2((pos + float2(-dx, +dy)) / sc.w), sc.z);
    float trTexel = shadowTex.SampleCmpLevelZero(sampler_shadow, float2((pos + float2(+dx, +dy)) / sc.w), sc.z);
    float blTexel = shadowTex.SampleCmpLevelZero(sampler_shadow, float2((pos + float2(-dx, -dy)) / sc.w), sc.z);
    float brTexel = shadowTex.SampleCmpLevelZero(sampler_shadow, float2((pos + float2(+dx, -dy)) / sc.w), sc.z);

    float mixA = lerp(tlTexel, trTexel, f.x);
    float mixB = lerp(blTexel, brTexel, f.x);

    return lerp(mixA, mixB, 1-f.y);
}

float FilterPoisson(float4 sc, Texture2D shadowTex, SamplerComparisonState sampler_shadow)
{
    float offsetScaled = 0.75 * SHADOWMAP_TEXEL_SIZE;

    float shadowFactor = 0.0;

    for (int i = 0; i < POISSON_SAMPLES; i++)
        shadowFactor += ShadowProjLinear(shadowTex, sampler_shadow, float4(sc.xy + poissonDisk[i] * offsetScaled, sc.z, sc.w), offsetScaled);

    return shadowFactor / POISSON_SAMPLES;
}

float FilterPCF(float4 sc, Texture2D shadowTex, SamplerComparisonState sampler_shadow)
{
    float offsetScaled = 0.75 * SHADOWMAP_TEXEL_SIZE;
    float dx = offsetScaled;
    float dy = offsetScaled;

    float shadowFactor = 0.0;
    int count = 0;
    int range = 1;

    for (int x = -range; x <= range; x++) {
        for (int y = -range; y <= range; y++) {
            shadowFactor += ShadowProjLinear(shadowTex, sampler_shadow, float4(sc.xy + float2(dx * x, dy * y), sc.z, sc.w), offsetScaled);
            count++;
        }
    }

    return shadowFactor / count;
}

static const float4x4 biasMat = float4x4(
    0.5, 0.0, 0.0, 0.0,
    0.0, 0.5, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.5, 0.5, 0.0, 1.0
);

float SampleShadowMap(int i, float zBias, float3 worldPos)
{
    float4 coords = mul(float4(worldPos, 1.0), mul(cb_cascades[i], biasMat));
    coords.z += zBias;

    return FilterPCF(coords, Shadow[i], sampler_Shadow);
}

float CalculateShadows(float3 worldPos, float depth, float NdL)
{
    float shadow = 1.0;

    if (pc.cast_shadows > 0.5)
    {
        float zBias = SHADOWMAP_TEXEL_SIZE * tan(acos(NdL));// cosTheta is dot( n,l ), clamped between 0 and 1
        zBias = clamp(zBias, 0, SHADOWMAP_TEXEL_SIZE * 2.0f);

        for (int i = 0; i < SHADOWMAP_CASCADES; i++)
        {
            if (depth < pc.max_cascade_dist[i])
            {
                shadow = SampleShadowMap(i, zBias, worldPos);

                // Blend between cascades
                float dist = pc.max_cascade_dist[i] - depth;
				float blendDist = 1.0;
                
                if (i < SHADOWMAP_CASCADES - 1 && dist < blendDist)
                    shadow = lerp(shadow, SampleShadowMap(i + 1, zBias, worldPos), 1.0 - dist / blendDist);

                break;
            }
        }
    }

    return shadow;
}

float3 DirectLight(Material material, float3 worldPos, float3 cameraPos, float3 materialNormal, float occlusion, float shadow)
{
    float roughness = material.roughness * 0.75 + 0.25;

    // Compute directional light.
    float3 lightDir = cb_sun.direction.xyz;
    float3 L = lightDir;
    float3 V = normalize(cameraPos - worldPos);
    float3 H = normalize(V + L);
    float3 N = materialNormal;

    float NoV = clamp(dot(N, V), 0.001, 1.0);
    float NoL = clamp(dot(N, L), 0.001, 1.0);
    float HoV = clamp(dot(H, V), 0.001, 1.0);
    float LoV = clamp(dot(L, V), 0.001, 1.0);

    float3 F0 = ComputeF0(material.albedo, material.metallic);
    float3 specularFresnel = Fresnel(F0, HoV);
    float3 specRef = cb_sun.color.xyz * NoL * shadow * CookTorranceSpecular(N, H, NoL, NoV, specularFresnel, roughness);
    float3 diffRef = cb_sun.color.xyz * NoL * shadow * (1.0 - specularFresnel) * (1.0 / PI);

    float3 reflectedLight = specRef;
    float3 diffuseLight = diffRef * material.albedo * (1.0 - material.metallic);
    float3 lighting = reflectedLight + diffuseLight;

    return lighting * cb_sun.color.a;
}

float3 ComputePointLight(int lightIndex, Material material, float3 worldPos, float3 cameraPos, float3 materialNormal, float occlusion)
{
    float3 lightDirFull = worldPos - cb_pointLights[lightIndex].position.xyz;
    float lightDist = max(0.1, length(lightDirFull));

    if (lightDist > cb_effects2.z) // max range
        return 0.0;

    float3 lightDir = normalize(-lightDirFull);
    float attenuation = 1 / (lightDist * lightDist);
    float3 pointColor = cb_pointLights[lightIndex].color.xyz * attenuation;
    pointColor *= cb_effects2.y * occlusion; // intensity

    float roughness = material.roughness * 0.75 + 0.25;

    float3 L = lightDir;
    float3 V = normalize(cameraPos - worldPos);
    float3 H = normalize(V + L);
    float3 N = materialNormal;

    float NoV = clamp(dot(N, V), 0.001, 1.0);
    float NoL = clamp(dot(N, L), 0.001, 1.0);
    float HoV = clamp(dot(H, V), 0.001, 1.0);
    float LoV = clamp(dot(L, V), 0.001, 1.0);

    float3 F0 = ComputeF0(material.albedo, material.metallic);
    float3 specularFresnel = Fresnel(F0, HoV);
    float3 specRef = NoL * CookTorranceSpecular(N, H, NoL, NoV, specularFresnel, roughness);
    float3 diffRef = NoL * (1.0 - specularFresnel) * (1.0 / PI);

    float3 reflectedLight = specRef;
    float3 diffuseLight = diffRef * material.albedo * (1.0 - material.metallic);

    float3 res = pointColor * (reflectedLight + diffuseLight);

    return res;
}

float3 SampleEnvironment(TextureCube cube, SamplerState sampler_cube, float3 dir, float mipLevel)
{
    float2 texelSize = TexelSize(uint(mipLevel), cube);

    float dx = 1.0f * texelSize.x;
    float dy = 1.0f * texelSize.y;
    float dz = 0.5f * (texelSize.x + texelSize.y);

    float3 d0 = cube.SampleLevel(sampler_cube, dir + float3(0.0, 0.0, 0.0), mipLevel).rgb;
    float3 d1 = cube.SampleLevel(sampler_cube, dir + float3(-dx, -dy, -dz), mipLevel).rgb;
    float3 d2 = cube.SampleLevel(sampler_cube, dir + float3(-dx, -dy, +dz), mipLevel).rgb;
    float3 d3 = cube.SampleLevel(sampler_cube, dir + float3(-dx, +dy, +dz), mipLevel).rgb;
    float3 d4 = cube.SampleLevel(sampler_cube, dir + float3(+dx, -dy, -dz), mipLevel).rgb;
    float3 d5 = cube.SampleLevel(sampler_cube, dir + float3(+dx, -dy, +dz), mipLevel).rgb;
    float3 d6 = cube.SampleLevel(sampler_cube, dir + float3(+dx, +dy, -dz), mipLevel).rgb;
    float3 d7 = cube.SampleLevel(sampler_cube, dir + float3(+dx, +dy, +dz), mipLevel).rgb;

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
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28f * NdV)) * r.x + r.y;
    float2 AB = float2(-1.04f, 1.04f) * a004 + r.zw;
    return specColor * AB.x + AB.y;
}

struct IBL { float3 final_color; float3 reflectivity; };
IBL ImageBasedLighting(Material material,
                        float3 normal, 
                        float3 camera_to_pixel, 
                        TextureCube cube, SamplerState sampler_cube, 
                        Texture2D lutIBL, SamplerState sampler_lutIBL)
{
    IBL ibl;

    float3 reflection = reflect(camera_to_pixel, normal);
    // From Sebastien Lagarde Moving Frostbite to PBR page 69
    reflection = GetSpecularDominantDir(normal, reflection, material.roughness);

    float NdV = saturate(dot(-camera_to_pixel, normal));
    float3 F = FresnelSchlickRoughness(NdV, material.F0, material.roughness);

    float3 kS = F;// The energy of light that gets reflected
    float3 kD = 1.0f - kS;// Remaining energy, light that gets refracted
    kD *= 1.0f - material.metallic;

    // Diffuse
    float3 irradiance = SampleEnvironment(cube, sampler_cube, normal, 8);
    float3 cDiffuse = irradiance * material.albedo;

    // Specular
    float mipLevel = max(0.001f, material.roughness * material.roughness) * 11.0f;// max lod 11.0f
    float3 prefilteredColor = SampleEnvironment(cube, sampler_cube, reflection, mipLevel);
    float2 envBRDF = lutIBL.Sample(sampler_lutIBL, float2(NdV, material.roughness)).xy;
    ibl.reflectivity = F * envBRDF.x + envBRDF.y;
    float3 cSpecular = prefilteredColor * ibl.reflectivity;

    ibl.final_color = kD * cDiffuse + cSpecular;

    return ibl;
}
// ----------------------------------------------------
// Reference for volumetric lighting:
// https://github.com/PanosK92/SpartanEngine/blob/master/Data/shaders/VolumetricLighting.hlsl

float3 DitherValve(float2 screenPos)
{
    float _dot = dot(float2(171.0, 231.0), screenPos);
    float3 dither = float3(_dot, _dot, _dot);
    dither = frac(dither / float3(103.0, 71.0, 97.0));

    return dither / 255.0;
}

// https://www.shadertoy.com/view/MslGR8
float3 Dither(float2 uv)
{
    float2 screenPos = uv * TextureSize(0, Albedo).xy;

    // bit-depth of display. Normally 8 but some LCD monitors are 7 or even 6-bit.
    float ditherBit = 8.0;

    // compute grid position
    float gridPosition = frac(dot(screenPos.xy - float2(0.5, 0.5), float2(1.0/16.0, 10.0/36.0) + 0.25));

    // compute how big the shift should be
    float ditherShift = 0.25 * (1.0 / (pow(2.0, ditherBit) - 1.0));

    // shift the individual colors differently, thus making it even harder to see the dithering pattern
    //float3 ditherShiftRGB = float3(ditherShift, -ditherShift, ditherShift); //subpixel dithering (chromatic)
    float3 ditherShiftRGB = float3(ditherShift, ditherShift, ditherShift);//non-chromatic dithering

    // modify shift acording to grid position.
    ditherShiftRGB = lerp(2.0 * ditherShiftRGB, -2.0 * ditherShiftRGB, gridPosition);//shift acording to grid position.

    // return dither shift
    return 0.5 / 255.0 + ditherShiftRGB;
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

float3 VolumetricLighting(DirectionalLight light, float3 worldPos, float2 uv, float4x4 lightViewProj, float fogFactor)
{
    float iterations = cb_effects1.z;

    float3 pixelToCamera = cb_camPos.xyz - worldPos;
    float pixelToCameraLength = length(pixelToCamera);
    float3 rayDir = pixelToCamera / pixelToCameraLength;
    float stepLength = pixelToCameraLength / iterations;
    float3 rayStep = rayDir * stepLength;
    float3 rayPos = worldPos;

    // Apply dithering as it will allows us to get away with a crazy low sample count ;-)
    float3 ditherValue = Dither(uv * TextureSize(0, Albedo).xy) * cb_effects1.w; // dithering strength 400 default
    rayPos += rayStep * ditherValue;

    // cb_effects2.w -> fog height position
    // cb_effects2.x -> fog spread
    // cb_effects3.x -> fog intensity

    float3 volumetricFactor = 0.0f;
    for (int i = 0; i < iterations; i++)
    {
        // Compute position in light space
        float4 posLight = mul(float4(rayPos, 1.0f), lightViewProj);
        posLight /= posLight.w;

        // Compute ray uv
        float2 rayUV = posLight.xy * 0.5f + 0.5f;

        // Check to see if the light can "see" the pixel		
        float depthDelta = Shadow[SHADOWMAP_CASCADES-1].SampleCmpLevelZero(sampler_Shadow, rayUV, posLight.z);
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