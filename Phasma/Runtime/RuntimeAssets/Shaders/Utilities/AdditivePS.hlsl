#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

[[vk::push_constant]] PushConstants_Additive pc;
[[vk::binding(1, 0)]] SamplerState material_sampler : register(s1, space0);

#ifdef PE_WEB_PROFILE
[[vk::binding(0, 1)]] Texture2D pe_additive_base_color : register(t0, space1);

float4 SampleAdditiveBaseColor(float2 uv, uint index)
{
    if (index == 0xFFFFFFFFu)
        return float4(1.0f, 1.0f, 1.0f, 1.0f);

    return pe_additive_base_color.Sample(material_sampler, uv);
}
#else
[[vk::binding(0, 1)]] Texture2D pe_textures[] : register(t0, space100);

float4 SampleAdditiveBaseColor(float2 uv, uint index)
{
    if (index == 0xFFFFFFFFu)
        return float4(1.0f, 1.0f, 1.0f, 1.0f);

    return pe_textures[NonUniformResourceIndex(index)].Sample(material_sampler, uv);
}
#endif

struct PS_INPUT_Additive
{
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

PS_OUTPUT_Color mainPS(PS_INPUT_Additive input)
{
    float4 sampled = SampleAdditiveBaseColor(input.uv, pc.baseColorTexIdx);

    float4 authored = input.color * pc.baseColorFactor;
    if (pc.linearColor != 0u)
        authored.rgb = SrgbToLinear(input.color.rgb) * SrgbToLinear(pc.baseColorFactor.rgb);

    float4 base = sampled * authored;
    float3 emissive = (pc.linearColor != 0u)
                          ? SrgbToLinearHdr(pc.emissiveFactor.rgb)
                          : pc.emissiveFactor.rgb;
    float3 rgb = base.rgb + emissive * sampled.a;
    float outAlpha = base.a;
    if (outAlpha <= 0.001f)
        discard;

    PS_OUTPUT_Color output;
    output.color = float4(rgb, outAlpha);
    return output;
}
