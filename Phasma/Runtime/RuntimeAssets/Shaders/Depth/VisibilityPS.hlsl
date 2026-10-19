#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"
#include "../Common/MaterialFlags.hlsl"
#include "../Common/DeformedGeometry.hlsl"

[[vk::binding(0, 1)]] StructuredBuffer<Mesh_Constants> constants : register(t0, space1);
[[vk::binding(1, 1)]] SamplerState material_sampler : register(s1, space1);
[[vk::binding(0, 2)]] Texture2D pe_textures[] : register(t0, space100);

float4 SampleArray(float2 uv, uint index)
{
    if (index == 0xFFFFFFFF)
        return float4(1.0, 1.0, 1.0, 1.0);
    return pe_textures[NonUniformResourceIndex(index)].Sample(material_sampler, uv);
}

uint4 mainPS(PS_INPUT_Position_Uv_ID input) : SV_TARGET0
{
    const uint id = input.id;
    const uint e0 = GetAttributeAtVertex(input.visEligible, 0);
    const uint e1 = GetAttributeAtVertex(input.visEligible, 1);
    const uint e2 = GetAttributeAtVertex(input.visEligible, 2);
    if (PeUsesGeneratedMask(constants[id]) && (e0 & e1 & e2) != 0u)
        discard;

    float4 sampledBase = HasTexture(constants[id].textureMask, TEX_BASE_COLOR_BIT)
                             ? SampleArray(input.uv, constants[id].meshImageIndex[0])
                             : float4(1.0f, 1.0f, 1.0f, 1.0f);

    const float spriteBlend = saturate(input.spriteBlend);
    if (spriteBlend > 0.0f && HasTexture(constants[id].textureMask, TEX_BASE_COLOR_BIT))
        sampledBase = lerp(sampledBase, SampleArray(input.nextUv, constants[id].meshImageIndex[0]), spriteBlend);

    // Same coverage as GBufferPS and the generated masks: zero alpha never covers, even at cutoff 0.
    const float alpha = sampledBase.a * input.alphaFactor;
    if (alpha <= 0.0f || alpha < constants[id].alphaCut)
        discard;

    if ((e0 & e1 & e2) == 0u)
        return uint4(0u, 0u, 0u, 0u);

    return uint4(id + 1u,
                 GetAttributeAtVertex(input.vertexId, 0),
                 GetAttributeAtVertex(input.vertexId, 1),
                 GetAttributeAtVertex(input.vertexId, 2));
}
