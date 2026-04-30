struct VSOutput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    nointerpolation uint baseArrayLayer : BASE_LAYER;
};

[[vk::binding(0, 0)]] SamplerState g_sampler : register(s0);
[[vk::binding(1, 0)]] TextureCube<float4> g_tex : register(t0);

// HLSL's float3x3 constructor is row-major-by-argument-order but WGSL's mat3x3f
// constructor is column-major. The upstream faceMat values (generateMipmap.wgsl)
// are written column-by-column; transposed here so that mul(M, v) reproduces
// upstream's M * v semantics exactly.
static const float3x3 kFaceMat[6] = {
    float3x3( 0,  0,  1,   0, -2,  1,  -2,  0,  1), // +x
    float3x3( 0,  0, -1,   0, -2,  1,   2,  0, -1), // -x
    float3x3( 2,  0, -1,   0,  0,  1,   0,  2, -1), // +y
    float3x3( 2,  0, -1,   0,  0, -1,   0, -2,  1), // -y
    float3x3( 2,  0, -1,   0, -2,  1,   0,  0,  1), // +z
    float3x3(-2,  0,  1,   0, -2,  1,   0,  0, -1), // -z
};

float4 PSMain(VSOutput i) : SV_Target0
{
    float2 uv = frac(i.texcoord);
    float3 dir = mul(kFaceMat[i.baseArrayLayer], float3(uv, 1.0));
    return g_tex.Sample(g_sampler, dir);
}
