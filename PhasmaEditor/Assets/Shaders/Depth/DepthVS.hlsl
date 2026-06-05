#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

// --- ByteAddressBuffer data ---
// PerFrameData                 -> 4 * sizeof(mat4)
// Constant Buffer indices      -> num of draw calls * sizeof(uint)
// for (num of draw calls)
//      NodeGpuData            -> sizeof(mat4) * 2  [worldMatrix, prevWorldMatrix]
// --- ByteAddressBuffer data ---

[[vk::push_constant]] PushConstants_DepthPass pc;
[[vk::binding(0, 0)]] ByteAddressBuffer data;
[[vk::binding(1, 0)]] StructuredBuffer<Mesh_Constants> constants;

static const uint MATRIX_SIZE = 64u;
static const uint MESH_DATA_SIZE = MATRIX_SIZE * 2u;

float4x4 LoadMatrix(uint offset)
{
    float4x4 result;

    result[0] = asfloat(data.Load4(offset + 0 * 16));
    result[1] = asfloat(data.Load4(offset + 1 * 16));
    result[2] = asfloat(data.Load4(offset + 2 * 16));
    result[3] = asfloat(data.Load4(offset + 3 * 16));

    return result;
}

// Matrices
float4x4 GetViewProjection()                  { return LoadMatrix(0); }
float4x4 GetMeshMatrix(uint id)               { return LoadMatrix(constants[id].meshDataOffset); }
float4x4 GetJointMatrix(uint id, uint index)  { return LoadMatrix(constants[id].meshDataOffset + MESH_DATA_SIZE + index * MATRIX_SIZE); }

VS_OUTPUT_Position_Uv_ID mainVS(VS_INPUT_Depth input)
{
    VS_OUTPUT_Position_Uv_ID output;

    const uint id = input.id;
    output.id = id;
    
    float4x4 jointTransform = identity_mat;
    if (pc.jointsCount > 0)
    {
        float weightSum = input.weights[0] + input.weights[1] + input.weights[2] + input.weights[3];
        // Non-skinned nodes allocate no joint tail; zero weights keep them from loading past NodeGpuData.
        if (weightSum > 0.0)
        {
            jointTransform = mul(GetJointMatrix(id, input.joints[0]), input.weights[0]) +
                             mul(GetJointMatrix(id, input.joints[1]), input.weights[1]) +
                             mul(GetJointMatrix(id, input.joints[2]), input.weights[2]) +
                             mul(GetJointMatrix(id, input.joints[3]), input.weights[3]);
        }
    }

    float4x4 combinedMatrix = mul(jointTransform, GetMeshMatrix(id));
    float4x4 final = mul(combinedMatrix, GetViewProjection());

    output.position = mul(float4(input.position, 1.0), final);
    output.uv = input.uv;
    output.alphaFactor = constants[id].baseColorAlpha;

    return output;
}
