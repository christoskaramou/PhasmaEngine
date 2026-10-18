#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"
#include "../Common/MaterialFlags.hlsl"

[[vk::binding(0, 1)]] StructuredBuffer<Mesh_Constants> constants : register(t0, space1);
[[vk::binding(1, 1)]] SamplerState material_sampler : register(s1, space1);
[[vk::binding(0, 2)]] Texture2D pe_textures[] : register(t0, space100);

float4 SampleArray(float2 uv, uint index)
{
    if (index == 0xFFFFFFFF)
        return float4(1.0, 1.0, 1.0, 1.0);
    return pe_textures[NonUniformResourceIndex(index)].Sample(material_sampler, uv);
}

float4 GetBaseColor(uint id, float2 uv)
{
    return SampleArray(uv, constants[id].meshImageIndex[0]);
}

void mainPS(PS_INPUT_Position_Uv_ID input)
{
    const uint id = input.id;
    float4 sampledBase = HasTexture(constants[id].textureMask, TEX_BASE_COLOR_BIT) ? GetBaseColor(id, input.uv) : float4(1.0f, 1.0f, 1.0f, 1.0f);
    const float spriteBlend = saturate(input.spriteBlend);
    if (spriteBlend > 0.0f && HasTexture(constants[id].textureMask, TEX_BASE_COLOR_BIT))
        sampledBase = lerp(sampledBase, GetBaseColor(id, input.nextUv), spriteBlend);
    float alpha = sampledBase.a * input.alphaFactor;
    if (alpha < constants[id].alphaCut)
        discard;
}
