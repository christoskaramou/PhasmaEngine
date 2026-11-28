#include "../Common/Structures.hlsl"

[[vk::push_constant]] PushConstants_Additive pc;
[[vk::binding(0, 0)]] ByteAddressBuffer data;

struct VS_INPUT_Additive
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float4 color : COLOR;
    uint4 joints : BLENDINDICES;
    float4 weights : BLENDWEIGHT;
};

struct VS_OUTPUT_Additive
{
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float4 position : SV_POSITION;
};

float4x4 LoadMatrix(uint offset)
{
    float4x4 result;
    result[0] = asfloat(data.Load4(offset + 0 * 16));
    result[1] = asfloat(data.Load4(offset + 1 * 16));
    result[2] = asfloat(data.Load4(offset + 2 * 16));
    result[3] = asfloat(data.Load4(offset + 3 * 16));
    return result;
}

VS_OUTPUT_Additive mainVS(VS_INPUT_Additive input)
{
    VS_OUTPUT_Additive output;
    output.position = ApplyViewportYConvention(
        mul(float4(input.position, 1.0f), mul(LoadMatrix(pc.meshDataOffset), LoadMatrix(0))));
    output.uv = input.uv;
    output.color = input.color;
    return output;
}
