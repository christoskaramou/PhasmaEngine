// Rasterizer vertex shader — ports webgpu-samples/cornell/rasterizer.wgsl (vertex half).

#include "common.inc.hlsl"

struct VertexIn
{
    float4 position : POSITION0;
    float3 uv : TEXCOORD0;       // xy=uv, z=quad index
    float3 emissive : TEXCOORD1;
};

struct VertexOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float3 emissive : TEXCOORD1;
    nointerpolation uint quad : TEXCOORD2;
};

VertexOut VSMain(VertexIn input)
{
    VertexOut o;
    o.position = mul(common_uniforms.mvp, input.position);
    o.uv = input.uv.xy;
    o.quad = (uint)(input.uv.z + 0.5);
    o.emissive = input.emissive;
    return o;
}
