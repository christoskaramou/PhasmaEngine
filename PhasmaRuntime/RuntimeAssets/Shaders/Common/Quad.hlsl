#include "Structures.hlsl"

VS_OUTPUT_Position_Uv mainVS(uint vertexID : SV_VertexID)
{
    VS_OUTPUT_Position_Uv output;
    output.uv = float2((vertexID << 1) & 2, vertexID & 2);
    float2 position = output.uv * 2.0f - 1.0f;
    output.position = float4(position, 0.0f, 1.0f);
    return output;
}
