#pragma once
#include "Voxel/ITerrainGenerator.h"
#include "Voxel/VoxelTypes.h"

// Heightmap-driven terrain: a grayscale surface map plus optional strata-thickness maps, painted in
// any image editor. Pixel value 0..255 == surface height in blocks (kWorldHeight is 256, so the
// mapping is 1:1); one pixel spans blocksPerPixel blocks and samples lerp between pixels, so a
// low-res map still gives smooth slopes. The map is centered on the world bounds center; columns
// outside it clamp to the edge value. Strata maps stretch over the same extent and paint the
// thickness of each material band below the surface block; below the last band, fillBlock
// (0 = air -> floating-island shells) fills down to y=0.
namespace pe::voxel
{
    struct VoxelConfig;

    class MapGen : public ITerrainGenerator
    {
    public:
        explicit MapGen(const VoxelConfig &cfg);
        // False when the surface map failed to load (already warned) — caller falls back to noise.
        bool Valid() const { return m_surface.Valid(); }
        // Columns needed to cover the whole map — lets a bounded world derive its radius from the map.
        int WorldRadiusColumns() const;
        // lod > 0 samples the maps once per 2^lod-block cell (coarse bands never show finer detail).
        void Generate(ChunkColumn &col, int lod) override;

    private:
        struct MapImage
        {
            int w = 0;
            int h = 0;
            std::vector<uint8_t> px;
            bool Valid() const { return w > 0; }
            bool Load(const std::string &configured, const char *what);
            float SampleNorm(float nu, float nv) const; // bilinear, edge-clamped, uv in 0..1
        };

        MapImage m_surface;
        MapImage m_strata1;
        MapImage m_strata2;
        int m_centerWX = 0; // world-block coords the map center is pinned to
        int m_centerWZ = 0;
        int m_blocksPerPixel = 1;
        int m_seaLevel = 0; // resolved absolute height; <=0 = no water
        BlockId m_surfaceBlock = 3;
        BlockId m_strata1Block = 2;
        BlockId m_strata2Block = 1;
        BlockId m_fillBlock = 1;
        int m_strata1Thickness = 3; // fixed band thickness when the strata map is absent
        int m_strata2Thickness = 8;
    };
} // namespace pe::voxel
