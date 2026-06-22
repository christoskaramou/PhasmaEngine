#pragma once
#include "Voxel/BlockStore.h"

// A 16^3 section: block get/set, dirty flag, backed by a BlockStore.
// Dirty defaults true on first fill. FROZEN interface (Wave-1 contract).
namespace pe::voxel
{
    class ChunkSection
    {
    public:
        BlockId Get(int lx, int ly, int lz) const;
        void Set(int lx, int ly, int lz, BlockId);
        bool IsEmpty() const;
        bool IsDirty() const;
        void SetDirty(bool);
        BlockStore &Store();
        const BlockStore &Store() const;

    private:
        BlockStore m_store;
        bool m_dirty = true;
    };
} // namespace pe::voxel
