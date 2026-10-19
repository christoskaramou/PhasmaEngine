// Material passes 1 and 2: bins each 8x8-pixel visibility tile once per module it holds (bin layout: VisibilityCommon.slang
// PeBinThread), so each module's passes dispatch over its own tiles instead of the whole visibility buffer.
#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"
#include "VisibilityCommon.slang"

struct PushConstants_MaterialClassify
{
    uint slotCount;   // bins, one per module of the scene
    uint mapSize;     // entries of pe_moduleSlots
    uint capacity;    // tiles per bin: every tile of the visibility buffer
    uint tilesOffset; // byte offset of bin 0's tiles, after every bin's uint4
};

[[vk::push_constant]] PushConstants_MaterialClassify pc;

[[vk::binding(0, 0)]] Texture2D<uint4> pe_visibility;
[[vk::binding(1, 0)]] StructuredBuffer<Mesh_Constants> pe_constants;
[[vk::binding(2, 0)]] ByteAddressBuffer pe_materialBytes;
[[vk::binding(3, 0)]] ByteAddressBuffer pe_moduleSlots; // runtime module index -> bin, 0xffffffff for none
[[vk::binding(4, 0)]] RWByteAddressBuffer pe_bins;     // tile counts and row counts zeroed before the dispatch

groupshared uint gs_slots[64];

uint SlotAt(uint2 pixel, uint2 size)
{
    if (any(pixel >= size))
        return 0xffffffffu;

    const uint4 vis = pe_visibility[pixel];
    if (vis.x == 0u)
        return 0xffffffffu;

    const uint module = PeModuleOf(pe_materialBytes, pe_constants[vis.x - 1u]);
    return module < pc.mapSize ? pe_moduleSlots.Load(module * 4u) : 0xffffffffu;
}

[numthreads(8, 8, 1)]
void mainCS(uint3 group : SV_GroupID, uint3 local : SV_GroupThreadID, uint index : SV_GroupIndex)
{
    if (all(group.xy == 0u))
    {
        for (uint s = index; s < pc.slotCount; s += 64u)
        {
            pe_bins.Store(s * 16u, PE_BIN_ROW_TILES * 4u);
            pe_bins.Store(s * 16u + 8u, 1u);
        }
    }

    uint2 size;
    pe_visibility.GetDimensions(size.x, size.y);
    const uint slot = SlotAt(group.xy * 8u + local.xy, size);
    gs_slots[index] = slot;
    GroupMemoryBarrierWithGroupSync();

    // The first thread holding a bin appends the tile to it once (wave-size independent).
    bool first = slot < pc.slotCount;
    for (uint i = 0u; i < index && first; ++i)
        first = gs_slots[i] != slot;

    if (!first)
        return;

    uint tile;
    pe_bins.InterlockedAdd(slot * 16u + 12u, 1u, tile);
    pe_bins.Store(pc.tilesOffset + (slot * pc.capacity + tile) * 4u, group.x | (group.y << 16u));
    if (tile % PE_BIN_ROW_TILES == 0u)
        pe_bins.InterlockedAdd(slot * 16u + 4u, 1u);
}
