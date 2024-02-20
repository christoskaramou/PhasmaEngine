#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

static const int MAX_DATA_SIZE = 2048;

[[vk::push_constant]] PushConstants_Shadows pc;
[[vk::binding(0, 0)]] tbuffer UBO { float4x4 data[MAX_DATA_SIZE]; };

float4x4 GetMeshMatrix()            { return data[pc.meshIndex]; }
float4x4 GetJointMatrix(uint index) { return data[pc.meshIndex + 2 + index]; }

VS_OUTPUT_Position_Uv mainVS(VS_INPUT_Depth input)
{
    VS_OUTPUT_Position_Uv output;
    
    float4x4 jointTransform = identity_mat;
    if (pc.meshJointCount > 0)
    {
        jointTransform = mul(GetJointMatrix(input.joints[0]), input.weights[0]) +
                         mul(GetJointMatrix(input.joints[1]), input.weights[1]) +
                         mul(GetJointMatrix(input.joints[2]), input.weights[2]) +
                         mul(GetJointMatrix(input.joints[3]), input.weights[3]);
    }

    float4x4 combinedMatrix = mul(jointTransform, GetMeshMatrix());
    float4x4 final = mul(combinedMatrix, pc.vp);

    output.position = mul(float4(input.position, 1.0), final);
    //output.uv = input.uv; // TODO: UVs are not used in shadow pass, remove this when we have shadow vertices in the buffer

    return output;
}
