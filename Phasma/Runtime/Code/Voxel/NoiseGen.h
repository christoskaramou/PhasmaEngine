#pragma once
#include "Voxel/ITerrainGenerator.h"

// Default engine terrain generator: domain-warped FBM + ridged hills, worm-tunnel caves,
// stone/dirt/grass layering and bare-stone "rocky peaks". This is just the out-of-the-box default — a game
// supplies its own world shape by implementing ITerrainGenerator and passing it to
// VoxelWorld::SetTerrainGenerator (the engine ships a sensible default, the game owns policy).
namespace pe::voxel
{
    class NoiseGen : public ITerrainGenerator
    {
    public:
        explicit NoiseGen(int groundY);
        void Generate(ChunkColumn &col) override;

    private:
        int m_groundY;
    };
} // namespace pe::voxel
