#pragma once
#include "Voxel/BlockType.h"

// Register/lookup block types. Air (id 0) is auto-registered in the ctor:
// {0,"air",false,false,Air,{0}}. Dense ids: Register asserts id==m_types.size().
// FROZEN interface (Wave-1 contract).
namespace pe::voxel
{
    class BlockRegistry
    {
    public:
        BlockRegistry();
        BlockId Register(const BlockType &);
        const BlockType &Get(BlockId) const;
        bool IsSolid(BlockId) const;
        bool IsOpaque(BlockId) const;
        bool IsTransparent(BlockId) const; // render class == Transparent (water)
        uint16_t FaceTile(BlockId, int face) const;
        size_t Count() const;

    private:
        std::vector<BlockType> m_types;
    };
} // namespace pe::voxel
