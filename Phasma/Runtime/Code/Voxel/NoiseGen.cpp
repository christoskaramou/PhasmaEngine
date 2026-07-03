#include "Voxel/NoiseGen.h"
#include "Voxel/ChunkColumn.h"
#include "Voxel/VoxelTypes.h"
#include "Base/Noise.h" // pe::Fbm2D — general engine noise util (not voxel-specific)

namespace pe::voxel
{
    namespace
    {
        // World block-id convention (see VoxelWorld::RegisterDefaultBlocks): 1=stone, 2=dirt, 3=grass.
        constexpr BlockId kStoneBlock = 1;
        constexpr BlockId kDirtBlock = 2;
        constexpr BlockId kGrassBlock = 3;
        constexpr BlockId kWaterBlock = 4;

        // Seed shifts the sample domain (glm::perlin has no seed input). Kept moderate so float
        // precision at large coordinates stays fine.
        vec2 SeedOffset2D(int seed)
        {
            return vec2(seed * 157.31f, seed * 113.17f);
        }

        int TerrainHeight(int wx, int wz, const NoiseParams &p)
        {
            const float baseFreq = 1.0f / p.featureScale;
            const float warpFreq = baseFreq / 1.875f; // historical 96 -> 180 wavelength ratio
            const float warpAmp = p.amplitude * 0.5f; // historical 28 -> 14 amplitude ratio

            const vec2 sp = vec2((float)wx, (float)wz) + SeedOffset2D(p.seed);
            const float warp = Fbm2D(sp, 2, warpFreq) * warpAmp;
            const vec2 wp = sp + vec2(warp, warp * 0.65f);

            const float hills = Fbm2D(wp, 3, baseFreq);
            float ridged = 1.0f - std::fabs(hills);
            ridged *= ridged;
            const float n = hills * 0.55f + ridged * 0.45f;
            return p.groundY + static_cast<int>(n * p.amplitude);
        }

        bool CarveCave(int wx, int wy, int wz, int seed)
        {
            constexpr float kCaveFreq = 1.0f / 22.0f;
            constexpr float kWormBand = 0.07f;
#if defined(PE_DEBUG)
            constexpr int kCaveOctaves = 1;
#else
            constexpr int kCaveOctaves = 2;
#endif
            const vec3 p = vec3((float)wx, (float)wy, (float)wz) + vec3(seed * 91.7f, seed * 45.3f, seed * 137.9f);
            const float worm = Fbm3D(p, kCaveOctaves, kCaveFreq);
            return std::fabs(worm) < kWormBand;
        }
    } // namespace

    NoiseGen::NoiseGen(const NoiseParams &params) : m_p(params)
    {
        m_p.amplitude = std::max(0.0f, m_p.amplitude);
        m_p.featureScale = std::max(4.0f, m_p.featureScale);
    }

    void NoiseGen::Generate(ChunkColumn &col, int lod)
    {
        const ColumnCoord coord = col.Coord();
        const int baseWX = coord.cx * kSectionDim;
        const int baseWZ = coord.cz * kSectionDim;
        const int stride = 1 << lod;
        // "Rocky peaks" kick in at the same fraction of the amplitude the historical look used (18/28).
        const int rockyAbove = static_cast<int>(m_p.amplitude * 0.64f);
        const int seaLevel = m_p.seaLevel < 0 ? m_p.groundY - 2 : m_p.seaLevel;

        // One height sample per 2^lod cell (stride 1 == the historical per-block path, byte-identical).
        // Coarse bands never show sub-cell detail, and the saved noise evaluations — especially the
        // per-block 3D cave fbm, skipped entirely at lod > 0 — are the bulk of world load time.
        for (int lz0 = 0; lz0 < kSectionDim; lz0 += stride)
        {
            for (int lx0 = 0; lx0 < kSectionDim; lx0 += stride)
            {
                int h = TerrainHeight(baseWX + lx0 + stride / 2, baseWZ + lz0 + stride / 2, m_p);
                h = std::max(1, std::min(kWorldHeight, h));
                const int topY = h - 1;
                const bool rocky = topY > m_p.groundY + rockyAbove;
                for (int oz = 0; oz < stride; ++oz)
                {
                    for (int ox = 0; ox < stride; ++ox)
                    {
                        const int lx = lx0 + ox;
                        const int lz = lz0 + oz;
                        for (int wy = 0; wy < h; ++wy)
                        {
                            if (lod == 0 && m_p.caves && wy >= 2 && wy <= topY - 2 &&
                                CarveCave(baseWX + lx, wy, baseWZ + lz, m_p.seed))
                                continue;
                            BlockId b = kStoneBlock;
                            if (!rocky)
                            {
                                if (wy == topY)
                                    b = kGrassBlock;
                                else if (wy >= topY - 3)
                                    b = kDirtBlock;
                            }
                            col.SetLocal(lx, wy, lz, b);
                        }

                        // Flood air above the terrain surface up to sea level — fills valleys/oceans,
                        // not caves (those are below the surface, wy < h).
                        for (int wy = h; wy <= seaLevel && wy < kWorldHeight; ++wy)
                            col.SetLocal(lx, wy, lz, kWaterBlock);
                    }
                }
            }
        }
    }
} // namespace pe::voxel
