#ifndef PBR_H_
#define PBR_H_

#ifndef PI
#define PI 3.1415628
#endif

float DGGX(float roughness, float3 N, float3 H)
{
    float NoH = clamp(dot(N, H), 0.0001, 1.0);
    float m = roughness * roughness;
    float m2 = m * m;
    float d = (NoH * m2 - NoH) * NoH + 1.0;
    return m2 / (PI * d * d);
}

float GSchlick(float roughness, float NoV, float NoL)
{
    float r = roughness + 1.0;
    float k = r * r * (1.0 / 8.0);
    float V = NoV * (1.0 - k) + k;
    float L = NoL * (1.0 - k) + k;
    return 0.25 / max(V * L, 0.001);// 1 / (4 * NoV * NoL) is folded in here.
}

float V_SmithGGXCorrelated(float NoV, float NoL, float roughness)
{
    float a2 = roughness * roughness;
    float GGXV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float GGXL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / (GGXV + GGXL);
}

float3 DisneyDiffuse(float3 albedo, float NoV, float NoL, float LoH, float roughness)
{
    float energyBias = lerp(0.0, 0.5, roughness);
    float energyFactor = lerp(1.0, 1.0 / 1.51, roughness);
    float fd90 = energyBias + 2.0 * LoH * LoH * roughness;
    float lightScatter = 1.0 + (fd90 - 1.0) * pow(1.0 - NoL, 5.0);
    float viewScatter = 1.0 + (fd90 - 1.0) * pow(1.0 - NoV, 5.0);
    
    return albedo * lightScatter * viewScatter * energyFactor / PI;
}

float3 CookTorranceSpecular_Disney(float3 N, float3 H, float NoL, float NoV, float3 specular, float roughness)
{
    float D = DGGX(roughness, N, H);
    float V = V_SmithGGXCorrelated(NoV, NoL, roughness); // V includes 1/(4*NoV*NoL)
    return specular * V * D;
}

float3 CookTorranceSpecular_GSchlick(float3 N, float3 H, float NoL, float NoV, float3 specular, float roughness)
{
    float D = DGGX(roughness, N, H);
    float G = GSchlick(roughness, NoV, NoL);
    return specular * G * D;
}

float3 CookTorranceSpecular(float3 N, float3 H, float NoL, float NoV, float3 specular, float roughness)
{
    return CookTorranceSpecular_Disney(N, H, NoL, NoV, specular, roughness);
}

float3 Fresnel(float3 F0, float HoV)
{
    return lerp(F0, 1.0, pow((1.0 - HoV), 5.0));
}

float3 FresnelIBL(float3 F0, float cosTheta, float roughness)
{
    return F0 + (max(1.0 - roughness, F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

float3 ComputeF0(float3 base_color, float metallic)
{
    return lerp(0.04, base_color, metallic);
}

float GetSpecularOcclusion(float NoV, float roughness, float ao)
{
    return saturate(pow(NoV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
}

#endif
