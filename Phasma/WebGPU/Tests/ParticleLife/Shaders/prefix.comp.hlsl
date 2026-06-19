// Port of gpu-life/src/countingSort/prefix.wgsl.
// Single-threaded prefix sum over the cell counts buffer. Produces per-cell
// {startOffset, count} pairs in `indices[]`, while overwriting `counts[]`
// with the exclusive prefix (used as an atomic tail pointer by sort.wgsl).
//
// Workgroup is 1x1x1 and dispatch is (1,1,1). Upstream uses arrayLength to
// drive the loop; we hardcode CELL_AMT = 2000 (upstream default cellAmt).

#define CELL_AMT 2000u

[[vk::binding(0, 0)]] RWStructuredBuffer<uint> counts : register(u0, space0);
// indices[i] = {start, count}; stride 8B.
[[vk::binding(1, 0)]] RWStructuredBuffer<uint2> indices : register(u1, space0);

[numthreads(1, 1, 1)]
void CSMain()
{
    uint total = 0u;
    for (uint i = 0u; i < CELL_AMT; i++)
    {
        uint count = counts[i];
        counts[i] = total;
        indices[i] = uint2(total, count);
        total += count;
    }
}
