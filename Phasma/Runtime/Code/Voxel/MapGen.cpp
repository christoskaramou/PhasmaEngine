#include "Voxel/MapGen.h"
#include "Voxel/ChunkColumn.h"
#include "Voxel/ColumnChunkStore.h" // ResolveRoot: Assets-relative or absolute path resolution
#include "Voxel/VoxelWorld.h"
#include "stb_image.h"

namespace pe::voxel
{
    namespace
    {
        constexpr BlockId kStoneBlock = 1;
        constexpr BlockId kWaterBlock = 4;
        constexpr BlockId kWoodBlock = 5;
        constexpr BlockId kLeavesBlock = 6;
        constexpr BlockId kRoadBlock = 7;
        constexpr BlockId kSandBlock = 8;
        constexpr BlockId kDryGrassBlock = 9;
        constexpr BlockId kRockBlock = 10;
        constexpr BlockId kSnowBlock = 11;
        constexpr BlockId kOliveLeavesBlock = 20;
        constexpr BlockId kCypressLeavesBlock = 21;
        // Features >= this paint block (value - base) onto the surface (any block, from the painter's
        // tile palette); values below are structural features (tree/rock/road/olive/cypress).
        constexpr uint8_t kBlockPaintBase = 64;
        constexpr int kFeatureMargin = 3; // widest spill past an anchor (tree canopy radius + 1)

        // Elevation bands for surfaceBands mode, as blocks above sea level. Tuned for the Greece map
        // (coast ~1, hills, mountains ~40-70, peaks like Olympus ~90). ponytail: hardcoded thresholds,
        // promote to config if a second banded map wants a different profile.
        BlockId BandBlock(int heightAboveSea)
        {
            if (heightAboveSea <= 2)
                return kSandBlock; // beaches + shallow seabed
            if (heightAboveSea <= 35)
                return kDryGrassBlock; // golden Mediterranean lowlands
            if (heightAboveSea <= 70)
                return kRockBlock; // bare mountain slopes
            return kSnowBlock;     // peaks
        }

        uint32_t FeatureHash(int x, int z)
        {
            return static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(z) * 19349663u;
        }
    } // namespace

    bool MapGen::MapImage::Load(const std::string &configured, const char *what)
    {
        if (configured.empty())
            return false;
        const std::filesystem::path path = ColumnChunkStore::ResolveRoot(configured);
        int channels = 0;
        stbi_uc *data = stbi_load(path.string().c_str(), &w, &h, &channels, 1); // 1 = force grayscale
        if (!data)
        {
            PE_WARN("MapGen: cannot load %s map '%s' (%s)", what, path.string().c_str(), stbi_failure_reason());
            w = h = 0;
            return false;
        }
        px.assign(data, data + static_cast<size_t>(w) * static_cast<size_t>(h));
        stbi_image_free(data);
        return true;
    }

    float MapGen::MapImage::SampleNorm(float nu, float nv) const
    {
        const float u = std::clamp(nu * (float)w - 0.5f, 0.0f, (float)(w - 1));
        const float v = std::clamp(nv * (float)h - 0.5f, 0.0f, (float)(h - 1));
        const int x0 = (int)u;
        const int y0 = (int)v;
        const int x1 = std::min(x0 + 1, w - 1);
        const int y1 = std::min(y0 + 1, h - 1);
        const float fx = u - (float)x0;
        const float fy = v - (float)y0;
        const float top = (float)px[y0 * w + x0] + ((float)px[y0 * w + x1] - (float)px[y0 * w + x0]) * fx;
        const float bot = (float)px[y1 * w + x0] + ((float)px[y1 * w + x1] - (float)px[y1 * w + x0]) * fx;
        return top + (bot - top) * fy;
    }

    MapGen::MapGen(const VoxelConfig &cfg)
    {
        m_surface.Load(cfg.heightmapPath, "surface height");
        m_strata1.Load(cfg.strata1Path, "strata 1 thickness");
        m_strata2.Load(cfg.strata2Path, "strata 2 thickness");
        m_features.Load(cfg.featuresPath, "features");
        m_blocksPerPixel = std::max(1, cfg.blocksPerPixel);
        m_centerWX = cfg.boundsCenterCx * kSectionDim + kSectionDim / 2;
        m_centerWZ = cfg.boundsCenterCz * kSectionDim + kSectionDim / 2;
        m_seaLevel = cfg.seaLevel < 0 ? cfg.groundY - 2 : cfg.seaLevel;
        m_surfaceBlock = static_cast<BlockId>(std::max(0, cfg.surfaceBlock));
        m_surfaceBands = cfg.surfaceBands;
        m_strata1Block = static_cast<BlockId>(std::max(0, cfg.strata1Block));
        m_strata2Block = static_cast<BlockId>(std::max(0, cfg.strata2Block));
        m_fillBlock = static_cast<BlockId>(std::max(0, cfg.fillBlock));
        m_strata1Thickness = std::max(0, cfg.strata1Thickness);
        m_strata2Thickness = std::max(0, cfg.strata2Thickness);
    }

    int MapGen::WorldRadiusColumns() const
    {
        const int extent = std::max(m_surface.w, m_surface.h) * m_blocksPerPixel;
        return (extent / 2 + kSectionDim - 1) / kSectionDim;
    }

    void MapGen::Generate(ChunkColumn &col, int lod)
    {
        if (!Valid())
            return;

        const ColumnCoord coord = col.Coord();
        const int baseWX = coord.cx * kSectionDim;
        const int baseWZ = coord.cz * kSectionDim;
        const int stride = 1 << lod;
        const float extentX = (float)(m_surface.w * m_blocksPerPixel);
        const float extentZ = (float)(m_surface.h * m_blocksPerPixel);

        // One map sample per 2^lod cell (stride 1 == per block); coarse bands mesh cell-sized
        // anyway, so finer sampling would be thrown away.
        for (int lz0 = 0; lz0 < kSectionDim; lz0 += stride)
        {
            for (int lx0 = 0; lx0 < kSectionDim; lx0 += stride)
            {
                const int wx = baseWX + lx0 + stride / 2;
                const int wz = baseWZ + lz0 + stride / 2;
                const float nu = (float)(wx - m_centerWX) / extentX + 0.5f;
                const float nv = (float)(wz - m_centerWZ) / extentZ + 0.5f;
                // Pixel value == surface height in blocks; clamp to >=1 so there is always ground.
                const int hgt = std::clamp((int)std::lround(m_surface.SampleNorm(nu, nv)), 1, kWorldHeight);
                const int topY = hgt - 1;
                const int t1 = m_strata1.Valid() ? (int)std::lround(m_strata1.SampleNorm(nu, nv)) : m_strata1Thickness;
                const int t2 = m_strata2.Valid() ? (int)std::lround(m_strata2.SampleNorm(nu, nv)) : m_strata2Thickness;
                const BlockId surfaceTop = m_surfaceBands ? BandBlock(hgt - m_seaLevel) : m_surfaceBlock;

                for (int oz = 0; oz < stride; ++oz)
                {
                    for (int ox = 0; ox < stride; ++ox)
                    {
                        const int lx = lx0 + ox;
                        const int lz = lz0 + oz;
                        for (int wy = 0; wy <= topY; ++wy)
                        {
                            BlockId b;
                            if (wy == topY)
                                b = surfaceTop;
                            else if (wy >= topY - t1)
                                b = m_strata1Block;
                            else if (wy >= topY - t1 - t2)
                                b = m_strata2Block;
                            else
                                b = m_fillBlock;
                            if (b != kAir)
                                col.SetLocal(lx, wy, lz, b);
                        }

                        for (int wy = hgt; wy <= m_seaLevel && wy < kWorldHeight; ++wy)
                            col.SetLocal(lx, wy, lz, kWaterBlock);
                    }
                }
            }
        }

        if (lod == 0 && m_features.Valid())
            SpawnFeatures(col);
    }

    void MapGen::SpawnFeatures(ChunkColumn &col)
    {
        const ColumnCoord coord = col.Coord();
        const int baseWX = coord.cx * kSectionDim;
        const int baseWZ = coord.cz * kSectionDim;
        const int bpp = m_blocksPerPixel;
        const int originWX = m_centerWX - m_features.w * bpp / 2; // world block of pixel (0,0)
        const int originWZ = m_centerWZ - m_features.h * bpp / 2;

        const auto place = [&](int wx, int wy, int wz, BlockId b, bool onlyAir)
        {
            const int lx = wx - baseWX;
            const int lz = wz - baseWZ;
            if (lx < 0 || lx >= kSectionDim || lz < 0 || lz >= kSectionDim || wy < 0 || wy >= kWorldHeight)
                return;
            if (onlyAir && col.GetLocal(lx, wy, lz) != kAir)
                return;
            col.SetLocal(lx, wy, lz, b);
        };

        // Scan the feature pixels overlapping this column grown by the spill margin; every column
        // derives the same feature from the shared anchor pixel, so seam halves match.
        const int pxMin = std::max(0, FloorDiv(baseWX - kFeatureMargin - originWX, bpp));
        const int pxMax = std::min(m_features.w - 1, FloorDiv(baseWX + kSectionDim - 1 + kFeatureMargin - originWX, bpp));
        const int pzMin = std::max(0, FloorDiv(baseWZ - kFeatureMargin - originWZ, bpp));
        const int pzMax = std::min(m_features.h - 1, FloorDiv(baseWZ + kSectionDim - 1 + kFeatureMargin - originWZ, bpp));

        for (int pz = pzMin; pz <= pzMax; ++pz)
        {
            for (int px = pxMin; px <= pxMax; ++px)
            {
                const uint8_t id = m_features.px[pz * m_features.w + px];
                if (id == 0)
                    continue;
                const int ax = originWX + px * bpp + bpp / 2; // anchor block = the pixel's center
                const int az = originWZ + pz * bpp + bpp / 2;
                const float nu = (float)(ax - m_centerWX) / (float)(m_surface.w * bpp) + 0.5f;
                const float nv = (float)(az - m_centerWZ) / (float)(m_surface.h * bpp) + 0.5f;
                const int hgt = std::clamp((int)std::lround(m_surface.SampleNorm(nu, nv)), 1, kWorldHeight);
                if (hgt <= m_seaLevel) // underwater: no decorations
                    continue;
                const int topY = hgt - 1;
                const uint32_t hash = FeatureHash(ax, az);

                if (id == 1) // tree: wood trunk + rounded leaf canopy
                {
                    const int trunkH = 4 + (int)(hash % 3u);
                    for (int dy = 1; dy <= trunkH; ++dy)
                        place(ax, topY + dy, az, kWoodBlock, false);
                    for (int dy = trunkH - 2; dy <= trunkH + 1; ++dy)
                    {
                        const int r = dy >= trunkH ? 1 : 2;
                        for (int oz = -r; oz <= r; ++oz)
                        {
                            for (int ox = -r; ox <= r; ++ox)
                            {
                                if (r == 2 && std::abs(ox) == 2 && std::abs(oz) == 2)
                                    continue; // trim corners for a rounded canopy
                                place(ax + ox, topY + dy, az + oz, kLeavesBlock, true);
                            }
                        }
                    }
                }
                else if (id == 2) // rock: stone half-dome sitting on the surface
                {
                    const int r = 1 + (int)((hash >> 4) % 2u);
                    for (int dy = 0; dy <= r; ++dy)
                        for (int oz = -r; oz <= r; ++oz)
                            for (int ox = -r; ox <= r; ++ox)
                                if (ox * ox + dy * dy + oz * oz <= r * r)
                                    place(ax + ox, topY + dy, az + oz, kStoneBlock, false);
                }
                else if (id == 3) // road: pave the surface block (route drawn walkable in the map)
                    place(ax, topY, az, kRoadBlock, false);
                else if (id == 4) // olive: short trunk + small silvery rounded canopy
                {
                    const int trunkH = 3 + (int)(hash % 2u);
                    for (int dy = 1; dy <= trunkH; ++dy)
                        place(ax, topY + dy, az, kWoodBlock, false);
                    for (int dy = trunkH - 1; dy <= trunkH + 1; ++dy)
                    {
                        const int r = dy > trunkH ? 1 : 2;
                        for (int oz = -r; oz <= r; ++oz)
                            for (int ox = -r; ox <= r; ++ox)
                                if (ox * ox + oz * oz <= r * r + 1)
                                    place(ax + ox, topY + dy, az + oz, kOliveLeavesBlock, true);
                    }
                }
                else if (id == 5) // cypress: tall narrow dark column
                {
                    const int trunkH = 7 + (int)(hash % 4u);
                    for (int dy = 1; dy <= trunkH; ++dy)
                        place(ax, topY + dy, az, kWoodBlock, false);
                    for (int dy = 2; dy <= trunkH + 1; ++dy)
                    {
                        const int r = dy >= trunkH ? 0 : 1;
                        for (int oz = -r; oz <= r; ++oz)
                            for (int ox = -r; ox <= r; ++ox)
                                place(ax + ox, topY + dy, az + oz, kCypressLeavesBlock, true);
                    }
                }
                else if (id >= kBlockPaintBase) // surface paint: replace the top block with block (id - base)
                    place(ax, topY, az, static_cast<BlockId>(id - kBlockPaintBase), false);
            }
        }
    }
} // namespace pe::voxel
