#include "Voxel/BlockStore.h"

namespace pe::voxel
{

    BlockId BlockStore::Get(int idx) const
    {
        return m_data[idx];
    }

    void BlockStore::Set(int idx, BlockId id)
    {
        BlockId old = m_data[idx];
        if (old == kAir && id != kAir)
            ++m_nonAir;
        else if (old != kAir && id == kAir)
            --m_nonAir;
        m_data[idx] = id;
    }

    void BlockStore::Fill(BlockId id)
    {
        m_data.fill(id);
        m_nonAir = (id == kAir) ? 0u : static_cast<uint32_t>(kBlocksPerSection);
    }

    bool BlockStore::IsAllAir() const
    {
        return m_nonAir == 0;
    }

} // namespace pe::voxel
