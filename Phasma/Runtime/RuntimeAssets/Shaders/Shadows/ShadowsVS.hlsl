#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"
#ifdef PE_GENERATED_MASK
#include "../Common/DeformedGeometry.hlsl"
#endif

[[vk::push_constant]] PushConstants_Shadows pc;
[[vk::binding(0, 0)]] ByteAddressBuffer data;
[[vk::binding(1, 0)]] StructuredBuffer<Mesh_Constants> constants;

#ifdef PE_MASK_FALLBACK
[[vk::binding(3, 0)]] ByteAddressBuffer pe_deformed;
#ifdef PE_DX12
struct PeShadowDraw
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};
[[vk::binding(2, 0)]] StructuredBuffer<PeShadowDraw> IndirectAll;
#endif

bool PeShadowMaskEligible(VS_INPUT_Depth input)
{
    if (input.joints.w == SPRITE_FRAME_BLEND_MARKER)
        return true;

    if ((constants[input.id].editorFlags & PE_EDITOR_FLAG_DEFORMED) != 0u)
    {
        uint vertexId = input.vertexId;
#ifdef PE_DX12
        vertexId += uint(IndirectAll[input.id].vertexOffset);
#endif
        PeDeformedVertex deformed;
        if (PeLoadDeformedVertex(pe_deformed, input.id, vertexId, deformed))
            return true;
    }

    const float weightSum = input.weights[0] + input.weights[1] + input.weights[2] + input.weights[3];
    return !(pc.jointsCount > 0u && weightSum > 0.0);
}

void mainLegacyPS(VS_OUTPUT_Position_Uv input)
{
    const uint e0 = GetAttributeAtVertex(input.maskEligible, 0);
    const uint e1 = GetAttributeAtVertex(input.maskEligible, 1);
    const uint e2 = GetAttributeAtVertex(input.maskEligible, 2);
    if ((e0 & e1 & e2) != 0u)
        discard;
}
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
float4x4 GetMeshMatrix(uint id)               { return LoadMatrix(constants[id].meshDataOffset); }
float4x4 GetJointMatrix(uint id, uint index)  { return LoadMatrix(constants[id].meshDataOffset + MESH_DATA_SIZE + index * MATRIX_SIZE); }


VS_OUTPUT_Position_Uv mainVS(VS_INPUT_Depth input)
{
    VS_OUTPUT_Position_Uv output = (VS_OUTPUT_Position_Uv)0;

    const uint id = input.id;

#ifdef PE_GENERATED_MASK
    const bool generatedMask = PeUsesGeneratedMask(constants[id]);
#ifdef PE_MASK_FALLBACK
    if (!generatedMask)
#else
    if (generatedMask)
#endif
    {
        output.position = float4(0.0, 0.0, -1.0, 1.0);
        return output;
    }
#ifdef PE_MASK_FALLBACK
    output.maskEligible = PeShadowMaskEligible(input) ? 1u : 0u;
    // Keep UV live: reflection derives the packed depth-stream stride from vertex inputs.
    output.uv = input.uv;
#endif
#endif
    
    float4x4 jointTransform = identity_mat;
    bool jointSkinning = pc.jointsCount > 0;
#ifdef PE_MASK_FALLBACK
    jointSkinning = jointSkinning && input.joints.w != SPRITE_FRAME_BLEND_MARKER;
#endif
    if (jointSkinning)
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
    float4x4 final = mul(combinedMatrix, pc.vp);

    output.position = ApplyViewportYConvention(mul(float4(input.position, 1.0), final));

    return output;
}
