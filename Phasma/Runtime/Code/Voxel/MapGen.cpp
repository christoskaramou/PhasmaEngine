#include "Voxel/MapGen.h"
#include "Voxel/ChunkColumn.h"
#include "Voxel/ColumnChunkStore.h" // ResolveRoot: Assets-relative or absolute path resolution
#include "Voxel/VoxelWorld.h"
#include "stb_image.h"

namespace pe::voxel
{
    namespace
    {
        constexpr BlockId kWaterBlock = 4;
    }

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
        m_blocksPerPixel = std::max(1, cfg.blocksPerPixel);
        m_centerWX = cfg.boundsCenterCx * kSectionDim + kSectionDim / 2;
        m_centerWZ = cfg.boundsCenterCz * kSectionDim + kSectionDim / 2;
        m_seaLevel = cfg.seaLevel < 0 ? cfg.groundY - 2 : cfg.seaLevel;
        m_surfaceBlock = static_cast<BlockId>(std::max(0, cfg.surfaceBlock));
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
                                b = m_surfaceBlock;
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
    }
} // namespace pe::voxel
