// RhiShaders (WebGPU RHI spec decision 7): one binding of each texture and sampler kind the sidecar describes.
struct Push
{
    float4 tint;
};

struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// The engine's TexSamplerDecl and CubeSamplerDecl pairs: texture at 2b, its sampler at 2b + 1.
[[vk::combinedImageSampler]] [[vk::binding(0, 0)]] Texture2D Albedo;
[[vk::combinedImageSampler]] [[vk::binding(0, 0)]] SamplerState sampler_Albedo;
[[vk::combinedImageSampler]] [[vk::binding(1, 0)]] TextureCube Sky;
[[vk::combinedImageSampler]] [[vk::binding(1, 0)]] SamplerState sampler_Sky;
[[vk::binding(2, 0)]] Texture2DArray Layers;     // only loaded: unfilterable-float, 2d-array
[[vk::binding(3, 0)]] Texture2D<uint> Ids;       // uint
[[vk::binding(4, 0)]] Texture2D ShadowMap;       // compared: depth
[[vk::binding(5, 0)]] SamplerComparisonState ShadowSampler;
[[vk::binding(6, 0)]] Texture2DMS<float4> Msaa;  // multisampled
[[vk::binding(0, 1)]] StructuredBuffer<float4> Positions;
[[vk::push_constant]] Push pc;

float4 mainPS(VSOut input) : SV_Target0
{
    float4 color = Albedo.Sample(sampler_Albedo, input.uv);
    color += Sky.Sample(sampler_Sky, float3(input.uv, 1.0));
    color += Layers.Load(int4(0, 0, 1, 0));
    color.r += float(Ids.Load(int3(0, 0, 0)) & 1u);
    color.g += ShadowMap.SampleCmpLevelZero(ShadowSampler, input.uv, 0.5);
    color += Msaa.Load(int2(0, 0), 0);
    color.b += Positions[0].x;
    return color * pc.tint;
}
