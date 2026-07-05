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
        // Map extent in world metres per axis — lets a bounded terrain match the map's aspect (no padding).
        int MapBlocksX() const { return (int)std::lround(m_surface.w * m_metersPerPixel); }
        int MapBlocksZ() const { return (int)std::lround(m_surface.h * m_metersPerPixel); }
        // lod > 0 samples the maps once per 2^lod-block cell (coarse bands never show finer detail).
        void Generate(ChunkColumn &col, int lod) override;
        // Continuous (fractional, bilinear) surface height from the map for smooth isosurface meshing.
        float SurfaceHeight(float x, float z) const override;

    private:
        struct MapImage
        {
            int w = 0;
            int h = 0;
            std::vector<uint8_t> px; // raw 0..255 maps (strata thickness, feature ids)
            std::vector<float> pxf;  // signed [-1,1] height scaler (surface map); used when isFloat
            bool isFloat = false;
            bool Valid() const { return w > 0; }
            // signedFloat: the surface height map, stored as float16 [-1,1] (PH16) or a legacy 8-bit
            // PNG whose gray is remapped to [-1,1]. Otherwise a plain 0..255 grayscale PNG.
            bool Load(const std::string &configured, const char *what, bool signedFloat);
            float SampleNorm(float nu, float nv) const; // bilinear, edge-clamped, uv in 0..1
        };

        // Map a surface scalar v in [-1,1] to a world height (blocks/metres): 0 -> groundHeight,
        // -1 -> +heightMin, +1 -> +heightMax. Cube and smooth paths both route through this.
        float MapHeight(float v) const;

        // Decorations from the features map (pixel 1 = tree, 2 = rock at the pixel's center block).
        // Deterministic per anchor, so neighboring columns emit identical blocks for a feature that
        // spans their seam. Skipped at lod > 0 — coarse cells would mangle 1-block trunks.
        void SpawnFeatures(ChunkColumn &col);

        MapImage m_surface;
        MapImage m_strata1;
        MapImage m_strata2;
        MapImage m_features;
        int m_centerWX = 0; // world-block coords the map center is pinned to
        int m_centerWZ = 0;
        int m_blocksPerPixel = 1;     // integer pixel->block step for the cube feature/strata placement
        float m_metersPerPixel = 1.f; // float metres/pixel for the smooth-surface extent (terrain); = bpp for cube
        // Smooth-terrain height mapping (see SurfaceHeight): gray 0..1 -> groundHeight + lerp(min,max).
        float m_heightMin = -32.0f;
        float m_heightMax = 32.0f;
        float m_groundHeight = 0.0f;
        int m_seaLevel = 0; // resolved absolute height; <=0 = no water
        BlockId m_surfaceBlock = 3;
        bool m_surfaceBands = false; // pick the top block by elevation instead of m_surfaceBlock
        BlockId m_strata1Block = 2;
        BlockId m_strata2Block = 1;
        BlockId m_fillBlock = 1;
        int m_strata1Thickness = 3; // fixed band thickness when the strata map is absent
        int m_strata2Thickness = 8;
    };
} // namespace pe::voxel
