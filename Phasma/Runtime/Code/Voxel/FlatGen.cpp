#include "Voxel/FlatGen.h"
#include "Voxel/ChunkColumn.h"

namespace pe::voxel
{
    FlatGen::FlatGen(int groundY, BlockId fill) : m_groundY(groundY), m_fill(fill)
    {
    }

    void FlatGen::Generate(ChunkColumn &col)
    {
        for (int lx = 0; lx < kSectionDim; ++lx)
        {
            for (int lz = 0; lz < kSectionDim; ++lz)
            {
                for (int wy = 0; wy < m_groundY; ++wy)
                {
                    col.SetLocal(lx, wy, lz, m_fill);
                }
            }
        }
    }
} // namespace pe::voxel
