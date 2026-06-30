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

        int TerrainHeight(int wx, int wz, int groundY)
        {
            constexpr float kBaseFreq = 1.0f / 96.0f;
            constexpr float kAmplitude = 28.0f;
            constexpr float kWarpFreq = 1.0f / 180.0f;
            constexpr float kWarpAmp = 14.0f;

            const vec2 p((float)wx, (float)wz);
            const float warp = Fbm2D(p, 2, kWarpFreq) * kWarpAmp;
            const vec2 wp = p + vec2(warp, warp * 0.65f);

            const float hills = Fbm2D(wp, 3, kBaseFreq);
            float ridged = 1.0f - std::fabs(hills);
            ridged *= ridged;
            const float n = hills * 0.55f + ridged * 0.45f;
            return groundY + static_cast<int>(n * kAmplitude);
        }

        bool CarveCave(int wx, int wy, int wz)
        {
            constexpr float kCaveFreq = 1.0f / 22.0f;
            constexpr float kWormBand = 0.07f;
#if defined(PE_DEBUG)
            constexpr int kCaveOctaves = 1;
#else
            constexpr int kCaveOctaves = 2;
#endif
            const vec3 p((float)wx, (float)wy, (float)wz);
            const float worm = Fbm3D(p, kCaveOctaves, kCaveFreq);
            return std::fabs(worm) < kWormBand;
        }
    } // namespace

    NoiseGen::NoiseGen(int groundY) : m_groundY(groundY)
    {
    }

    void NoiseGen::Generate(ChunkColumn &col)
    {
        const ColumnCoord coord = col.Coord();
        const int baseWX = coord.cx * kSectionDim;
        const int baseWZ = coord.cz * kSectionDim;

        for (int lz = 0; lz < kSectionDim; ++lz)
        {
            for (int lx = 0; lx < kSectionDim; ++lx)
            {
                const int wx = baseWX + lx;
                const int wz = baseWZ + lz;
                int h = TerrainHeight(wx, wz, m_groundY);
                h = std::max(1, std::min(kWorldHeight, h));
                const int topY = h - 1;
                const bool rocky = topY > m_groundY + 18;
                for (int wy = 0; wy < h; ++wy)
                {
                    if (wy >= 2 && wy <= topY - 2 && CarveCave(wx, wy, wz))
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

                // Flood air above the terrain surface up to sea level — fills valleys/oceans, not caves
                // (those are below the surface, wy < h). seaLevel is a knob: raise for more water.
                const int seaLevel = m_groundY - 2;
                for (int wy = h; wy <= seaLevel && wy < kWorldHeight; ++wy)
                    col.SetLocal(lx, wy, lz, kWaterBlock);
            }
        }
    }
} // namespace pe::voxel
