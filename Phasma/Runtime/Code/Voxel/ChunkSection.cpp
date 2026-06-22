#include "Voxel/ChunkSection.h"

namespace pe::voxel
{

    BlockId ChunkSection::Get(int lx, int ly, int lz) const
    {
        return m_store.Get(BlockIndex(lx, ly, lz));
    }

    void ChunkSection::Set(int lx, int ly, int lz, BlockId id)
    {
        m_store.Set(BlockIndex(lx, ly, lz), id);
        m_dirty = true;
    }

    bool ChunkSection::IsEmpty() const
    {
        return m_store.IsAllAir();
    }

    bool ChunkSection::IsDirty() const
    {
        return m_dirty;
    }

    void ChunkSection::SetDirty(bool d)
    {
        m_dirty = d;
    }

    BlockStore &ChunkSection::Store()
    {
        return m_store;
    }

    const BlockStore &ChunkSection::Store() const
    {
        return m_store;
    }

} // namespace pe::voxel
