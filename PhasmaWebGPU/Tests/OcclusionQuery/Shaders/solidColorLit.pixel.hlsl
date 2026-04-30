struct Uniforms
{
    column_major float4x4 worldViewProjection;
    column_major float4x4 worldInverseTranspose;
    float4   color;
};

[[vk::binding(0, 0)]] cbuffer UCB : register(b0, space0) { Uniforms uni; }

struct PSInput
{
    float4 svPos                       : SV_Position;
    [[vk::location(0)]] float3 vNormal : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target0
{
    float3 L = normalize(float3(4.0f, 10.0f, 6.0f));
    float light = dot(normalize(input.vNormal), L) * 0.5f + 0.5f;
    return float4(uni.color.rgb * light, uni.color.a);
}
