#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

static const int MAX_DATA_SIZE = 2048; // TODO: calculate on init
static const uint MATRIX_SIZE = 64u;

[[vk::push_constant]] PushConstants_AABB pc;

[[vk::binding(0)]] ByteAddressBuffer data;

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

VS_OUTPUT_AABB mainVS(VS_INPUT_Position input)
{
    VS_OUTPUT_AABB output;
    output.position = mul(float4(input.position, 1.0f), mul(GetMeshMatrix(), GetViewProjection()));
    output.color    = UnpackColorRGBA(pc.color);
    return output;
}
