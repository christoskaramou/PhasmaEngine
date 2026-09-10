#ifndef PE_INSTANCE_BATCH_HLSL
#define PE_INSTANCE_BATCH_HLSL

[[vk::binding(19, 0)]] RWStructuredBuffer<uint> DrawInstanceIds;

// Each wave reserves at most one ID per eligible draw. Group after culling/LOD selection so
// instances retain their own visibility, pose and material while sharing an identical index range.
// ponytail: batches stop at wave boundaries; global grouping needs a measured win over another dispatch.
bool BatchInstances(inout DrawIndexedIndirectCommand cmd, uint drawId, uint bucket,
                    bool eligible, uint outputBase, uint counterIndex)
{
    uint count = WaveActiveCountBits(eligible);
    if (count < 2u)
        return true;

    uint base = 0u;
    if (WaveIsFirstLane())
        InterlockedAdd(Counters[counterIndex], count, base);
    base = WaveReadLaneFirst(base) + outputBase;

    bool pending = eligible;
    bool emit = !eligible;
    while (WaveActiveAnyTrue(pending))
    {
        uint leader = WaveActiveMin(pending ? WaveGetLaneIndex() : 0xffffffffu);
        uint4 key = uint4(cmd.indexCount, cmd.firstIndex, asuint(cmd.vertexOffset), bucket);
        uint4 leaderKey = WaveReadLaneAt(key, leader);
        bool match = pending && all(key == leaderKey);
        uint instances = WaveActiveCountBits(match);
        uint offset = WavePrefixCountBits(match);
        if (match)
        {
            if (instances > 1u)
                DrawInstanceIds[base + offset] = drawId;
            if (offset == 0u)
            {
                if (instances > 1u)
                {
                    cmd.instanceCount = instances;
                    cmd.firstInstance = base;
                }
                emit = true;
            }
            pending = false;
        }
        base += instances;
    }
    return emit;
}

#endif
