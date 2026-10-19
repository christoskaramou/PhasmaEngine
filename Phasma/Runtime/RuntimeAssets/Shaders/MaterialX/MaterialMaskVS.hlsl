#define VISIBILITY 1
#define PE_OPENPBR_MASK 1
#include "../Depth/DepthVS.hlsl"
#include "VisibilityCommon.slang"

[[vk::binding(4, 0)]] ByteAddressBuffer pe_geometry;
[[vk::binding(5, 0)]] ByteAddressBuffer pe_materialBytes;

void PeMaskAttributes(VS_INPUT_Depth input, uint vertexId, inout VS_OUTPUT_OpenPBRMask output)
{
    const uint id = input.id;
    const PeVertex source = PeLoadVertex(pe_geometry, pc.verticesOffset, vertexId);
    const float4x4 world = GetMeshMatrix(id);
    const float3x3 rotation = remove_scale3x3((float3x3)world);
    // Keep POSITION and UV live: reflected attributes define the packed depth-vertex layout.
    output.worldPosition = mul(float4(input.position, 1.0), world).xyz;
    output.normal = normalize(mul(source.normal, rotation));
    output.tangent = float4(PeSafeNormalize(mul(source.tangent.xyz, rotation)), source.tangent.w);
    output.color = source.color;
    output.uv = input.uv;
    output.nextUv = source.nextUv;
    output.spriteBlend = source.spriteBlend;

    PeDeformedVertex deformed;
    if ((constants[id].editorFlags & PE_EDITOR_FLAG_DEFORMED) != 0u &&
        PeLoadDeformedVertex(pe_deformed, id, vertexId, deformed))
    {
        output.worldPosition = deformed.position;
        output.normal = deformed.normal;
        output.tangent = deformed.tangent;
    }
}

VS_OUTPUT_OpenPBRMask mainVS(VS_INPUT_Depth input)
{
    VS_OUTPUT_OpenPBRMask output = (VS_OUTPUT_OpenPBRMask)0;
    output.position = float4(0.0, 0.0, -1.0, 1.0);
    const uint id = input.id;
    if (!PeUsesGeneratedMask(constants[id]) || !PeMatchesModule(pe_materialBytes, constants[id], pc.moduleIndex))
        return output;

    // Keep the camera's precise depth expression and the draw-local precomputed clip unchanged.
    const VS_OUTPUT_Position_Uv_ID depth = PeDepthVertex(input);
    output.id = id;
    output.vertexId = depth.vertexId;
    output.maskEligible = depth.visEligible;
    PeMaskAttributes(input, depth.vertexId, output);
#ifdef PE_MASK_SHADOW
    output.position = ApplyViewportYConvention(
        mul(float4(output.worldPosition, 1.0), pc.lightViewProjection));
#else
    output.position = depth.position;
#endif

    return output;
}
