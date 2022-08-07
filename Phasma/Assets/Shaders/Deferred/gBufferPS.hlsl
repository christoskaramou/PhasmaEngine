#include "../Common/common.hlsl"

struct PushConstants { 
    float2 projJitter;
    float2 prevProjJitter;
    uint modelIndex;
    uint meshIndex;
    uint meshJointCount;
    uint primitiveIndex;
    uint primitiveImageIndex;
};
[[vk::push_constant]] PushConstants pc;

[[vk::binding(0, 1)]] SamplerState material_sampler;
[[vk::binding(1, 1)]] Texture2D textures[];

#define index_BaseColor           pc.primitiveImageIndex + 0
#define index_MetallicRoughness   pc.primitiveImageIndex + 1
#define index_Normal              pc.primitiveImageIndex + 2
#define index_Occlusion           pc.primitiveImageIndex + 3
#define index_Emissive            pc.primitiveImageIndex + 4

struct PS_INPUT {
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL;
    float4 color : COLOR;
    float4 baseColorFactor : TEXCOORD1;
    float3 emissiveFactor : TEXCOORD2;
    float4 metRoughAlphacutOcl : TEXCOORD3;
    float4 positionCS  : POSITION0;
    float4 previousPositionCS : POSITION1;
    float4 positionWS : POSITION2;
};

struct PS_OUTPUT {
    float3 normal : SV_TARGET0;
    float4 albedo : SV_TARGET1;
    float3 metRough : SV_TARGET2;
    float2 velocity : SV_TARGET3;
    float4 emissive : SV_TARGET4;
};

float4 SampleArray(float2 uv, float index)
{
    return textures[index].Sample(material_sampler, uv);
}

PS_OUTPUT mainPS(PS_INPUT input)
{
    PS_OUTPUT output;

    float4 basicColor = SampleArray(input.uv, index_BaseColor) + input.color;
    if (basicColor.a < input.metRoughAlphacutOcl.z) discard; // needed because alpha blending is messed up when objects are not in order
    float3 metRough = SampleArray(input.uv, index_MetallicRoughness).xyz;
    float3 emissive = SampleArray(input.uv, index_Emissive).xyz;
    float ao = SampleArray(input.uv, index_Occlusion).r;
    float3 tangentNormal = SampleArray(input.uv, index_Normal).xyz;

    output.normal = GetNormal(input.positionWS.xyz, tangentNormal, input.normal, input.uv) * 0.5 + 0.5;
    output.albedo = float4(basicColor.xyz * ao, basicColor.a) * input.baseColorFactor;
    output.metRough = float3(0.0, metRough.y, metRough.z);
    float2 cancelJitter = pc.prevProjJitter - pc.projJitter;
    output.velocity = (input.previousPositionCS.xy / input.previousPositionCS.w - 
                        input.positionCS.xy / input.positionCS.w) - cancelJitter;
    output.velocity *= float2(0.5f, 0.5f);
    output.emissive = float4(emissive * input.emissiveFactor, 0.0);

    return output;
}
