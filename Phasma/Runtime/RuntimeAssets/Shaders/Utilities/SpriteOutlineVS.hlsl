#include "../Common/Structures.hlsl"

struct PushConstants_SpriteOutline
{
    float4 color;
    uint meshDataOffset;
};

[[vk::push_constant]] PushConstants_SpriteOutline pc;

[[vk::binding(0)]] ByteAddressBuffer data;

float4x4 LoadMatrix(uint offset)
{
    float4x4 result;
    result[0] = asfloat(data.Load4(offset + 0 * 16));
    result[1] = asfloat(data.Load4(offset + 1 * 16));
    result[2] = asfloat(data.Load4(offset + 2 * 16));
    result[3] = asfloat(data.Load4(offset + 3 * 16));
    return result;
}

VS_OUTPUT_AABB mainVS(VS_INPUT_Depth input)
{
    VS_OUTPUT_AABB output;
    output.position = ApplyViewportYConvention(
        mul(float4(input.position, 1.0f), mul(LoadMatrix(pc.meshDataOffset), LoadMatrix(0))));
    output.color = float4(pc.color.rgb, pc.color.a * input.uv.x);
    return output;
}
