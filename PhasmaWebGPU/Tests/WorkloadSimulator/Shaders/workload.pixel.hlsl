[[vk::binding(1, 0)]]
SamplerState uSampler : register(s1, space0);

[[vk::binding(2, 0)]]
Texture2D<float4> uTexture : register(t2, space0);

struct PSInput
{
    float4 svPos              : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target0
{
    return uTexture.Sample(uSampler, input.uv);
}
