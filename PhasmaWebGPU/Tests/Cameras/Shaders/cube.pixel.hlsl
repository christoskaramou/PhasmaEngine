[[vk::binding(1, 0)]] SamplerState mySampler : register(s1, space0);
[[vk::binding(2, 0)]] Texture2D<float4> myTexture : register(t2, space0);

struct PSInput
{
    float4 svPos                         : SV_Position;
    [[vk::location(0)]] float2 fragUV    : TEXCOORD0;
    [[vk::location(1)]] float4 fragPos   : TEXCOORD1;
};

float4 PSMain(PSInput input) : SV_Target0
{
    return myTexture.Sample(mySampler, input.fragUV) * input.fragPos;
}
