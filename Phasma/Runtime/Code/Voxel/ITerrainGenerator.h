#pragma once

// Fills a column's voxels.
namespace pe::voxel
{
    class ChunkColumn;

    class ITerrainGenerator
    {
    public:
        virtual ~ITerrainGenerator() = default;
        // lod is the detail band the streamer will mesh this column at (0 = full detail).
        // Generators MAY produce coarser data for lod > 0 (cell-resolution heights, no caves) —
        // that is where big-radius worlds win their load time; the world regenerates a column at
        // finer detail before its band drops. Ignoring lod and always generating full detail is
        // always correct, just slower.
        virtual void Generate(ChunkColumn &col, int lod) = 0;
    };
} // namespace pe::voxel
