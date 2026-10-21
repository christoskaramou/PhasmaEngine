#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"
#ifdef PE_DEFORMED_VERTEX
#include "../Common/DeformedGeometry.hlsl"
[[vk::binding(3, 0)]] ByteAddressBuffer pe_deformed;
#ifdef PE_DX12
struct PeSkinDraw
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};
[[vk::binding(2, 0)]] StructuredBuffer<PeSkinDraw> pe_skinDraws;
#endif
#endif

// --- ByteAddressBuffer data ---
// PerFrameData                 -> 4 * sizeof(mat4)
// Constant Buffer indices      -> num of draw calls * sizeof(uint)
// for (num of draw calls)
//      NodeGpuData            -> sizeof(mat4) * 2  [worldMatrix, prevWorldMatrix]
// --- ByteAddressBuffer data ---

#ifdef PE_OPENPBR_FORWARD
// OpenPBRForwardPass: material pass 2's lighting push constants and set-0 layout (MaterialForwardHead.slang). Own names:
// the DX12 backend reads [[vk::binding]] by resource name from the unpreprocessed source, where both branches appear.
[[vk::push_constant]] PushConstants_Lighting pc;
[[vk::binding(16, 0)]] ByteAddressBuffer pe_frameData;
[[vk::binding(17, 0)]] StructuredBuffer<Mesh_Constants> pe_constants;
#define data pe_frameData
#define constants pe_constants
#else
[[vk::push_constant]] PushConstants_GBuffer pc;
[[vk::binding(0, 0)]] ByteAddressBuffer data;
[[vk::binding(1, 0)]] StructuredBuffer<Mesh_Constants> constants;
#endif

static const uint MATRIX_SIZE = 64u;
static const uint MESH_DATA_SIZE = MATRIX_SIZE * 2u + 16u; // +16B: NodeGpuData render-visible/flags tail
static const uint PE_EDITOR_FLAG_MATERIALX_FORWARD = 128u; // Scene::ComputeMeshConstants: MeshUsesOpenPBRForward

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
float4x4 GetPreviousViewProjection()          { return LoadMatrix(64); }
float4x4 GetMeshMatrix(uint id)               { return LoadMatrix(constants[id].meshDataOffset); }
float4x4 GetMeshPreviousMatrix(uint id)       { return LoadMatrix(constants[id].meshDataOffset + MATRIX_SIZE); }
float4x4 GetJointMatrix(uint id, uint index)  { return LoadMatrix(constants[id].meshDataOffset + MESH_DATA_SIZE + index * MATRIX_SIZE); }
// NodeGpuData.jointCount (byte 136): the previous frame's pose follows the current one in the joint tail.
uint GetJointCount(uint id)                   { return data.Load(constants[id].meshDataOffset + 136u); }
float4x4 GetPreviousJointMatrix(uint id, uint index)
{
    return LoadMatrix(constants[id].meshDataOffset + MESH_DATA_SIZE + (GetJointCount(id) + index) * MATRIX_SIZE);
}

VS_OUTPUT_Gbuffer mainVS(VS_INPUT_Gbuffer input)
{
    VS_OUTPUT_Gbuffer output;
    output.uv = input.texCoord.xy;
    const bool spriteFrameBlend = input.joints.w == SPRITE_FRAME_BLEND_MARKER;
    output.nextUv = spriteFrameBlend ? float2(asfloat(input.joints.x), asfloat(input.joints.y)) : input.texCoord.xy;
    output.spriteBlend = spriteFrameBlend ? asfloat(input.joints.z) : 0.0f;

    const uint id = input.id;
    output.id = id;

#ifdef PE_OPENPBR_FORWARD
    output.uvVertex = input.texCoord.xy;
#elif defined(GBUFFER_TRANSPARENT)
    // While OpenPBR is effective, the forward pass shades MaterialX alpha-blend and transmission meshes instead.
    if (pc.openpbrForward != 0u && (constants[id].editorFlags & PE_EDITOR_FLAG_MATERIALX_FORWARD) != 0u)
    {
        output = (VS_OUTPUT_Gbuffer)0;
        output.position = float4(0.0, 0.0, -1.0, 1.0);
        return output;
    }
#endif

#ifdef PE_DEFORMED_VERTEX
    const float weightSum = input.weights[0] + input.weights[1] + input.weights[2] + input.weights[3];
    output.maskEligible = (!spriteFrameBlend && pc.jointsCount > 0 && weightSum > 0.0) ? 0u : 1u;
    if (!spriteFrameBlend && (constants[id].editorFlags & PE_EDITOR_FLAG_DEFORMED) != 0u)
    {
        uint sourceVertex = input.vertexId;
#ifdef PE_DX12
        sourceVertex += uint(pe_skinDraws[id].vertexOffset);
#endif
        PeDeformedVertex vertex;
        if (PeLoadDeformedVertex(pe_deformed, id, sourceVertex, vertex))
        {
            output.positionWS = float4(vertex.position, 1.0);
            output.positionCS = vertex.clip;
            output.prevPositionCS = vertex.previousClip;
            output.position = ApplyViewportYConvention(vertex.clip);
            output.normal = vertex.normal;
            output.tangent = vertex.tangent;
            output.color = input.color;
            output.maskEligible = 1u;
            return output;
        }
    }
#endif

    float4x4 boneTransform = identity_mat;
    float4x4 prevBoneTransform = identity_mat;
    if (pc.jointsCount && !spriteFrameBlend)
    {
        float weightSum = input.weights[0] + input.weights[1] + input.weights[2] + input.weights[3];
        // Non-skinned nodes allocate no joint tail; zero weights keep them from loading past NodeGpuData.
        if (weightSum > 0.0)
        {
            boneTransform = mul(GetJointMatrix(id, input.joints[0]), input.weights[0]) +
                            mul(GetJointMatrix(id, input.joints[1]), input.weights[1]) +
                            mul(GetJointMatrix(id, input.joints[2]), input.weights[2]) +
                            mul(GetJointMatrix(id, input.joints[3]), input.weights[3]);
            // Last frame's pose, so an animated limb's motion reaches the velocity buffer (TAA ghosted it).
            prevBoneTransform = mul(GetPreviousJointMatrix(id, input.joints[0]), input.weights[0]) +
                                mul(GetPreviousJointMatrix(id, input.joints[1]), input.weights[1]) +
                                mul(GetPreviousJointMatrix(id, input.joints[2]), input.weights[2]) +
                                mul(GetPreviousJointMatrix(id, input.joints[3]), input.weights[3]);
        }
    }

    // World space and clip space positions
    float4 inPos            = float4(input.position, 1.0f);
    float4x4 worldTransform = mul(boneTransform, GetMeshMatrix(id));
    output.positionWS       = mul(inPos, worldTransform);
    // DepthVS's exact maths, precise: this pass depth-tests EQUAL against the depth prepass, and a skinned mesh
    // multiplied in another order (J*(M*VP)) rounded apart from it and failed the test in speckles on DX12.
    precise float4 positionCS = mul(inPos, mul(worldTransform, GetViewProjection()));
    output.positionCS       = positionCS;
    output.prevPositionCS   = mul(inPos, mul(prevBoneTransform, mul(GetMeshPreviousMatrix(id), GetPreviousViewProjection())));
    output.position         = ApplyViewportYConvention(output.positionCS);
    
    // Normal
    float3x3 worldRotationScale3x3  = (float3x3)worldTransform;                       // remove translation
    float3x3 worldRotation3X3       = remove_scale3x3(worldRotationScale3x3);         // remove scale
    output.normal                   = normalize(mul(input.normal, worldRotation3X3)); // transform normal to world space
    output.tangent.xyz              = normalize(mul(input.tangent.xyz, worldRotation3X3)); // transform tangent to world space
    output.tangent.w                = input.tangent.w; // pass sign

    // Color
    output.color = input.color;

    return output;
}
