#pragma once

#include "Voxel/ChunkColumn.h"
#include "Voxel/VoxelTypes.h"

// Region-style column persistence: sparse section payloads overlaid on procedural terrain.
namespace pe::voxel
{
    class ColumnChunkStore
    {
    public:
        static constexpr char kMagic[4] = {'P', 'E', 'V', 'C'};
        static constexpr uint16_t kVersion = 1;

        static std::filesystem::path ResolveRoot(const std::string &configured);
        static std::filesystem::path ColumnPath(const std::filesystem::path &root, ColumnCoord coord);

        // Overwrites sections present in the on-disk file; returns false when no file exists.
        static bool TryOverlay(const std::filesystem::path &root, ChunkColumn &column);

        // Section mask from an existing column file, or 0 when absent/invalid.
        static uint16_t PersistedSectionMask(const std::filesystem::path &root, ColumnCoord coord);

        // Writes touchedMask sections plus any sections already persisted for this column.
        static bool Save(const std::filesystem::path &root, const ChunkColumn &column, uint16_t touchedMask);
    };
} // namespace pe::voxel
