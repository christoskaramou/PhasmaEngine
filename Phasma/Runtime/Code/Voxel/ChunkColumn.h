#pragma once
#include "Voxel/ChunkSection.h"

// A vertical stack of kSectionCount sections to kWorldHeight.
// GetLocal/SetLocal take column-local x/z (0..15) and WORLD y (0..kWorldHeight-1);
// out-of-range y returns kAir / no-ops. FROZEN interface (Wave-1 contract).
namespace pe::voxel
{
    class ChunkColumn
    {
    public:
        explicit ChunkColumn(ColumnCoord);
        ColumnCoord Coord() const;
        BlockId GetLocal(int lx, int wy, int lz) const;
        void SetLocal(int lx, int wy, int lz, BlockId);
        ChunkSection &Section(int si);
        const ChunkSection &Section(int si) const;
        bool SectionInBounds(int wy) const;

    private:
        ColumnCoord m_coord;
        std::array<ChunkSection, kSectionCount> m_sections;
    };
} // namespace pe::voxel
