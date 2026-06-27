#pragma once
#include "Voxel/IChunkMesher.h"

// Greedy cube mesher: sweep-and-merge coplanar same-block visible faces into maximal quads.
// Packs the per-face block tile index into Vertex.joints[0]; uv spans the merged-quad extent
// (0..w, 0..h) so the shader's frac() tiles per block. FROZEN interface (Wave-1 contract).
namespace pe::voxel
{
    class GreedyMesher : public IChunkMesher
    {
    public:
        MeshData Mesh(BlockSampleFn, void *sampleCtx, const BlockRegistry &, int lod) override;
    };
} // namespace pe::voxel
