struct Uniforms
{
    column_major float4x4 worldViewProjection;
    column_major float4x4 worldInverseTranspose;
    float4   color;
};

[[vk::binding(0, 0)]] cbuffer UCB : register(b0, space0) { Uniforms uni; }

struct VSInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal   : NORMAL;
};

struct VSOutput
{
    float4 svPos                       : SV_Position;
    [[vk::location(0)]] float3 vNormal : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.svPos = mul(uni.worldViewProjection, float4(input.position, 1.0f));
    o.vNormal = mul(uni.worldInverseTranspose, float4(input.normal, 0.0f)).xyz;
    return o;
}
