struct VSOutput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    nointerpolation uint baseArrayLayer : BASE_LAYER;
};

[[vk::binding(0, 0)]] SamplerState g_sampler : register(s0);
[[vk::binding(1, 0)]] TextureCubeArray<float4> g_tex : register(t0);

// Transposed from upstream WGSL (column-major) to HLSL (row-major constructor).
static const float3x3 kFaceMat[6] = {
    float3x3( 0,  0,  1,   0, -2,  1,  -2,  0,  1),
    float3x3( 0,  0, -1,   0, -2,  1,   2,  0, -1),
    float3x3( 2,  0, -1,   0,  0,  1,   0,  2, -1),
    float3x3( 2,  0, -1,   0,  0, -1,   0, -2,  1),
    float3x3( 2,  0, -1,   0, -2,  1,   0,  0,  1),
    float3x3(-2,  0,  1,   0, -2,  1,   0,  0, -1),
};

float4 PSMain(VSOutput i) : SV_Target0
{
    uint face = i.baseArrayLayer % 6u;
    uint cubeLayer = i.baseArrayLayer / 6u;
    float2 uv = frac(i.texcoord);
    float3 dir = mul(kFaceMat[face], float3(uv, 1.0));
    return g_tex.Sample(g_sampler, float4(dir, (float)cubeLayer));
}
