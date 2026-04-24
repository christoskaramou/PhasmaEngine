struct VSOutput
{
    float4 position : SV_Position;
    float3 texcoord : TEXCOORD0;
    nointerpolation uint instanceIdx : INSTANCE_IDX;
};

static const uint kNumLayers = 10u;

[[vk::binding(1, 0)]] SamplerState g_sampler : register(s0);
[[vk::binding(2, 0)]] Texture2DArray<float4> g_tex : register(t0);

float4 PSMain(VSOutput i) : SV_Target0
{
    float layer = (float)(i.instanceIdx % kNumLayers);
    return g_tex.Sample(g_sampler, float3(i.texcoord.xy, layer));
}
