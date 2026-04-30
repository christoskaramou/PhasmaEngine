[[vk::binding(0, 0)]] SamplerState samp : register(s0, space0);
[[vk::binding(1, 0)]] Texture2D<float4> tex : register(t1, space0);

struct PSInput
{
    float4 svPos : SV_Position;
    [[vk::location(0)]] float2 texcoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target0
{
    return tex.Sample(samp, input.texcoord);
}
