#ifndef LIGHT_H_
#define LIGHT_H_

#include "../Common/Common.hlsl"
#include "../Common/Structures.hlsl"
#include "../Common/IBL.hlsl"
#include "PBR.hlsl"
#include "Material.hlsl"

[[vk::push_constant]] PushConstants_Lighting pc;

// Set 0
TexSamplerDecl(0, 0, Depth)
TexSamplerDecl(1, 0, Normal)
TexSamplerDecl(2, 0, Albedo)
TexSamplerDecl(3, 0, MetRough)
[[vk::binding(4, 0)]] cbuffer LightSystemUBO
{

    uint cb_numDirectionalLights;
    uint cb_numPointLights;
    uint cb_numSpotLights;
    uint cb_numAreaLights;
    uint cb_offsetDirectionalLights;
    uint cb_offsetPointLights;
    uint cb_offsetSpotLights;
    uint cb_offsetAreaLights;
};
TexSamplerDecl(5, 0, Ssao)
TexSamplerDecl(6, 0, Emission)
[[vk::binding(7, 0)]] cbuffer PassUBO
{
    float4x4    cb_invViewProj;
    float4      cb_camPos;
    uint        cb_ssao;
    uint        cb_ssr;
    uint        cb_tonemapping;
    uint        cb_fsr2;
    uint        cb_IBL;
    float       cb_IBL_intensity;
    float       cb_lightsIntensity;
    uint        cb_shadows;
    uint        cb_use_Disney_PBR;
};
TexSamplerDecl(8, 0, Transparency)
TexSamplerDecl(9, 0, LutIBL)
[[vk::binding(10, 0)]] ByteAddressBuffer LightsBuffer;

// Set 1
[[vk::binding(0, 1)]] cbuffer shadow_buffer {float4x4 cb_cascades[SHADOWMAP_CASCADES];};
[[vk::binding(1, 1)]] Texture2D Shadow[SHADOWMAP_CASCADES];
[[vk::binding(2, 1)]] SamplerComparisonState sampler_Shadow;

 // Set 2
 CubeSamplerDecl(0, 2, Cube)

// Helper functions for loading lights
DirectionalLight LoadDirectionalLight(uint index)
{
    uint offset = cb_offsetDirectionalLights + index * 32;
    DirectionalLight light;
    light.color = asfloat(LightsBuffer.Load4(offset));
    light.direction = asfloat(LightsBuffer.Load4(offset + 16));
    return light;
}

PointLight LoadPointLight(uint index)
{
    uint offset = cb_offsetPointLights + index * 32;
    PointLight light;
    light.color = asfloat(LightsBuffer.Load4(offset));
    light.position = asfloat(LightsBuffer.Load4(offset + 16));
    return light;
}

SpotLight LoadSpotLight(uint index)
{
    uint offset = cb_offsetSpotLights + index * 48;
    SpotLight light;
    light.color = asfloat(LightsBuffer.Load4(offset));
    light.position = asfloat(LightsBuffer.Load4(offset + 16));
    light.rotation = asfloat(LightsBuffer.Load4(offset + 32));
    return light;
}

AreaLight LoadAreaLight(uint index)
{
    uint offset = cb_offsetAreaLights + index * 64;
    AreaLight light;
    light.color = asfloat(LightsBuffer.Load4(offset));
    light.position = asfloat(LightsBuffer.Load4(offset + 16));
    light.rotation = asfloat(LightsBuffer.Load4(offset + 32));
    light.size = asfloat(LightsBuffer.Load4(offset + 48));
    return light;
}

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

    if (cb_shadows)
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

float3 DirectLight(DirectionalLight light, Material material, float3 worldPos, float3 cameraPos, float3 materialNormal, float occlusion, float shadow, float3 energyCompensation)
{
    float roughness = max(material.roughness, 0.04);

    // Compute directional light.
    float3 lightDir = light.direction.xyz;
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
    
    // Specular
    float3 specRef;
    if (cb_use_Disney_PBR)
        specRef = light.color.xyz * NoL * shadow * CookTorranceSpecular_Disney(N, H, NoL, NoV, specularFresnel, roughness);
    else
        specRef = light.color.xyz * NoL * shadow * CookTorranceSpecular_GSchlick(N, H, NoL, NoV, specularFresnel, roughness);
    
    specRef *= energyCompensation;
    
    // Diffuse
    float3 diffRef;
    if (cb_use_Disney_PBR)
    {
        float LoH = clamp(dot(L, H), 0.001, 1.0);
        float3 disneyDiffuse = DisneyDiffuse(material.albedo, NoV, NoL, LoH, roughness);
        diffRef = light.color.xyz * NoL * shadow * (1.0 - specularFresnel) * disneyDiffuse;
    }
    else
    {
        diffRef = light.color.xyz * NoL * shadow * (1.0 - specularFresnel) * (material.albedo / PI);
    }

    float3 reflectedLight   = specRef;
    float3 diffuseLight     = diffRef * (1.0 - material.metallic) * occlusion;
    float3 lighting         = reflectedLight + diffuseLight;

    return lighting * light.color.a;
}

float3 ComputePointLight(int lightIndex, Material material, float3 worldPos, float3 cameraPos, float3 materialNormal, float occlusion, float3 energyCompensation)
{
    PointLight light = LoadPointLight(lightIndex);
    float3 lightDirFull = worldPos - light.position.xyz; // position is .xyz
    float  range        = light.position.w; // radius is .w
    float  lightDist    = max(0.1, length(lightDirFull));

    if (lightDist > range) // max range
        return 0.0;

    float lightDistRatio    = lightDist / range;
    float attenuation       = (1.0 - lightDistRatio * lightDistRatio);
    attenuation             = max(0.0, attenuation);  // Clamp to [0, 1]
    attenuation             *= attenuation; // Quadratic fall-off
    float3 lightDir         = normalize(-lightDirFull);
    float3 pointColor       = light.color.xyz * attenuation; // color is .xyz
    pointColor              *= light.color.w * cb_lightsIntensity; // intensity is .w

    float roughness = max(material.roughness, 0.04);

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
    
    // Specular
    float3 specRef;
    if (cb_use_Disney_PBR)
    {
        specRef = NoL * CookTorranceSpecular_Disney(N, H, NoL, NoV, specularFresnel, roughness);
    }
    else
        specRef = NoL * CookTorranceSpecular_GSchlick(N, H, NoL, NoV, specularFresnel, roughness);
    
    specRef *= energyCompensation;
    
    // Diffuse
    float3 diffRef;
    if (cb_use_Disney_PBR)
    {
        float LoH = clamp(dot(L, H), 0.001, 1.0);
        float3 disneyDiffuse = DisneyDiffuse(material.albedo, NoV, NoL, LoH, roughness);
        diffRef = NoL * (1.0 - specularFresnel) * disneyDiffuse;
    }
    else
    {
        diffRef = NoL * (1.0 - specularFresnel) * (material.albedo / PI);
    }
    
    float3 reflectedLight   = specRef;
    float3 diffuseLight     = diffRef * (1.0 - material.metallic) * occlusion;

    return pointColor * (reflectedLight + diffuseLight);
}

float3 ComputeSpotLight(int lightIndex, Material material, float3 worldPos, float3 cameraPos, float3 materialNormal, float occlusion, float3 energyCompensation)
{
    SpotLight light = LoadSpotLight(lightIndex);
    float3 lightPos = light.position.xyz; // .xyz
    float3 lightDirFull = worldPos - lightPos;
    float  range        = light.position.w; // range is .w
    float  lightDist    = max(0.1, length(lightDirFull));

    if (lightDist > range)
        return 0.0;

    // Compute spot direction from Rotation (Pitch, Yaw)
    float p = radians(light.rotation.x);
    float y = radians(light.rotation.y);
    
    // Convert to Direction Vector
    float3 spotDir;
    spotDir.x = cos(p) * sin(y);
    spotDir.y = sin(p);
    spotDir.z = cos(p) * cos(y);
    spotDir = normalize(spotDir);

    float3 L = normalize(-lightDirFull);
    
    // Check if the pixel is within the cone
    // L points from Pixel to Light. spotDir points from Light out.
    // Ideally we aligned -L (Light to Pixel) with spotDir.
    float theta = dot(-L, spotDir);

    // Cone attenuation and Falloff
    float cutoffCos = cos(radians(light.rotation.z)); // Angle is .z
    float outerCutoffCos = cos(radians(light.rotation.z + light.rotation.w)); // Falloff is .w
    
    if (theta < outerCutoffCos) // Check against outer cutoff
        return 0.0;
    
    // Use smoothstep for smooth transition between outer (0.0) and inner (1.0).
    float spotIntensity = smoothstep(outerCutoffCos, cutoffCos, theta);

    // Distance attenuation
    float lightDistRatio    = lightDist / range;
    float attenuation       = (1.0 - lightDistRatio * lightDistRatio);
    attenuation             = max(0.0, attenuation);
    attenuation             *= attenuation;

    float3 spotColor = light.color.xyz * attenuation * spotIntensity;
    spotColor       *= light.color.w * cb_lightsIntensity; // intensity is .w

    float roughness = max(material.roughness, 0.04);
    float3 V = normalize(cameraPos - worldPos);
    float3 H = normalize(V + L);
    float3 N = materialNormal;
    float NoV = clamp(dot(N, V), 0.001, 1.0);
    float NoL = clamp(dot(N, L), 0.001, 1.0);
    float HoV = clamp(dot(H, V), 0.001, 1.0);

    float3 F0               = ComputeF0(material.albedo, material.metallic);
    float3 specularFresnel  = Fresnel(F0, HoV);
    
    // Specular
    float3 specRef;
    if (cb_use_Disney_PBR)
        specRef = NoL * CookTorranceSpecular_Disney(N, H, NoL, NoV, specularFresnel, roughness);
    else
        specRef = NoL * CookTorranceSpecular_GSchlick(N, H, NoL, NoV, specularFresnel, roughness);
    specRef *= energyCompensation;
    
    // Diffuse
    float3 diffRef;
    if (cb_use_Disney_PBR)
    {
        float LoH = clamp(dot(L, H), 0.001, 1.0);
        float3 disneyDiffuse = DisneyDiffuse(material.albedo, NoV, NoL, LoH, roughness);
        diffRef = NoL * (1.0 - specularFresnel) * disneyDiffuse;
    }
    else
    {
        diffRef = NoL * (1.0 - specularFresnel) * (material.albedo / PI);
    }

    float3 reflectedLight   = specRef;
    float3 diffuseLight     = diffRef * (1.0 - material.metallic) * occlusion;

    return spotColor * (reflectedLight + diffuseLight);
}

float3 ComputeAreaLight(int lightIndex, Material material, float3 worldPos, float3 cameraPos, float3 materialNormal, float occlusion, float3 energyCompensation)
{
    AreaLight light = LoadAreaLight(lightIndex);
    float3 lightPos = light.position.xyz;
    float  range    = light.position.w;
    float width     = light.size.x;
    float height    = light.size.y;

    // 1. Calculate orientation
    float p = radians(light.rotation.x);
    float y = radians(light.rotation.y);
    
    // Light Vectors
    float3 lightForward;
    lightForward.x = cos(p) * sin(y);
    lightForward.y = sin(p);
    lightForward.z = cos(p) * cos(y);
    lightForward = normalize(lightForward);

    float3 lightRight = normalize(cross(lightForward, float3(0, 1, 0)));
    float3 lightUp    = normalize(cross(lightRight, lightForward));

    // 2. Find closest point on area light to WorldPos for Attenuation/Range
    float3 toLight = worldPos - lightPos;
    float distPlane = dot(toLight, lightForward);
    float3 pointOnPlane = worldPos - lightForward * distPlane;
    float3 localPoint = pointOnPlane - lightPos;

    float u = dot(localPoint, lightRight);
    float v = dot(localPoint, lightUp);

    u = clamp(u, -width * 0.5, width * 0.5);
    v = clamp(v, -height * 0.5, height * 0.5);

    float3 closestPoint = lightPos + lightRight * u + lightUp * v;
    float3 L_closest_vec = closestPoint - worldPos;
    float closestDist = length(L_closest_vec);

    if (closestDist > range)
        return 0.0;

    // 3. Specular Representative Point (Karis)
    float3 V = normalize(cameraPos - worldPos);
    float3 N = materialNormal;
    float3 R = reflect(-V, N);

    // Intersect reflection ray with plane
    float denom = dot(R, lightForward);
    float3 targetPoint = closestPoint; // Default to closest point

    if (abs(denom) > 0.001)
    {
        float t = dot(lightPos - worldPos, lightForward) / denom;
        if (t > 0)
        {
            float3 intersectP = worldPos + t * R;
            float3 localP = intersectP - lightPos;
            float u_spec = dot(localP, lightRight);
            float v_spec = dot(localP, lightUp);
            
            u_spec = clamp(u_spec, -width * 0.5, width * 0.5);
            v_spec = clamp(v_spec, -height * 0.5, height * 0.5);
            
            targetPoint = lightPos + lightRight * u_spec + lightUp * v_spec;
        }
    }

    // Use targetPoint for lighting vector L
    float3 L_vec = targetPoint - worldPos;
    float distToTarget = length(L_vec);
    float3 L = normalize(L_vec);
    
    // Attenuation
    // Use closestDist for range falloff to maintain shape
    float lightDistRatio = closestDist / range;
    float attenuation = (1.0 - lightDistRatio * lightDistRatio);
    attenuation = max(0.0, attenuation);
    attenuation *= attenuation;
    
    // Use distToTarget for inverse square to keep intensity correct at distance
    attenuation /= (distToTarget * distToTarget + 1.0);

    float lightOutputCos = dot(-L, lightForward);
    if (lightOutputCos <= 0.0) return 0.0;

    float3 areaColor = light.color.xyz * light.color.w * cb_lightsIntensity * attenuation * lightOutputCos;

    // Standard PBR ...
    float roughness = max(material.roughness, 0.04);
    float3 H = normalize(V + L);
    
    float NoV = clamp(dot(N, V), 0.001, 1.0);
    float NoL = clamp(dot(N, L), 0.001, 1.0);
    float HoV = clamp(dot(H, V), 0.001, 1.0);

    float3 F0               = ComputeF0(material.albedo, material.metallic);
    float3 specularFresnel  = Fresnel(F0, HoV);
    
    float3 specRef;
    if (cb_use_Disney_PBR)
        specRef = NoL * CookTorranceSpecular_Disney(N, H, NoL, NoV, specularFresnel, roughness);
    else
        specRef = NoL * CookTorranceSpecular_GSchlick(N, H, NoL, NoV, specularFresnel, roughness);
    specRef *= energyCompensation;
    
    float3 diffRef;
    if (cb_use_Disney_PBR)
    {
        float LoH = clamp(dot(L, H), 0.001, 1.0);
        float3 disneyDiffuse = DisneyDiffuse(material.albedo, NoV, NoL, LoH, roughness);
        diffRef = NoL * (1.0 - specularFresnel) * disneyDiffuse;
    }
    else
    {
        diffRef = NoL * (1.0 - specularFresnel) * (material.albedo / PI);
    }
    
    float3 reflectedLight   = specRef;
    float3 diffuseLight     = diffRef * (1.0 - material.metallic) * occlusion;

    return areaColor * (reflectedLight + diffuseLight);
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

float3 ImageBasedLighting(Material material,
                        float3 normal,
                        float3 V,
                        TextureCube cube, SamplerState sampler_cube,
                        float2 envBRDF,
                        float occlusion)
{
    return ComputeIBL_Common(
        normal, V, material.albedo, material.metallic, material.roughness, material.F0, occlusion,
        cube, sampler_cube, envBRDF
    );
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

#endif
