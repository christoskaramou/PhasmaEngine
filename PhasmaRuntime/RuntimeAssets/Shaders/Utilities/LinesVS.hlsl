#include "../Common/Structures.hlsl"

// Push constants kept local: float4 color carries HDR emissive factors that the
// packed-uint AABB color cannot.
struct PushConstants_Lines
{
    float4 color;
    uint meshIndex;
};

[[vk::push_constant]] PushConstants_Lines pc;

[[vk::binding(0)]] ByteAddressBuffer data;

static const uint MATRIX_SIZE = 64u;

float4x4 LoadMatrix(uint matrixIndex)
{
    uint offset = matrixIndex * MATRIX_SIZE;
    float4x4 result;
    result[0] = asfloat(data.Load4(offset + 0 * 16));
    result[1] = asfloat(data.Load4(offset + 1 * 16));
    result[2] = asfloat(data.Load4(offset + 2 * 16));
    result[3] = asfloat(data.Load4(offset + 3 * 16));
    return result;
}

float4x4 GetViewProjection() { return LoadMatrix(0); }
float4x4 GetMeshMatrix()     { return LoadMatrix(pc.meshIndex); }

// VS_INPUT_Depth matches the positions stream layout (position+uv+joints+weights);
// only position is used.
VS_OUTPUT_AABB mainVS(VS_INPUT_Depth input)
{
    VS_OUTPUT_AABB output;
    output.position = mul(float4(input.position, 1.0f), mul(GetMeshMatrix(), GetViewProjection()));
    output.color    = pc.color;
    return output;
}
