// VoxelShadowVS.hlsl — depth-only shadow caster for the packed voxel arena.
//
// The voxel arena's vertices are packed (8 B; see VoxelVertex in API/Vertex.h), so they cannot be
// drawn by the stock ShadowsVS (which expects the PositionUvVertex layout). This mirrors ShadowsVS
// but unpacks position from w0 and adds the section origin (the slot's AABB min in Mesh_Constants).
// Voxel meshes are never skinned, so the joint path is dropped.
//
// Input is declared as a single uint2 so the reflected vertex stride is a fixed 8 B even though only
// w0 (.x) is read — w1 (.y) is unused here but must occupy the stride to match the packed buffer.

#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"

[[vk::push_constant]] PushConstants_Shadows pc;
[[vk::binding(0, 0)]] ByteAddressBuffer data;
[[vk::binding(1, 0)]] StructuredBuffer<Mesh_Constants> constants;

float4x4 LoadMatrix(uint offset)
{
    float4x4 result;
    result[0] = asfloat(data.Load4(offset + 0 * 16));
    result[1] = asfloat(data.Load4(offset + 1 * 16));
    result[2] = asfloat(data.Load4(offset + 2 * 16));
    result[3] = asfloat(data.Load4(offset + 3 * 16));
    return result;
}

float4x4 GetMeshMatrix(uint id) { return LoadMatrix(constants[id].meshDataOffset); }

struct VS_INPUT_VoxelShadow
{
    uint2 packed : POSITION; // .x = w0 (pos5x3|normal3|ao2), .y = w1 (unused here)
#if defined(PE_DX12)
    uint id : SV_StartInstanceLocation;
#else
    uint id : SV_InstanceID;
#endif
};

VS_OUTPUT_Position mainVS(VS_INPUT_VoxelShadow input)
{
    VS_OUTPUT_Position output;

    const uint id = input.id;
    const uint w0 = input.packed.x;

    float3 localPos = float3(w0 & 31u, (w0 >> 5) & 31u, (w0 >> 10) & 31u);
    float3 origin = float3(constants[id].aabbMinX, constants[id].aabbMinY, constants[id].aabbMinZ);

    float4x4 final = mul(GetMeshMatrix(id), pc.vp);
    output.position = mul(float4(origin + localPos, 1.0), final);
    return output;
}
