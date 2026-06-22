#include "Voxel/BlockRegistry.h"

namespace pe::voxel
{
    BlockRegistry::BlockRegistry()
    {
        m_types.push_back({0, "air", false, false, VoxelRenderClass::Air, {0, 0, 0, 0, 0, 0}});
    }
    BlockId BlockRegistry::Register(const BlockType &bt)
    {
        assert((BlockId)m_types.size() == bt.id);
        m_types.push_back(bt);
        return bt.id;
    }
    const BlockType &BlockRegistry::Get(BlockId id) const
    {
        return m_types[id];
    }
    bool BlockRegistry::IsSolid(BlockId id) const
    {
        return m_types[id].solid;
    }
    bool BlockRegistry::IsOpaque(BlockId id) const
    {
        return m_types[id].opaque;
    }
    uint16_t BlockRegistry::FaceTile(BlockId id, int face) const
    {
        return m_types[id].faceTiles[face];
    }
    size_t BlockRegistry::Count() const
    {
        return m_types.size();
    }
} // namespace pe::voxel
