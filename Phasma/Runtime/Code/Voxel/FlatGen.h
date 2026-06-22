#pragma once
#include "Voxel/ITerrainGenerator.h"
#include "Voxel/VoxelTypes.h"

// Flat ground generator: fills wy in [0, groundY) with `fill`, rest air, across all
// 16x16 column-local cells. FROZEN interface (Wave-1 contract).
namespace pe::voxel
{
    class FlatGen : public ITerrainGenerator
    {
    public:
        FlatGen(int groundY, BlockId fill);
        void Generate(ChunkColumn &) override;

    private:
        int m_groundY;
        BlockId m_fill;
    };
} // namespace pe::voxel
