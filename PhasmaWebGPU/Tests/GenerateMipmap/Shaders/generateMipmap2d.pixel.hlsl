struct VSOutput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    nointerpolation uint baseArrayLayer : BASE_LAYER;
};

[[vk::binding(0, 0)]] SamplerState g_sampler : register(s0);
[[vk::binding(1, 0)]] Texture2D<float4> g_tex : register(t0);

float4 PSMain(VSOutput i) : SV_Target0
{
    return g_tex.Sample(g_sampler, i.texcoord);
}
