#pragma once
#include "Voxel/VoxelTypes.h"
#include "Voxel/BlockRegistry.h"
#include "API/Vertex.h"

// Section voxels -> CPU vertex/index arrays. FROZEN interface (Wave-1 contract).
namespace pe::voxel
{
    struct MeshData
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
    };

    // Section-local coords 0..15; MAY be queried at -1..16 to reach neighbor sections;
    // returns the neighbor block or air.
    using BlockSampler = std::function<BlockId(int x, int y, int z)>;

    class IChunkMesher
    {
    public:
        virtual ~IChunkMesher() = default;
        // lod 0 = full; lod N samples stride 2^N and merges (Phase 1 ships lod 0).
        virtual MeshData Mesh(const BlockSampler &sample, const BlockRegistry &reg, int lod) = 0;
    };
} // namespace pe::voxel
