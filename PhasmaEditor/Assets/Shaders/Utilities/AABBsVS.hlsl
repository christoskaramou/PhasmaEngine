#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

static const int MAX_DATA_SIZE = 2048; // TODO: calculate on init

[[vk::push_constant]] PushConstants_AABB pc;

[[vk::binding(0)]] tbuffer UBO { float4x4 data[MAX_DATA_SIZE]; };

float4x4 GetViewProjection() { return data[0]; }
float4x4 GetMeshMatrix()     { return data[pc.meshIndex]; }

VS_OUTPUT_AABB mainVS(VS_INPUT_Position input)
{
    VS_OUTPUT_AABB output;
    output.position = mul(float4(input.position, 1.0f), mul(GetMeshMatrix(), GetViewProjection()));
    output.color    = UnpackColorRGBA(pc.color).rgb;
    return output;
}
