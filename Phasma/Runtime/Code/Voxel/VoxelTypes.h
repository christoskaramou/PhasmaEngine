#pragma once

// Voxel coordinate math + constants for the PhasmaEngine voxel subsystem (Phase 1).
//
// Conventions:
//  - World is infinite in X/Z, bounded in Y. Y-up, integer block coordinates.
//  - Block id is uint16_t; 0 == air (kAir). 65,536 types (future-proof for "all" blocks).
//  - A column is a vertical stack of kSectionCount sections; each section is kSectionDim^3.
//  - Section block index layout: lx + kSectionDim*(lz + kSectionDim*ly)  (x fastest, then z, then y).
//
// Mesher vertex packing (see GreedyMesher / VoxelGBuffer shaders): the per-face block tile
// index is packed into the engine Vertex's joints[0] slot (uint32_t[4], unused for static
// voxel meshes). A uint16_t tile index fits with room to spare. If tile indices are ever
// widened, re-check the shader's `nointerpolation uint tileLayer` read of joints[0].
namespace pe::voxel
{
    inline constexpr int kSectionDim = 16;
    inline constexpr int kWorldHeight = 256;
    inline constexpr int kSectionCount = kWorldHeight / kSectionDim;
    inline constexpr int kBlocksPerSection = kSectionDim * kSectionDim * kSectionDim;
    using BlockId = uint16_t;
    inline constexpr BlockId kAir = 0;

    struct BlockPos
    {
        int x, y, z;
    };
    struct ColumnCoord
    {
        int cx, cz;
        bool operator==(const ColumnCoord &o) const { return cx == o.cx && cz == o.cz; }
    };

    inline int FloorDiv(int a, int b)
    {
        int q = a / b, r = a % b;
        if ((r != 0) && ((r < 0) != (b < 0)))
            --q;
        return q;
    }
    inline int Mod(int a, int b)
    {
        int r = a % b;
        if (r < 0)
            r += b;
        return r;
    }

    inline ColumnCoord WorldToColumn(int wx, int wz)
    {
        return {FloorDiv(wx, kSectionDim), FloorDiv(wz, kSectionDim)};
    }
    inline int LocalX(int wx)
    {
        return Mod(wx, kSectionDim);
    }
    inline int LocalZ(int wz)
    {
        return Mod(wz, kSectionDim);
    }
    inline int SectionIndex(int wy)
    {
        return FloorDiv(wy, kSectionDim);
    }
    inline int LocalY(int wy)
    {
        return Mod(wy, kSectionDim);
    }
    inline int BlockIndex(int lx, int ly, int lz)
    {
        return lx + kSectionDim * (lz + kSectionDim * ly);
    }
} // namespace pe::voxel
