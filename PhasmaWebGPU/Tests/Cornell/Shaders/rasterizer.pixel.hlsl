// Rasterizer pixel shader — ports webgpu-samples/cornell/rasterizer.wgsl (fragment half).

[[vk::binding(0, 1)]] Texture2DArray<float4> lightmap : register(t0, space1);
[[vk::binding(1, 1)]] SamplerState smpl : register(s0, space1);

struct VertexOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float3 emissive : TEXCOORD1;
    nointerpolation uint quad : TEXCOORD2;
};

float4 PSMain(VertexOut i) : SV_Target0
{
    float4 lm = lightmap.Sample(smpl, float3(i.uv, (float)i.quad));
    return lm + float4(i.emissive, 1.0);
}
