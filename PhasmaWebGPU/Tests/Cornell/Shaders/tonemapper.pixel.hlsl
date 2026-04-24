// Tonemapper pixel shader — ports webgpu-samples/cornell/tonemapper.wgsl.
// Reinhard + gamma, constants match upstream (TonemapExposure=0.5, Gamma=2.2).
// We moved to a fragment pipeline so the surface does not need StorageBinding usage.

[[vk::binding(0, 0)]] Texture2D<float4> inputTex : register(t0, space0);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

static const float TonemapExposure = 0.5;
static const float Gamma = 2.2;

float3 reinhard_tonemap(float3 linearColor)
{
    float3 color = linearColor * TonemapExposure;
    float3 mapped = color / (1.0 + color);
    return pow(mapped, float3(1.0 / Gamma, 1.0 / Gamma, 1.0 / Gamma));
}

float4 PSMain(VSOut i) : SV_Target0
{
    // Point load at the exact pixel — matches upstream textureLoad(input, id.xy, 0).
    int2 coord = int2(i.pos.xy);
    float3 color = inputTex.Load(int3(coord, 0)).rgb;
    float3 mapped = reinhard_tonemap(color);
    return float4(mapped, 1.0);
}
