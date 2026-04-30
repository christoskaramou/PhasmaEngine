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
    float4 texColor = myTexture.Sample(mySampler, input.fragUV * 0.8f + float2(0.1f, 0.1f));
    float f = (length(texColor.rgb - float3(0.5f, 0.5f, 0.5f)) < 0.01f) ? 0.0f : 1.0f;
    return f * texColor + (1.0f - f) * input.fragPos;
}
