#include "Voxel/ChunkColumn.h"

namespace pe::voxel
{
    ChunkColumn::ChunkColumn(ColumnCoord coord) : m_coord(coord)
    {
    }

    ColumnCoord ChunkColumn::Coord() const
    {
        return m_coord;
    }

    bool ChunkColumn::SectionInBounds(int wy) const
    {
        return wy >= 0 && wy < kWorldHeight;
    }

    BlockId ChunkColumn::GetLocal(int lx, int wy, int lz) const
    {
        if (!SectionInBounds(wy))
            return kAir;

        return m_sections[SectionIndex(wy)].Get(lx, LocalY(wy), lz);
    }

    void ChunkColumn::SetLocal(int lx, int wy, int lz, BlockId id)
    {
        if (!SectionInBounds(wy))
            return;

        m_sections[SectionIndex(wy)].Set(lx, LocalY(wy), lz, id);
    }

    ChunkSection &ChunkColumn::Section(int si)
    {
        return m_sections[si];
    }

    const ChunkSection &ChunkColumn::Section(int si) const
    {
        return m_sections[si];
    }
} // namespace pe::voxel
