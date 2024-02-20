#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

[[vk::push_constant]] PushConstants_GBuffer pc;

[[vk::binding(0, 1)]] SamplerState material_sampler;
[[vk::binding(1, 1)]] Texture2D textures[];

float4 SampleArray(float2 uv, float index)
{
    return textures[index].Sample(material_sampler, uv);
}

float4 GetBaseColor(float2 uv)          { return SampleArray(uv, pc.primitiveImageIndex + 0); }
float4 GetMetallicRoughness(float2 uv)  { return SampleArray(uv, pc.primitiveImageIndex + 1); }
float4 GetNormal(float2 uv)             { return SampleArray(uv, pc.primitiveImageIndex + 2); }
float4 GetOcclusion(float2 uv)          { return SampleArray(uv, pc.primitiveImageIndex + 3); }
float4 GetEmissive(float2 uv)           { return SampleArray(uv, pc.primitiveImageIndex + 4); }

PS_OUTPUT_Gbuffer mainPS(PS_INPUT_Gbuffer input)
{
    PS_OUTPUT_Gbuffer output;
    
    float2 uv = input.uv;
    float4 basicColor = GetBaseColor(uv) * input.color;
    if (basicColor.a < pc.alphaCut)
            discard;

    float3 metRough         = GetMetallicRoughness(uv).xyz;
    float3 emissive         = GetEmissive(uv).xyz;
    float3 tangentNormal    = GetNormal(uv).xyz;
    float ao                = GetOcclusion(uv).r;

    float2 cancelJitter     = pc.prevProjJitter - pc.projJitter;
    output.normal           = CalculateNormal(input.positionWS.xyz, tangentNormal, input.normal, input.uv) * 0.5 + 0.5;
    output.albedo           = float4(basicColor.xyz * ao, basicColor.a) * input.baseColorFactor;
    output.metRough         = float3(0.0, metRough.y, metRough.z);
    output.velocity         = (input.prevPositionCS.xy / input.prevPositionCS.w - input.positionCS.xy / input.positionCS.w) - cancelJitter;
    output.velocity         *= float2(0.5f, 0.5f);
    output.emissive         = float4(emissive * input.emissiveFactor, 0.0);

    return output;
}
