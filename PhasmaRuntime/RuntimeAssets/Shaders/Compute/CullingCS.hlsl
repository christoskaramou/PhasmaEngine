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
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> Counters; // [opaqueSS, alphaCutSS, alphaBlend, transmission, selected, opaqueDS, alphaCutDS]
[[vk::binding(3, 0)]] RWStructuredBuffer<DrawIndexedIndirectCommand> IndirectOpaqueSS;
[[vk::binding(4, 0)]] RWStructuredBuffer<DrawIndexedIndirectCommand> IndirectAlphaCutSS;
[[vk::binding(5, 0)]] RWStructuredBuffer<DrawIndexedIndirectCommand> IndirectAlphaBlendOut;
[[vk::binding(6, 0)]] RWStructuredBuffer<DrawIndexedIndirectCommand> IndirectTransmissionOut;
[[vk::binding(7, 0)]] RWStructuredBuffer<DrawIndexedIndirectCommand> IndirectSelectedOut;
[[vk::binding(8, 0)]] RWStructuredBuffer<DrawIndexedIndirectCommand> IndirectOpaqueDS;
[[vk::binding(9, 0)]] RWStructuredBuffer<DrawIndexedIndirectCommand> IndirectAlphaCutDS;
[[vk::binding(10, 0)]] RWStructuredBuffer<float> SortKeysAlphaBlend;
[[vk::binding(11, 0)]] RWStructuredBuffer<float> SortKeysTransmission;
[[vk::binding(12, 0)]] ByteAddressBuffer NodeData;

struct PushConstants
{
    uint maxDrawCount;
    uint enableFrustumCulling;
    float cameraPositionX;
    float cameraPositionY;
    float cameraPositionZ;
    float pad0;
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

[numthreads(64, 1, 1)] void mainCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= pc.maxDrawCount)
        return;

    DrawIndexedIndirectCommand cmd = IndirectCommandsIn[idx];

    if (cmd.indexCount == 0 || cmd.instanceCount == 0)
        return;

    Mesh_Constants constants = MeshConstants[idx];

    float3 localMin = float3(constants.aabbMinX, constants.aabbMinY, constants.aabbMinZ);
    float3 localMax = float3(constants.aabbMaxX, constants.aabbMaxY, constants.aabbMaxZ);

    float4x4 worldMatrix = LoadMatrix(constants.meshDataOffset);
    float3 aabbMin, aabbMax;
    TransformAABB(localMin, localMax, worldMatrix, aabbMin, aabbMax);

    if (pc.enableFrustumCulling)
    {
        if (!AABBInFrustum(aabbMin, aabbMax))
            return;
    }

    cmd.firstInstance = idx;

    uint type = constants.renderType;
    bool doubleSided = (constants.editorFlags & 2) != 0;
    uint offset = 0;

    if (type == 1)
    {
        if (doubleSided)
        {
            InterlockedAdd(Counters[5], 1, offset);
            IndirectOpaqueDS[offset] = cmd;
        }
        else
        {
            InterlockedAdd(Counters[0], 1, offset);
            IndirectOpaqueSS[offset] = cmd;
        }
    }
    else if (type == 2)
    {
        if (doubleSided)
        {
            InterlockedAdd(Counters[6], 1, offset);
            IndirectAlphaCutDS[offset] = cmd;
        }
        else
        {
            InterlockedAdd(Counters[1], 1, offset);
            IndirectAlphaCutSS[offset] = cmd;
        }
    }
    else if (type == 3)
    {
        float3 center = (aabbMin + aabbMax) * 0.5;
        float3 camPos = float3(pc.cameraPositionX, pc.cameraPositionY, pc.cameraPositionZ);
        float dist = distance(camPos, center);
        InterlockedAdd(Counters[2], 1, offset);
        IndirectAlphaBlendOut[offset] = cmd;
        SortKeysAlphaBlend[offset] = -dist; // negative: ascending sort gives back-to-front
    }
    else if (type == 4)
    {
        float3 center = (aabbMin + aabbMax) * 0.5;
        float3 camPos = float3(pc.cameraPositionX, pc.cameraPositionY, pc.cameraPositionZ);
        float dist = distance(camPos, center);
        InterlockedAdd(Counters[3], 1, offset);
        IndirectTransmissionOut[offset] = cmd;
        SortKeysTransmission[offset] = -dist; // negative: ascending sort gives back-to-front
    }

    if (constants.editorFlags & 1)
    {
        InterlockedAdd(Counters[4], 1, offset);
        IndirectSelectedOut[offset] = cmd;
    }
}
