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

struct PushConstants
{
    uint maxDrawCount;
    uint arenaSlotBase;
    uint2 pad0;
    float4 frustumPlanes[6];
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
