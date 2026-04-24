// Port of gpu-life/src/countingSort/sort.wgsl.
// Reads the hashed SortParticle stream from `input` and scatters each entry
// to its sorted slot in `output`, atomically bumping offsets[cellHash] (which
// was just set to the cell's start offset by prefix.wgsl).

#define SORT_STRIDE 32u

[[vk::binding(0, 0)]] ByteAddressBuffer inputSorted : register(t0, space0);
[[vk::binding(1, 0)]] RWByteAddressBuffer outputSorted : register(u0, space0);

[[vk::binding(2, 0)]] RWStructuredBuffer<uint> offsets : register(u1, space0);

static const uint kParticleCount = 50000u;

[numthreads(128, 1, 1)]
void CSMain(uint3 gid : SV_DispatchThreadID)
{
    uint i = gid.x;
    if (i >= kParticleCount)
        return;

    uint base = i * SORT_STRIDE;

    // Read the full 32B entry.
    uint w0 = inputSorted.Load(base + 0u);
    uint w1 = inputSorted.Load(base + 4u);
    uint w2 = inputSorted.Load(base + 8u);
    uint w3 = inputSorted.Load(base + 12u);
    uint w4 = inputSorted.Load(base + 16u);
    uint w5 = inputSorted.Load(base + 20u);
    uint cellHash = inputSorted.Load(base + 24u);
    uint particleIdx = inputSorted.Load(base + 28u);

    uint targetIndex;
    InterlockedAdd(offsets[cellHash], 1u, targetIndex);

    uint outBase = targetIndex * SORT_STRIDE;
    outputSorted.Store(outBase + 0u, w0);
    outputSorted.Store(outBase + 4u, w1);
    outputSorted.Store(outBase + 8u, w2);
    outputSorted.Store(outBase + 12u, w3);
    outputSorted.Store(outBase + 16u, w4);
    outputSorted.Store(outBase + 20u, w5);
    outputSorted.Store(outBase + 24u, cellHash);
    outputSorted.Store(outBase + 28u, particleIdx);
}
