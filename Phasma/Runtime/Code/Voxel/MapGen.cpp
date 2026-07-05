#include "Voxel/MapGen.h"
#include "Voxel/ChunkColumn.h"
#include "Voxel/HeightMap.h"        // PH16 signed-half-float surface map format
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

    bool MapImage::Load(const std::string &configured, const char *what, bool signedFloat)
    {
        if (configured.empty())
            return false;
        const std::filesystem::path path = ColumnChunkStore::ResolveRoot(configured);

        if (signedFloat)
        {
            // Surface height map: a PH16 half-float blob ([-1,1]), or a legacy 8-bit image whose
            // gray 0..255 is remapped to [-1,1]. Either way the surface is held as float in pxf.
            std::ifstream in(path, std::ios::binary | std::ios::ate);
            if (in)
            {
                const std::streamsize size = in.tellg();
                in.seekg(0);
                std::vector<uint8_t> bytes(static_cast<size_t>(std::max<std::streamsize>(0, size)));
                if (!bytes.empty())
                    in.read(reinterpret_cast<char *>(bytes.data()), size);
                if (DecodeHeightMapF16(bytes.data(), bytes.size(), w, h, pxf))
                {
                    isFloat = true;
                    return true;
                }
            }
            int channels = 0;
            stbi_uc *data = stbi_load(path.string().c_str(), &w, &h, &channels, 1);
            if (!data)
            {
                PE_WARN("MapGen: cannot load %s map '%s' (%s)", what, path.string().c_str(), stbi_failure_reason());
                w = h = 0;
                return false;
            }
            const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
            pxf.resize(n);
            for (size_t i = 0; i < n; ++i)
                pxf[i] = static_cast<float>(data[i]) / 255.0f * 2.0f - 1.0f;
            stbi_image_free(data);
            isFloat = true;
            return true;
        }

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
        isFloat = false;
        return true;
    }

    float MapImage::SampleNorm(float nu, float nv) const
    {
        const float u = std::clamp(nu * (float)w - 0.5f, 0.0f, (float)(w - 1));
        const float v = std::clamp(nv * (float)h - 0.5f, 0.0f, (float)(h - 1));
        const int x0 = (int)u;
        const int y0 = (int)v;
        const int x1 = std::min(x0 + 1, w - 1);
        const int y1 = std::min(y0 + 1, h - 1);
        const float fx = u - (float)x0;
        const float fy = v - (float)y0;
        const auto at = [&](int x, int y) -> float
        { return isFloat ? pxf[y * w + x] : (float)px[y * w + x]; };
        const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * fx;
        const float bot = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * fx;
        return top + (bot - top) * fy;
    }

    float MapGen::MapHeight(float v) const
    {
        // -1 -> 0.0, 0 -> 0.5 (= groundHeight for a symmetric range), +1 -> 1.0
        const float t = (std::clamp(v, -1.0f, 1.0f) + 1.0f) * 0.5f;
        return m_groundHeight + m_heightMin + t * (m_heightMax - m_heightMin);
    }

    MapGen::MapGen(const VoxelConfig &cfg)
    {
        m_surface.Load(cfg.heightmapPath, "surface height", true); // signed [-1,1] height scaler
        m_strata1.Load(cfg.strata1Path, "strata 1 thickness", false);
        m_strata2.Load(cfg.strata2Path, "strata 2 thickness", false);
        m_features.Load(cfg.featuresPath, "features", false);
        m_blocksPerPixel = std::max(1, cfg.blocksPerPixel);
        // Terrain passes a float metres/pixel; the cube path leaves it 0 and gets the integer bpp.
        m_metersPerPixel = cfg.surfaceMetersPerPixel > 0.0f ? cfg.surfaceMetersPerPixel : (float)m_blocksPerPixel;
        m_heightMin = cfg.heightMin;
        m_heightMax = cfg.heightMax;
        m_groundHeight = cfg.groundHeight;
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
                // Surface value is the signed [-1,1] scaler; MapHeight turns it into a block height.
                const int hgt = std::clamp((int)std::lround(MapHeight(m_surface.SampleNorm(nu, nv))), 1, kWorldHeight);
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

    float MapGen::SurfaceHeight(float x, float z) const
    {
        if (!m_surface.Valid())
            return 0.0f;
        const float extentX = (float)m_surface.w * m_metersPerPixel;
        const float extentZ = (float)m_surface.h * m_metersPerPixel;
        // Both axes flip vs the raw world->uv so the terrain matches the painted map (image col 0 = +X,
        // row 0 = +Z) instead of mirroring. Terrain-only: the cube Generate path keeps its own mapping.
        const float nu = 0.5f - (x - (float)m_centerWX) / extentX;
        const float nv = 0.5f - (z - (float)m_centerWZ) / extentZ;
        // The surface map is a signed [-1,1] height scaler; MapHeight maps it into [heightMin,
        // heightMax] metres around groundHeight (0 = ground), so the terrain can dip below y=0.
        return MapHeight(m_surface.SampleNorm(nu, nv)); // SampleNorm edge-clamps uv at the border
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
                const int hgt = std::clamp((int)std::lround(MapHeight(m_surface.SampleNorm(nu, nv))), 1, kWorldHeight);
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
