struct DrawIndexedIndirectCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

[[vk::binding(0, 0)]] RWStructuredBuffer<float> SortKeys;
[[vk::binding(1, 0)]] RWStructuredBuffer<DrawIndexedIndirectCommand> IndirectCommands;

struct PushConstants {
    uint count;
    uint blockSize;
    uint subBlockSize;
};
[[vk::push_constant]] PushConstants pc;

[numthreads(64, 1, 1)]
void mainCS(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;

    uint halfBlock = pc.subBlockSize >> 1;
    uint i = (idx / halfBlock) * pc.subBlockSize + (idx % halfBlock);
    uint j = i + halfBlock;

    if (j >= pc.count)
        return;

    bool ascending = ((i / pc.blockSize) % 2) == 0;

    float keyI = SortKeys[i];
    float keyJ = SortKeys[j];

    bool shouldSwap = ascending ? (keyI > keyJ) : (keyI < keyJ);

    if (shouldSwap)
    {
        SortKeys[i] = keyJ;
        SortKeys[j] = keyI;

        DrawIndexedIndirectCommand tmpCmd = IndirectCommands[i];
        IndirectCommands[i] = IndirectCommands[j];
        IndirectCommands[j] = tmpCmd;
    }
}
