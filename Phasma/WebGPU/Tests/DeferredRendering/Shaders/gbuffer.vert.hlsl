struct Uniforms
{
    column_major float4x4 modelMatrix;
    column_major float4x4 normalModelMatrix;
};
struct Camera
{
    column_major float4x4 viewProjectionMatrix;
    column_major float4x4 invViewProjectionMatrix;
};

[[vk::binding(0, 0)]] cbuffer UCB : register(b0, space0) { Uniforms uni; }
[[vk::binding(1, 0)]] cbuffer CCB : register(b1, space0) { Camera camera; }

struct VSInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal   : NORMAL;
    [[vk::location(2)]] float2 uv       : TEXCOORD;
};

struct VSOutput
{
    float4 svPos                      : SV_Position;
    [[vk::location(0)]] float3 fragNormal : TEXCOORD0;
    [[vk::location(1)]] float2 fragUV     : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    float3 worldPos = mul(uni.modelMatrix, float4(input.position, 1.0f)).xyz;
    o.svPos         = mul(camera.viewProjectionMatrix, float4(worldPos, 1.0f));
    o.fragNormal    = normalize(mul(uni.normalModelMatrix, float4(input.normal, 1.0f)).xyz);
    o.fragUV        = input.uv;
    return o;
}
