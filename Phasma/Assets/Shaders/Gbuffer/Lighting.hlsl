#ifndef LIGHT_H_
#define LIGHT_H_

#include "../Common/Common.hlsl"
#include "../Common/Structures.hlsl"
#include "PBR.hlsl"
#include "Material.hlsl"

[[vk::push_constant]] PushConstants_Lighting pc;

// Set 0
TexSamplerDecl(0, 0, Depth)
TexSamplerDecl(1, 0, Normal)
TexSamplerDecl(2, 0, Albedo)
TexSamplerDecl(3, 0, MetRough)
[[vk::binding(4, 0)]] cbuffer Lights
{
    float4 cb_camPos;
    DirectionalLight cb_sun;
    PointLight cb_pointLights[MAX_POINT_LIGHTS];
    SpotLight  cb_spotLights[MAX_SPOT_LIGHTS];
};
TexSamplerDecl(5, 0, Ssao)
TexSamplerDecl(6, 0, Emission)
[[vk::binding(7, 0)]] cbuffer LightBuffer
{
    float4x4    cb_invViewProj;
    uint        cb_ssao;
    uint        cb_ssr;
    uint        cb_tonemapping;
    uint        cb_fsr2;
    uint        cb_IBL;
    float       cb_IBL_intensity;
    uint        cb_volumetric;
    uint        cb_volumetric_steps;
    float       cb_volumetric_dither_strength;
    float       cb_lightsIntensity;
    float       cb_lightsRange;
    uint        cb_fog;
    float       cb_fogThickness;
    float       cb_fogMaxHeight;
    float       cb_fogGroundThickness;
    uint        cb_shadows;
};
TexSamplerDecl(8, 0, Transparency)
TexSamplerDecl(9, 0, LutIBL)

// Set 1
[[vk::binding(0, 1)]] cbuffer shadow_buffer {float4x4 cb_cascades[SHADOWMAP_CASCADES];};
[[vk::binding(1, 1)]] Texture2D Shadow[SHADOWMAP_CASCADES];
[[vk::binding(2, 1)]] SamplerComparisonState sampler_Shadow;

 // Set 2
 CubeSamplerDecl(0, 2, Cube)

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
    float2 f        = frac(pixelPos);
    float2 pos      = (pixelPos - f) * SHADOWMAP_TEXEL_SIZE;
    float dx        = offsetScaled;
    float dy        = offsetScaled;

    float tlTexel   = shadowTex.SampleCmpLevelZero(sampler_shadow, float2((pos + float2(-dx, +dy)) / sc.w), sc.z);
    float trTexel   = shadowTex.SampleCmpLevelZero(sampler_shadow, float2((pos + float2(+dx, +dy)) / sc.w), sc.z);
    float blTexel   = shadowTex.SampleCmpLevelZero(sampler_shadow, float2((pos + float2(-dx, -dy)) / sc.w), sc.z);
    float brTexel   = shadowTex.SampleCmpLevelZero(sampler_shadow, float2((pos + float2(+dx, -dy)) / sc.w), sc.z);

    float mixA      = lerp(tlTexel, trTexel, f.x);
    float mixB      = lerp(blTexel, brTexel, f.x);

    return lerp(mixA, mixB, 1 - f.y);
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
    float offsetScaled  = 0.75 * SHADOWMAP_TEXEL_SIZE;
    float dx            = offsetScaled;
    float dy            = offsetScaled;

    float shadowFactor  = 0.0;
    int count           = 0;
    int range           = 1;

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
    float4 coords   = mul(float4(worldPos, 1.0), mul(cb_cascades[i], biasMat));
    coords.z        += zBias;
    
    // TOTO: fix this annoying looking code snippet
    // Randomly figured how to fix a bug, possibly in the Vulkan or AMD driver (or there is something in my code that other GPU drivers are forgiving)
    // that causes the cascades to be sampled incorrectly, square like artifacts in between cascades and in the middle of the view
    if (i == 0)
        return FilterPCF(coords, Shadow[i], sampler_Shadow);
    else if (i == 1)
        return FilterPCF(coords, Shadow[i], sampler_Shadow);
    else if (i == 2)
        return FilterPCF(coords, Shadow[i], sampler_Shadow);
    else
        return FilterPCF(coords, Shadow[i], sampler_Shadow);
}

float CalculateShadows(float3 worldPos, float depth, float NdL)
{
    float shadow = 1.0;

    if (pc.cast_shadows)
    {
        float zBias = clamp(SHADOWMAP_TEXEL_SIZE * tan(acos(NdL)), 0, SHADOWMAP_TEXEL_SIZE * 2.0f); 

        for (int i = 0; i < SHADOWMAP_CASCADES; i++)
        {
            if (depth < pc.max_cascade_dist[i])
            {
                // Blend between cascades
                float dist      = pc.max_cascade_dist[i] - depth;
				float blendDist = 1.0;
                shadow          = SampleShadowMap(i, zBias, worldPos);

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
    float3 L        = lightDir;
    float3 V        = normalize(cameraPos - worldPos);
    float3 H        = normalize(V + L);
    float3 N        = materialNormal;

    float NoV       = clamp(dot(N, V), 0.001, 1.0);
    float NoL       = clamp(dot(N, L), 0.001, 1.0);
    float HoV       = clamp(dot(H, V), 0.001, 1.0);
    float LoV       = clamp(dot(L, V), 0.001, 1.0);

    float3 F0               = ComputeF0(material.albedo, material.metallic);
    float3 specularFresnel  = Fresnel(F0, HoV);
    float3 specRef          = cb_sun.color.xyz * NoL * shadow * CookTorranceSpecular(N, H, NoL, NoV, specularFresnel, roughness);
    float3 diffRef          = cb_sun.color.xyz * NoL * shadow * (1.0 - specularFresnel) * (1.0 / PI);

    float3 reflectedLight   = specRef;
    float3 diffuseLight     = diffRef * material.albedo * (1.0 - material.metallic) * occlusion;
    float3 lighting         = reflectedLight + diffuseLight;

    return lighting * cb_sun.color.a;
}

float3 ComputePointLight(int lightIndex, Material material, float3 worldPos, float3 cameraPos, float3 materialNormal, float occlusion)
{
    float3 lightDirFull = worldPos - cb_pointLights[lightIndex].position.xyz;
    float  lightDist    = max(0.1, length(lightDirFull));

    if (lightDist > cb_lightsRange) // max range
        return 0.0;

    float lightDistRatio    = lightDist / cb_lightsRange;
    float attenuation       = (1.0 - lightDistRatio * lightDistRatio);
    attenuation             = max(0.0, attenuation);  // Clamp to [0, 1]
    attenuation             *= attenuation; // Quadratic fall-off
    float3 lightDir         = normalize(-lightDirFull);
    float3 pointColor       = cb_pointLights[lightIndex].color.xyz * attenuation;
    pointColor              *= cb_lightsIntensity; // intensity

    float roughness = material.roughness * 0.75 + 0.25;

    float3 L = lightDir;
    float3 V = normalize(cameraPos - worldPos);
    float3 H = normalize(V + L);
    float3 N = materialNormal;

    float NoV = clamp(dot(N, V), 0.001, 1.0);
    float NoL = clamp(dot(N, L), 0.001, 1.0);
    float HoV = clamp(dot(H, V), 0.001, 1.0);
    float LoV = clamp(dot(L, V), 0.001, 1.0);

    float3 F0               = ComputeF0(material.albedo, material.metallic);
    float3 specularFresnel  = Fresnel(F0, HoV);
    float3 specRef          = NoL * CookTorranceSpecular(N, H, NoL, NoV, specularFresnel, roughness);
    float3 diffRef          = NoL * (1.0 - specularFresnel) * (1.0 / PI);

    float3 reflectedLight   = specRef;
    float3 diffuseLight     = diffRef * material.albedo * (1.0 - material.metallic) * occlusion;

    return pointColor * (reflectedLight + diffuseLight);
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
    float4 r        = roughness * c0 + c1;
    float a004      = min(r.x * r.x, exp2(-9.28f * NdV)) * r.x + r.y;
    float2 AB       = float2(-1.04f, 1.04f) * a004 + r.zw;

    return specColor * AB.x + AB.y;
}

struct IBL
{
    float3 final_color;
    float3 reflectivity;
};

IBL ImageBasedLighting(Material material,
                       float3 normal,
                       float3 camera_to_pixel,
                       TextureCube cube, SamplerState sampler_cube,
                       Texture2D lutIBL, SamplerState sampler_lutIBL)
{
    float3 reflection   = reflect(camera_to_pixel, normal);
    reflection          = GetSpecularDominantDir(normal, reflection, material.roughness);

    float  NdV          = saturate(dot(-camera_to_pixel, normal));
    float3 F            = FresnelSchlickRoughness(NdV, material.F0, material.roughness);
    float3 kS           = F;                                        // The energy of light that gets reflected
    float3 kD           = (1.0f - kS) * (1.0f - material.metallic); // Remaining energy, light that gets refracted

    // Diffuse
    float3 irradiance   = SampleEnvironment(cube, sampler_cube, normal, 8);
    float3 diffuse      = irradiance * material.albedo;

    // Specular
    float  mipLevel         = max(0.001f, material.roughness * material.roughness) * 8.0f; // lod 8
    float3 prefilteredColor = SampleEnvironment(cube, sampler_cube, reflection, mipLevel);
    float2 envBRDF          = lutIBL.Sample(sampler_lutIBL, float2(NdV, material.roughness)).xy;
    float3 reflectivity     = F * envBRDF.x + envBRDF.y;
    float3 specular         = prefilteredColor * reflectivity;

    IBL ibl;
    ibl.final_color         = kD * diffuse + specular;
    ibl.reflectivity        = reflectivity;

    return ibl;
}

float3 DitherValve(float2 screenPos)
{
    float _dot      = dot(float2(171.0, 231.0), screenPos);
    float3 dither   = float3(_dot, _dot, _dot);
    dither          = frac(dither / float3(103.0, 71.0, 97.0));

    return dither / 255.0;
}

// https://www.shadertoy.com/view/MslGR8
float3 Dither(float2 uv)
{
    // bit-depth of display. Normally 8 but some LCD monitors are 7 or even 6-bit.
    float ditherBit = 8.0;

    // compute grid position
    float gridPosition = frac(dot(uv - float2(0.5, 0.5), float2(1.0/16.0, 10.0/36.0) + 0.25));

    // compute how big the shift should be
    float ditherShift = 0.25 * (1.0 / (pow(2.0, ditherBit) - 1.0));

    // shift the individual colors differently, thus making it even harder to see the dithering pattern
    //float3 ditherShiftRGB = float3(ditherShift, -ditherShift, ditherShift); //subpixel dithering (chromatic)
    float3 ditherShiftRGB = float3(ditherShift, ditherShift, ditherShift); //non-chromatic dithering

    // modify shift acording to grid position.
    ditherShiftRGB = lerp(2.0 * ditherShiftRGB, -2.0 * ditherShiftRGB, gridPosition); //shift acording to grid position.

    // return dither shift
    return 0.5 / 255.0 + ditherShiftRGB;
}

// Mie scattering approximated with Henyey-Greenstein phase function.
float ComputeScattering(float dirDotL)
{
    float scat = 0.95f;
    float e = abs(1.0f + scat * scat - (2.0f * scat) * dirDotL);

    return (1.0f - scat * scat) / pow(e, 0.1f);
}

float3 VolumetricLighting(DirectionalLight light, float3 worldPos, float2 uv, float4x4 lightViewProj, float fogFactor, float2 size)
{
    float  iterations           = cb_volumetric_steps;
    float3 pixelToCamera        = cb_camPos.xyz - worldPos;
    float  pixelToCameraLength  = length(pixelToCamera);
    float3 rayDir               = pixelToCamera / pixelToCameraLength;
    float  stepLength           = pixelToCameraLength / iterations;
    float3 rayStep              = rayDir * stepLength;
    float3 rayPos               = worldPos;

    // Apply dithering as it will allows us to get away with a crazy low sample count ;-)
    float3 ditherValue  = Dither(uv * size) * cb_volumetric_dither_strength; // dithering strength 400 default
    rayPos              += rayStep * ditherValue;

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
