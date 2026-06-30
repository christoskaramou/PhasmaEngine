#pragma once
#include "Voxel/VoxelTypes.h"
#include "Voxel/BlockRegistry.h"
#include "API/Vertex.h"

// Section voxels -> CPU vertex/index arrays. FROZEN interface (Wave-1 contract).
namespace pe::voxel
{
    struct MeshData
    {
        std::vector<VoxelVertex> vertices; // packed 8 B/vert (see VoxelVertex bit layout)
        std::vector<uint16_t> indices;     // uint16: a 16^3 section maxes at ~49k verts (< 65536)
        // Tight section-local bounds (0..kSectionDim) of the geometry. The mesher re-bases packed
        // positions to localMin so aabbMin (= sectionOrigin + localMin) doubles as the VS origin while
        // the stored AABB hugs the real surfaces — without this a sparse cave section keeps the full
        // 16^3 cube and its nearest corner sits in air, so Hi-Z occlusion never culls it. Full-cube
        // default keeps any non-greedy mesher conservative.
        uint32_t localMin[3] = {0, 0, 0};
        uint32_t localMax[3] = {(uint32_t)kSectionDim, (uint32_t)kSectionDim, (uint32_t)kSectionDim};

        // Alpha-blended (Transparent render class, e.g. water) faces meshed into a parallel stream.
        // Uploaded as a SECOND, independent arena mesh tagged transparent so it draws in the transparent
        // GBuffer pass. Empty when the section has no transparent blocks (the common case).
        std::vector<VoxelVertex> transparentVertices;
        std::vector<uint16_t> transparentIndices;
        uint32_t transparentLocalMin[3] = {0, 0, 0};
        uint32_t transparentLocalMax[3] = {(uint32_t)kSectionDim, (uint32_t)kSectionDim, (uint32_t)kSectionDim};
    };

    // Section-local coords 0..15; MAY be queried at -1..16 to reach neighbor sections;
    // returns the neighbor block or air. Function pointer + ctx avoids std::function heap
    // traffic on the meshing hot path (each section job calls this millions of times).
    using BlockSampleFn = BlockId (*)(void *ctx, int x, int y, int z);

    class IChunkMesher
    {
    public:
        virtual ~IChunkMesher() = default;
        // lod 0 = full; lod N samples stride 2^N and merges (Phase 1 ships lod 0).
        virtual MeshData Mesh(BlockSampleFn sample, void *sampleCtx, const BlockRegistry &reg, int lod) = 0;
    };
} // namespace pe::voxel
