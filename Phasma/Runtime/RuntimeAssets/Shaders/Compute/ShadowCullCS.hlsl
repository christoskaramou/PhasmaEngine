#include "../Common/Structures.hlsl"

struct DrawIndexedIndirectCommand
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

[[vk::binding(0, 0)]] StructuredBuffer<DrawIndexedIndirectCommand> IndirectCommandsIn;
[[vk::binding(1, 0)]] StructuredBuffer<Mesh_Constants> MeshConstants;
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> Counters; // [regular, voxels]
[[vk::binding(3, 0)]] RWStructuredBuffer<DrawIndexedIndirectCommand> IndirectRegularOut;
[[vk::binding(4, 0)]] RWStructuredBuffer<DrawIndexedIndirectCommand> IndirectVoxelsOut;
[[vk::binding(12, 0)]] ByteAddressBuffer NodeData;

// Same LOD params buffer the main CullingCS consumes (Scene::UpdateLodUniforms, binding 16). Lets the
// shadow cull pick each caster's LOD by camera distance instead of always emitting LOD0.
[[vk::binding(16, 0)]] cbuffer LodUBO
{
    uint lodEnabled;
    uint lodPad0;
    float lodBias;
    float lodPad1;
    float4 lodDistances;
};

struct PushConstants
{
    uint maxDrawCount;
    uint arenaSlotBase;
    uint2 pad0;
    float4 frustumPlanes[6];
    float3 cameraPos;    // for distance-based LOD selection (matches CullingCS)
    float shadowLodBias; // extra coarsening on top of lodBias; <=0 disables LOD (full-res casters)
};
[[vk::push_constant]] PushConstants pc;

float4x4 LoadMatrix(uint byteOffset)
{
    float4x4 result;
    result[0] = asfloat(NodeData.Load4(byteOffset + 0 * 16));
    result[1] = asfloat(NodeData.Load4(byteOffset + 1 * 16));
    result[2] = asfloat(NodeData.Load4(byteOffset + 2 * 16));
    result[3] = asfloat(NodeData.Load4(byteOffset + 3 * 16));
    return result;
}

void TransformAABB(float3 localMin, float3 localMax, float4x4 worldMatrix,
                   out float3 worldMin, out float3 worldMax)
{
    float3 translation = float3(worldMatrix[3][0], worldMatrix[3][1], worldMatrix[3][2]);
    worldMin = translation;
    worldMax = translation;

    for (int i = 0; i < 3; i++)
    {
        float3 col = float3(worldMatrix[i][0], worldMatrix[i][1], worldMatrix[i][2]);
        float3 a = col * localMin[i];
        float3 b = col * localMax[i];
        worldMin += min(a, b);
        worldMax += max(a, b);
    }
}

bool AABBInFrustum(float3 aabbMin, float3 aabbMax)
{
    for (int i = 0; i < 6; i++)
    {
        float3 normal = pc.frustumPlanes[i].xyz;
        float d = pc.frustumPlanes[i].w;
        float3 center = (aabbMin + aabbMax) * 0.5;
        float3 halfSize = (aabbMax - aabbMin) * 0.5;
        float dist = dot(normal, center) + d;
        float radius = dot(abs(normal), halfSize);
        if (dist < -radius)
            return false;
    }
    return true;
}

uint WaveAppend(uint counterIndex, bool emit)
{
    uint laneSlot = WavePrefixCountBits(emit);
    uint waveCount = WaveActiveCountBits(emit);
    uint base = 0;
    if (waveCount > 0 && WaveIsFirstLane())
        InterlockedAdd(Counters[counterIndex], waveCount, base);
    base = WaveReadLaneFirst(base);
    return base + laneSlot;
}

[numthreads(64, 1, 1)] void mainCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= pc.maxDrawCount)
        return;

    DrawIndexedIndirectCommand cmd = IndirectCommandsIn[idx];
    if (cmd.indexCount == 0 || cmd.instanceCount == 0)
        return;

    Mesh_Constants constants = MeshConstants[idx];
    if (NodeData.Load(constants.meshDataOffset + 128u) == 0u)
        return;

    float3 localMin = float3(constants.aabbMinX, constants.aabbMinY, constants.aabbMinZ);
    float3 localMax = float3(constants.aabbMaxX, constants.aabbMaxY, constants.aabbMaxZ);

    float4x4 worldMatrix = LoadMatrix(constants.meshDataOffset);
    float3 aabbMin, aabbMax;
    TransformAABB(localMin, localMax, worldMatrix, aabbMin, aabbMax);

    if (!AABBInFrustum(aabbMin, aabbMax))
        return;

    // Discrete LOD: override this draw's index range by camera distance, exactly like CullingCS, so
    // shadow casters shed vertices with distance instead of transforming full-res geometry x4 cascades.
    // shadowLodBias>1 drops shadow detail sooner than the visible geometry (PCF-blurred silhouettes
    // tolerate it); shadowLodBias<=0 keeps full-res (baseline / A-B toggle).
    if (lodEnabled != 0u && constants.lodMeshEnabled != 0u && constants.lodCount > 1u && pc.shadowLodBias > 0.0)
    {
        float3 lodCenter = (aabbMin + aabbMax) * 0.5;
        float lodDist = distance(pc.cameraPos, lodCenter) * lodBias * constants.lodMeshBias * pc.shadowLodBias;
        uint lod = 0u;
        if (lodDist > lodDistances.x) lod = 1u;
        if (lodDist > lodDistances.y) lod = 2u;
        if (lodDist > lodDistances.z) lod = 3u;
        lod = min(lod + constants.lodShift, constants.lodCount - 1u);
        cmd.firstIndex = constants.lodIndexOffset[lod];
        cmd.indexCount = constants.lodIndexCount[lod];
    }

    const bool isVoxel = idx >= pc.arenaSlotBase;
    const bool emitRegular = !isVoxel;
    const bool emitVoxel = isVoxel;

    uint slot;
    slot = WaveAppend(0, emitRegular);
    if (emitRegular)
        IndirectRegularOut[slot] = cmd;

    slot = WaveAppend(1, emitVoxel);
    if (emitVoxel)
        IndirectVoxelsOut[slot] = cmd;
}
