// Port of webgpu-samples/alpha-to-coverage-emulator scenes/FoliageCommon.wgsl
// (fragment side) to HLSL. The shader returns alpha for native A2C edge
// feathering; the vertex shader also constrains the leaf footprint to a disk so
// transparent quad corners cannot leak as triangles if A2C is unreliable.
//
// For the full set of emulator algorithms (Native, AlphaTest, Apple M1 Pro,
// NVIDIA GeForce RTX 3070, AMD Radeon RX 580 / 890M, Intel HD 4400, ARM
// Mali-G78, Imagination PowerVR Rogue GE8300, Qualcomm Adreno 630) see
// emulators.inc.hlsl in this directory.

struct UniformsData
{
    column_major float4x4 uViewProj;
    float4                uFeatheringWidthPxPad;
};

[[vk::binding(0, 0)]] cbuffer UniformsCB : register(b0, space0)
{
    UniformsData uniforms;
};

struct Varying
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
    float2 gb  : TEXCOORD1;
};

// WGSL `uvToAlpha` equivalent. Produces a feathered signed distance field
// across the quad, matching upstream.
float UvToAlpha(float2 uv)
{
    const float kRadius = 1.0f;
    float t = kRadius - length(uv);
    float2 grad = float2(ddx(t), ddy(t));
    float divisor = length(grad) * uniforms.uFeatheringWidthPxPad.x;
    return clamp(t / max(divisor, 0.0001f), 0.0f, 1.0f);
}

float4 ComputeFragment(Varying vary)
{
    float alpha = UvToAlpha(vary.uv);
    return float4(0.0f, vary.gb, alpha);
}

float4 PSMain(Varying vary) : SV_Target0
{
    return ComputeFragment(vary);
}
