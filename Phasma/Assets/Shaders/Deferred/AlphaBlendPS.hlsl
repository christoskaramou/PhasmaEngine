#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"
#include "Light.hlsl"

float4 SampleArray(float2 uv, float index)
{
    return textures[index].Sample(material_sampler, uv);
}

float4 GetBaseColor(float2 uv)          { return SampleArray(uv, pc.primitiveImageIndex + 0); }
float4 GetMetallicRoughness(float2 uv)  { return SampleArray(uv, pc.primitiveImageIndex + 1); }
float4 GetNormal(float2 uv)             { return SampleArray(uv, pc.primitiveImageIndex + 2); }
float4 GetOcclusion(float2 uv)          { return SampleArray(uv, pc.primitiveImageIndex + 3); }
float4 GetEmissive(float2 uv)           { return SampleArray(uv, pc.primitiveImageIndex + 4); }

PS_OUTPUT_Color mainPS(PS_INPUT_Gbuffer input)
{
    PS_OUTPUT_Color output;

    float2 uv = input.uv;
    float4 basicColor = GetBaseColor(uv) + input.color;
    if (basicColor.a < pc.alphaCut)
        discard;

    float3 metRough         = float3(0.0f, GetMetallicRoughness(uv).yz);
    float4 emissive         = float4(GetEmissive(uv).xyz * input.emissiveFactor, 0.0f);
    float  ao               = GetOcclusion(uv).r;
    float3 tangentNormal    = GetNormal(uv).xyz;
    float3 wolrdPos         = input.positionWS.xyz;
    float3 normal           = CalculateNormal(input.positionWS.xyz, tangentNormal, input.normal, uv) * 0.5 + 0.5;
    float4 albedo           = float4(basicColor.xyz * ao, basicColor.a) * input.baseColorFactor;

    Material material;
    material.albedo     = albedo.xyz;
    material.roughness  = metRough.y;
    material.metallic   = metRough.z;
    material.F0         = lerp(0.04f, material.albedo, material.metallic);

    // Ambient
    float occlusion     = cb_effects0.x > 0.5 ? SsaoBlur.Sample(sampler_SsaoBlur, uv).x : 1.0;
    float skyLight      = clamp(cb_sun.color.a, 0.025f, 1.0f);
    skyLight            *= cb_effects3.z > 0.5 ? 0.25 : 0.15;
    float ambientLight  = skyLight * occlusion;
    float3 fragColor    = 0.0;

    // cb_effects3.z -> shadow cast
    if (cb_effects3.z > 0.5)
    {
        float shadow    = CalculateShadows(wolrdPos, length(wolrdPos - cb_camPos.xyz), dot(normal, cb_sun.direction.xyz));
        fragColor       += DirectLight(material, wolrdPos, cb_camPos.xyz, normal, occlusion, shadow);
    }

    // IBL
    IBL ibl;
    ibl.reflectivity = 0.5;
    if (cb_effects1.x > 0.5) {
        ibl         = ImageBasedLighting(material, normal, normalize(wolrdPos - cb_camPos.xyz), Cube, sampler_Cube, LutIBL, sampler_LutIBL);
        fragColor   += ibl.final_color * ambientLight;
    }

    for (int i = 0; i < MAX_POINT_LIGHTS; ++i)
        fragColor += ComputePointLight(i, material, wolrdPos, cb_camPos.xyz, normal, occlusion);

    output.color = float4(fragColor, albedo.a) + emissive;

    // // SSR
    // if (cb_effects0.y > 0.5)
    // {
    //     float3 ssr      = Ssr.Sample(sampler_Ssr, input.uv).xyz * ibl.reflectivity;
    //     output.color    += float4(ssr, 0.0);// * (1.0 - material.roughness);
    // }

    // Tone Mapping
    if (cb_effects0.z > 0.5)
        output.color.xyz = Reinhard(output.color.xyz);

    // Fog
    if (cb_effects3.y > 0.5)
    {
        // cb_effects3.y -> fog on/off
        // cb_effects2.w -> fog max height
        // cb_effects2.x -> fog global thickness
        // cb_effects3.x -> fog ground thickness

        float fogNear           = 0.5;
        float fogFar            = 1000.0;
        float depth             = length(wolrdPos - cb_camPos.xyz);
        float groundHeightMin   = -2.0;
        float groundHeightMax   = cb_effects2.w;
        float globalThickness   = cb_effects2.x;
        float groundThickness   = cb_effects3.x;
        float depthFactor       = clamp(1.0 - (fogFar - depth) / (fogFar - fogNear), 0.0, 1.0);
        float heightFactor      = clamp(1.0 - (wolrdPos.y - groundHeightMin) / (groundHeightMax - groundHeightMin), 0.0, 1.0);
        float globalFactor      = depthFactor * globalThickness;
        float noiseFactor       = 1.0;
        float groundFactor      = heightFactor * depthFactor * noiseFactor * groundThickness;
        float fogFactor         = clamp(groundFactor + globalFactor, 0.0, 1.0);
        output.color.xyz        = lerp(output.color.xyz, 1.0, fogFactor);

        // Volumetric light
        if (cb_effects1.y > 0.5)
            output.color.xyz += VolumetricLighting(cb_sun, wolrdPos, input.uv, cb_cascades[0], fogFactor, pc.frameBufferSize);
    }

    return output;
}
