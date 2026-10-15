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

#if defined(VISIBILITY) && defined(PE_DX12)
struct DrawIndexedIndirectCommand
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

// Set 0 keeps this away from DepthPS's unbounded textures[] table in set 1.
[[vk::binding(2, 0)]] StructuredBuffer<DrawIndexedIndirectCommand> IndirectAll;
#endif

static const uint MATRIX_SIZE = 64u;
static const uint MESH_DATA_SIZE = MATRIX_SIZE * 2u + 16u; // +16B: NodeGpuData render-visible/flags tail

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
    const bool spriteFrameBlend = input.joints.w == SPRITE_FRAME_BLEND_MARKER;
    output.nextUv = spriteFrameBlend ? float2(asfloat(input.joints.x), asfloat(input.joints.y)) : input.uv;
    output.spriteBlend = spriteFrameBlend ? asfloat(input.joints.z) : 0.0f;

    float4x4 jointTransform = identity_mat;
    if (pc.jointsCount > 0 && !spriteFrameBlend)
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

    // precise: GBufferVS repeats this maths and depth-tests EQUAL against it.
    precise float4 positionCS = mul(float4(input.position, 1.0), final);
    output.position = ApplyViewportYConvention(positionCS);
    output.uv = input.uv;
    output.alphaFactor = constants[id].baseColorAlpha;

#ifdef VISIBILITY
    // Vulkan's VertexIndex includes the base vertex; D3D12's SV_VertexID does not.
#ifdef PE_DX12
    output.vertexId = input.vertexId + uint(IndirectAll[id].vertexOffset);
#else
    output.vertexId = input.vertexId;
#endif
    // Vertices skinned above, and animated sprites, keep the raster path until Phase 4 pre-skins them. Same test as the
    // skinning branch: static meshes cooked before boneless meshes got zero weights carry weight 1.
    const float weightSum = input.weights[0] + input.weights[1] + input.weights[2] + input.weights[3];
    output.visEligible = (spriteFrameBlend || (pc.jointsCount > 0 && weightSum > 0.0)) ? 0u : 1u;
#endif

    return output;
}
