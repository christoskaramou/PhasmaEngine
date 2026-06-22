#pragma once
#include "Voxel/VoxelTypes.h"

// Per-section uint16 block storage behind a seam (palette compression can drop in later).
// FROZEN interface (Wave-1 contract) — implement BlockStore.cpp against this; do not edit.
namespace pe::voxel
{
    class BlockStore
    {
    public:
        BlockId Get(int idx) const;
        void Set(int idx, BlockId);
        void Fill(BlockId);
        bool IsAllAir() const;

    private:
        std::array<BlockId, kBlocksPerSection> m_data{};
        uint32_t m_nonAir = 0;
    };
} // namespace pe::voxel
