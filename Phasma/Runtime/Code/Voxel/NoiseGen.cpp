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

        // FBM heightmap: groundY is the mean level, terrain rolls +/- kAmplitude around it.
        // Noise math lives in the general pe::Fbm2D (Base/Noise.h); only the terrain policy
        // (frequency, amplitude, block layering) is voxel-specific.
        // ponytail: 3 octaves of perlin = rolling hills; raise octaves or add ridged/domain-warp
        // noise only if it reads too smooth. ponytail: wx*freq is float32 — fine to ~tens-of-
        // thousands of blocks from origin; for a truly far-roaming world sample in doubles first.
        int TerrainHeight(int wx, int wz, int groundY)
        {
            constexpr float kBaseFreq = 1.0f / 96.0f; // ~96-block wavelength for the broad hills
            constexpr float kAmplitude = 28.0f;
            float n = Fbm2D(vec2((float)wx, (float)wz), 3, kBaseFreq); // default lacunarity 2 / gain 0.5
            return groundY + static_cast<int>(n * kAmplitude);
        }

        // Carve underground air pockets from a 3D-noise threshold. Sparse enough (high threshold)
        // that the surface mostly stays the rolling hills, with the occasional cave mouth/overhang.
        // ponytail: single-field threshold = blobby caverns; for winding tunnels switch to ridged
        // "worm" noise (an abs(noise) band) later. ponytail: 2 octaves of perlin3 per solid block
        // is the worldgen hot path — drop to 1 octave or gate by depth if streaming hitches.
        bool CarveCave(int wx, int wy, int wz)
        {
            constexpr float kCaveFreq = 1.0f / 22.0f;
            // Normalized 2-octave Perlin rarely exceeds ~0.6, so this threshold sets cave density:
            // ~0.25 carves the upper ~10-15% of the field = findable connected caverns; raise it
            // toward 0.4 for sparser caves, lower toward 0.15 for a more hollowed world.
            constexpr float kCaveThreshold = 0.25f;
            return Fbm3D(vec3((float)wx, (float)wy, (float)wz), 2, kCaveFreq) > kCaveThreshold;
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
                // Rocky peaks are a second "biome" from the existing 3 blocks only: above the
                // stone line the surface is bare stone (mountains), below it grass over dirt.
                const bool rocky = topY > m_groundY + 18;
                for (int wy = 0; wy < h; ++wy)
                {
                    // Carve caves strictly underground: above a 2-block bedrock floor and below a
                    // 2-block surface crust (keeps the rolling-hill surface intact). Carved cells are
                    // left as air. A shallow world (topY <= 3) has an empty [2, topY-2] range, so the
                    // mechanics smoke's surface column is never touched.
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
            }
        }
    }
} // namespace pe::voxel
