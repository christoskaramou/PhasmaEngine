#pragma once
#include "Voxel/ITerrainGenerator.h"

// Default engine terrain generator: domain-warped FBM + ridged hills, worm-tunnel caves,
// stone/dirt/grass layering and bare-stone "rocky peaks". This is just the out-of-the-box default — a game
// supplies its own world shape by implementing ITerrainGenerator and passing it to
// VoxelWorld::SetTerrainGenerator (the engine ships a sensible default, the game owns policy).
namespace pe::voxel
{
    // Terrain-shape knobs; defaults reproduce the historical baked-in look. Per-sample math is
    // stateless, so the generator stays thread-safe (workers generate distinct columns concurrently).
    struct NoiseParams
    {
        int groundY = 64;
        float amplitude = 28.0f; // peak height above groundY in blocks; 0 = flat plain
        // Smooth-path height range (metres): the normalized noise 0..1 maps to
        // groundHeight + lerp(heightMin, heightMax, n), the same mapping the heightmap path uses.
        float groundHeight = 0.0f;
        float heightMin = -32.0f;
        float heightMax = 32.0f;
        float featureScale = 96.0f; // base feature wavelength in blocks (bigger = wide rolling hills)
        int seed = 0;               // shifts the noise domain; each seed is a different world
        bool caves = true;
        int seaLevel = -1; // <0 = auto (groundY - 2), 0 = no water, >0 = absolute block height
        // Smooth-path 3D relief 0..1: warps the density near the surface with 3D FBM so cliffs
        // undercut and hollows open (true overhangs/caves). 0 keeps the pure heightfield. The surface
        // never leaves the influence band of ± 0.5 * (heightMax - heightMin) around SurfaceHeight —
        // tile meshers size their vertical sampling band from that bound.
        float overhangs = 0.0f;
    };

    class NoiseGen : public ITerrainGenerator
    {
    public:
        explicit NoiseGen(const NoiseParams &params);
        // lod > 0 generates coarse data: one height sample per 2^lod-block cell and no caves —
        // the noise evaluations (especially per-block 3D cave fbm) dominate world load time.
        void Generate(ChunkColumn &col, int lod) override;
        // Continuous (fractional) terrain height for smooth isosurface meshing — the same warp/ridge
        // FBM as the block path without the integer quantization.
        float SurfaceHeight(float x, float z) const override;
        // Heightfield density, plus the near-surface 3D FBM warp when overhangs > 0.
        float DensityAtHeight(float x, float y, float z, float surfaceHeight) const override;

    private:
        NoiseParams m_p;
    };
} // namespace pe::voxel
