#pragma once

// Fills a column's voxels. FROZEN interface (Wave-1 contract).
namespace pe::voxel
{
    class ChunkColumn;

    class ITerrainGenerator
    {
    public:
        virtual ~ITerrainGenerator() = default;
        virtual void Generate(ChunkColumn &col) = 0;
    };
} // namespace pe::voxel
