// RhiShaders (WebGPU RHI spec decision 7): buffers, a storage texture and push constants through the real bake.
// Self-contained, so the cache key depends on this file only, on desktop and in the browser.
struct Params
{
    float scale;
    float bias;
    uint count;
    uint pad;
};

// No vector after a scalar here: HLSL packs a uint3 at offset 4, which WGSL cannot express (vec3 aligns to 16).
struct Push
{
    float offset;
};

[[vk::binding(0, 0)]] StructuredBuffer<float> Input;
[[vk::binding(1, 0)]] RWStructuredBuffer<float> Output;
[[vk::binding(2, 0)]] ConstantBuffer<Params> params;
[[vk::image_format("rgba8")]] [[vk::binding(3, 0)]] RWTexture2D<float4> Target;
[[vk::push_constant]] Push pc;

[numthreads(64, 1, 1)]
void mainCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= params.count)
        return;

    const float value = Input[id.x] * params.scale + params.bias + pc.offset;
    Output[id.x] = value;
    if (id.x < 4)
        Target[uint2(id.x, 0)] = float4(value / 255.0, 0.0, 0.0, 1.0);
}
