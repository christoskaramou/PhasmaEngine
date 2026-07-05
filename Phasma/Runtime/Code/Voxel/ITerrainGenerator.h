#pragma once

// Fills a column's voxels.
namespace pe::voxel
{
    class ChunkColumn;

    class ITerrainGenerator
    {
    public:
        virtual ~ITerrainGenerator() = default;
        // lod is the detail band the streamer will mesh this column at (0 = full detail).
        // Generators MAY produce coarser data for lod > 0 (cell-resolution heights, no caves) —
        // that is where big-radius worlds win their load time; the world regenerates a column at
        // finer detail before its band drops. Ignoring lod and always generating full detail is
        // always correct, just slower.
        virtual void Generate(ChunkColumn &col, int lod) = 0;

        // --- Smooth (isosurface) worldgen ---
        // Surface height in blocks (may be fractional) at world (x, z). Density then defaults to a
        // heightfield: solid below the surface, air above. A generator supports smooth voxel terrain
        // by overriding SurfaceHeight; overriding DensityAtHeight adds true 3D features (overhangs,
        // caves). Generators that leave both at the default only produce flat terrain when smoothed.
        virtual float SurfaceHeight(float x, float z) const { return 0.0f; }
        // Density given a pre-computed SurfaceHeight(x, z): tile meshers sample whole columns, so they
        // cache the (expensive) surface height once per column and the generator adds only its
        // per-sample 3D term here. Override THIS for 3D features; Density() routes through it.
        virtual float DensityAtHeight(float /*x*/, float y, float /*z*/, float surfaceHeight) const
        {
            return surfaceHeight - y;
        }
        virtual float Density(float x, float y, float z) const
        {
            return DensityAtHeight(x, y, z, SurfaceHeight(x, z));
        }
    };
} // namespace pe::voxel
