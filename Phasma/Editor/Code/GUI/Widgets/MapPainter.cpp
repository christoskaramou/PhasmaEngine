#include "MapPainter.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Sampler.h"
#include "Base/Path.h"     // RuntimeAssets: the block tile PNGs for the palette thumbnails
#include "Camera/Camera.h" // Alt+LMB camera teleport
#include "GUI/GUI.h"
#include "GUI/Helpers.h"
#include "Phasma/MCP/Utils.h" // EncodeRGBA_PNG: same encoder the screenshot tools use
#include "ECS/Context.h"      // GetGlobalSystem: live scatter push into the TerrainWorld
#include "Scene/NodeComponents.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Terrain/TerrainSystem.h"
#include "Voxel/ColumnChunkStore.h" // ResolveRoot: Assets-relative or absolute map paths
#include "Voxel/HeightMap.h"        // PH16 signed-half-float surface map format
#include "imgui/imgui.h"
#include "stb_image.h"

namespace pe
{
    namespace
    {
        constexpr const char *kLayerNames[6] = {"Surface Height", "Strata 1 Thickness", "Strata 2 Thickness",
                                                "Features", "Caves (Terrain)", "Scatter (Terrain)"};
        // Keep the Terrain node's bounded extent in sync with the painted heightmap dimensions. No-op
        // unless painting the surface-height layer (0); the pointers are null for non-terrain targets.
        void SyncTerrainExtentFromMap(int layer, float *metersPerPixel, int *sizeXMeters, int *sizeZMeters, int w, int h)
        {
            if (layer != 0 || !metersPerPixel || !sizeXMeters || !sizeZMeters || w <= 0 || h <= 0)
                return;
            const float mpp = std::max(0.05f, *metersPerPixel);
            *sizeXMeters = std::max(1, static_cast<int>(std::lround(w * mpp)));
            *sizeZMeters = std::max(1, static_cast<int>(std::lround(h * mpp)));
        }

        constexpr const char *kDefaultPaths[6] = {"Maps/heightmap.png", "Maps/strata1.png", "Maps/strata2.png",
                                                  "Maps/features.png", "Maps/caves.png", "Maps/scatter.png"};
        // Scatter-kind preview dot colours, cycled by 1-based kind id.
        constexpr uint8_t kScatterKindColors[8][3] = {{40, 220, 40}, {210, 210, 210}, {150, 220, 90}, {200, 150, 90}, {90, 170, 220}, {220, 120, 200}, {230, 210, 90}, {160, 110, 220}};
        constexpr const char *kFeatureNames[6] = {"Tree", "Rock", "Olive", "Cypress", "Block", "Erase"};
        constexpr uint8_t kScatterId[4] = {1, 2, 4, 5}; // Tree, Rock, Olive, Cypress map pixel values
        constexpr uint8_t kBlockPaintBase = 64;         // painted block = kBlockPaintBase + blockId

        // The paintable blocks and their atlas tile (mirrors VoxelWorld::RegisterDefaultBlocks +
        // VoxelMaterial::Build). ponytail: a small hand-kept table beats plumbing the live registry
        // + atlas filenames into the editor widget; add a row when a block is added.
        struct PaletteEntry
        {
            const char *name;
            int block;
            const char *tile;
        };
        constexpr PaletteEntry kPalette[] = {
            {"Grass", 3, "grass"},
            {"Dirt", 2, "dirt"},
            {"Stone", 1, "stone"},
            {"Sand", 8, "sand"},
            {"Dry Grass", 9, "dry_grass"},
            {"Rock", 10, "rock"},
            {"Snow", 11, "snow"},
            {"Gravel", 12, "gravel"},
            {"Cobblestone", 7, "cobblestone"},
            {"Marble", 13, "marble"},
            {"Limestone", 14, "limestone"},
            {"Terracotta", 15, "terracotta"},
            {"Whitewash", 16, "whitewash"},
            {"Blue Plaster", 17, "blue_plaster"},
            {"Roof Tile", 18, "roof_tile"},
            {"Column", 19, "marble_column"},
            {"Wood", 5, "wood"},
            {"Leaves", 6, "leaves"},
            {"Olive Leaves", 20, "olive_leaves"},
            {"Cypress", 21, "cypress_leaves"},
            {"Water", 4, "water"},
        };
        constexpr int kPaletteCount = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));

        uint8_t ToU8(float v)
        {
            return static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(v)), 0, 255));
        }

        // Grayscale ([0,255] float) -> RGBA for the shared PNG encoder; stbi_load(..., 1) reads it back
        // as the identical gray value (equal channels -> luma == value).
        std::vector<uint8_t> GrayToRgba(const std::vector<float> &gray)
        {
            std::vector<uint8_t> rgba(gray.size() * 4);
            for (size_t i = 0; i < gray.size(); ++i)
            {
                const uint8_t g = ToU8(gray[i]);
                rgba[i * 4 + 0] = g;
                rgba[i * 4 + 1] = g;
                rgba[i * 4 + 2] = g;
                rgba[i * 4 + 3] = 255;
            }
            return rgba;
        }

        // Surface height map: the painter's float buffer works in a [0,255] value domain (so brush
        // strength/set numbers match the other layers) but never snaps to integers, so it keeps the
        // file's half-float precision. These map between that domain and the stored signed [-1,1]
        // scaler (0 = ground) at the load/save/UI edges.
        float SurfSignedToRange(float v)
        {
            return std::clamp((v * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f);
        }
        float SurfRangeToSigned(float u)
        {
            return u / 255.0f * 2.0f - 1.0f;
        }

        uint32_t FeatureHash(int x, int z)
        {
            return static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(z) * 19349663u;
        }

        int FloorDiv(int a, int b)
        {
            return (a >= 0 ? a : a - b + 1) / b;
        }
    } // namespace

    MapPainter::MapPainter() : Widget("Map Painter")
    {
        m_open = false;
        snprintf(m_newPath.data(), m_newPath.size(), "%s", kDefaultPaths[0]);
    }

    MapPainter::~MapPainter()
    {
        ReleasePreview();
        ReleasePalette();
    }

    MapPainter::MapTarget MapPainter::ResolveTarget(int layer) const
    {
        Scene *scene = GetActiveScene();
        MapTarget t;
        if (!scene)
            return t;
        // Height layer: a Terrain node owns it when present (surface = terrain height). The Caves and
        // Scatter layers are Terrain-only (heightfield cube worlds have their own worldgen caves
        // toggle, and the voxel Features layer covers cube decorations).
        if (layer == 0 || layer == kCavesLayer || layer == kScatterLayer)
        {
            if (NodeId *tn = scene->GetTerrainNode())
                if (NodeTerrainTag *tt = scene->GetTerrainForNode(tn))
                {
                    t.path = layer == kCavesLayer     ? &tt->cavesPath
                             : layer == kScatterLayer ? &tt->scatterPath
                                                      : &tt->heightmapPath;
                    t.metersPerPixel = &tt->metersPerPixel;
                    t.scatterMeshes = &tt->scatterMeshes;
                    t.sizeXMeters = &tt->sizeXMeters;
                    t.sizeZMeters = &tt->sizeZMeters;
                    t.rebuild = &tt->rebuildRequested;
                    t.heightMin = &tt->heightMin;
                    t.heightMax = &tt->heightMax;
                    t.groundHeight = &tt->groundHeight;
                    t.node = tn;
                    t.bounded = true;     // terrain is always bounded/static
                    t.terrainFlip = true; // Terrain maps col 0 -> +X, row 0 -> +Z
                    return t;
                }
            if (layer == kCavesLayer || layer == kScatterLayer)
                return t; // no Terrain node -> no target
        }
        // Strata / features (and the height layer's fallback) live on the Voxel World node.
        NodeId *vn = scene->GetVoxelWorldNode();
        NodeVoxelWorldTag *vt = vn ? scene->GetVoxelWorldForNode(vn) : nullptr;
        if (!vt)
            return t;
        t.node = vn;
        t.blocksPerPixel = &vt->blocksPerPixel;
        t.rebuild = &vt->rebuildRequested;
        t.heightMin = &vt->heightMin;
        t.heightMax = &vt->heightMax;
        t.groundHeight = &vt->groundHeight;
        t.bounded = (vt->worldRadius > 0 || !vt->streaming);
        switch (layer)
        {
        case 1:
            t.path = &vt->strata1Path;
            break;
        case 2:
            t.path = &vt->strata2Path;
            break;
        case kFeaturesLayer:
            t.path = &vt->featuresPath;
            break;
        default:
            t.path = &vt->heightmapPath;
            break;
        }
        return t;
    }

    bool MapPainter::SetLayer(int layer)
    {
        const int clamped = std::clamp(layer, 0, kLayerCount - 1);
        if (clamped != m_layer)
        {
            m_layer = clamped;
            m_brushType = 0;
            m_zoom = 1.0f;
            m_panX = m_panY = 0.0f; // re-fit the view when the layer changes
            snprintf(m_newPath.data(), m_newPath.size(), "%s", kDefaultPaths[m_layer]);
            m_textureDirty = true;
            m_haveLastStamp = false;
        }
        return true;
    }

    void MapPainter::SyncLayer(int layer)
    {
        const MapTarget t = ResolveTarget(layer);
        const std::string configured = t.valid() ? *t.path : std::string();
        const std::string resolved =
            configured.empty() ? std::string() : voxel::ColumnChunkStore::ResolveRoot(configured).string();
        LayerBuffer &buf = m_layers[layer];
        if (resolved == buf.loadedPath)
            return;

        buf = {}; // path changed in the inspector: drop any edits and reload
        buf.loadedPath = resolved;
        if (layer == m_layer)
            m_haveLastStamp = false;
        m_textureDirty = true;
        if (resolved.empty())
            return;

        int w = 0, h = 0;
        bool loaded = false;
        if (layer == 0) // surface height map: signed [-1,1] PH16 half-float -> the 0..255 buffer
        {
            std::ifstream in(resolved, std::ios::binary | std::ios::ate);
            if (in)
            {
                const std::streamsize size = in.tellg();
                in.seekg(0);
                std::vector<uint8_t> bytes(static_cast<size_t>(std::max<std::streamsize>(0, size)));
                if (!bytes.empty())
                    in.read(reinterpret_cast<char *>(bytes.data()), size);
                std::vector<float> pxf;
                if (voxel::DecodeHeightMapF16(bytes.data(), bytes.size(), w, h, pxf))
                {
                    buf.w = w;
                    buf.h = h;
                    buf.px.resize(pxf.size());
                    for (size_t i = 0; i < pxf.size(); ++i)
                        buf.px[i] = SurfSignedToRange(pxf[i]);
                    loaded = true;
                }
            }
        }
        if (!loaded) // legacy 8-bit image (surface: gray remaps to [-1,1] on save) / strata / features
        {
            int channels = 0;
            if (stbi_uc *data = stbi_load(resolved.c_str(), &w, &h, &channels, 1))
            {
                buf.w = w;
                buf.h = h;
                const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
                buf.px.resize(n);
                for (size_t i = 0; i < n; ++i)
                    buf.px[i] = static_cast<float>(data[i]);
                stbi_image_free(data);
                loaded = true;
            }
        }
        if (loaded && layer == m_layer)
        {
            m_newW = w;
            m_newH = h;
        }
    }

    void MapPainter::CreateMap()
    {
        const MapTarget t = ResolveTarget(m_layer);
        if (!t.valid())
            return;
        std::string &slot = *t.path;
        if (slot.empty())
        {
            slot = m_newPath.data();
            if (Scene *scene = GetActiveScene())
                scene->MarkDirty();
        }
        LayerBuffer &buf = Buf();
        buf.loadedPath = voxel::ColumnChunkStore::ResolveRoot(slot).string();
        buf.w = std::clamp(m_newW, 16, 2048);
        buf.h = std::clamp(m_newH, 16, 2048);
        // Features/caves/scatter maps start empty (0 = nothing there); a gray fill is garbage.
        const float fill =
            (OnFeatures() || m_layer == kCavesLayer || OnScatter()) ? 0.0f : std::clamp(m_newValue, 0.0f, 255.0f);
        buf.px.assign(static_cast<size_t>(buf.w) * static_cast<size_t>(buf.h), fill);
        buf.unsaved = true;
        m_textureDirty = true;
        SyncTerrainExtentFromMap(m_layer, t.metersPerPixel, t.sizeXMeters, t.sizeZMeters, buf.w, buf.h);
        Save(); // write it out right away so MapGen can load it on the rebuild
    }

    void MapPainter::ResizeMap()
    {
        LayerBuffer &buf = Buf();
        const int newW = std::clamp(m_newW, 16, 2048);
        const int newH = std::clamp(m_newH, 16, 2048);
        if (buf.px.empty() || (newW == buf.w && newH == buf.h))
            return;

        std::vector<float> out(static_cast<size_t>(newW) * static_cast<size_t>(newH));
        for (int y = 0; y < newH; ++y)
        {
            for (int x = 0; x < newW; ++x)
            {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(newW);
                const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(newH);
                if (OnFeatures() || OnScatter())
                {
                    // Nearest: feature/kind ids are discrete, interpolating them invents garbage.
                    const int sx = std::clamp(static_cast<int>(u * static_cast<float>(buf.w)), 0, buf.w - 1);
                    const int sy = std::clamp(static_cast<int>(v * static_cast<float>(buf.h)), 0, buf.h - 1);
                    out[y * newW + x] = buf.px[sy * buf.w + sx];
                }
                else
                {
                    const float fx = std::clamp(u * static_cast<float>(buf.w) - 0.5f, 0.0f, static_cast<float>(buf.w - 1));
                    const float fy = std::clamp(v * static_cast<float>(buf.h) - 0.5f, 0.0f, static_cast<float>(buf.h - 1));
                    const int x0 = static_cast<int>(fx);
                    const int y0 = static_cast<int>(fy);
                    const int x1 = std::min(x0 + 1, buf.w - 1);
                    const int y1 = std::min(y0 + 1, buf.h - 1);
                    const float tx = fx - static_cast<float>(x0);
                    const float ty = fy - static_cast<float>(y0);
                    const float top = buf.px[y0 * buf.w + x0] * (1.0f - tx) + buf.px[y0 * buf.w + x1] * tx;
                    const float bot = buf.px[y1 * buf.w + x0] * (1.0f - tx) + buf.px[y1 * buf.w + x1] * tx;
                    out[y * newW + x] = top + (bot - top) * ty;
                }
            }
        }
        buf.w = newW;
        buf.h = newH;
        buf.px = std::move(out);
        buf.unsaved = true;
        m_textureDirty = true;
        m_haveLastStamp = false;
        const MapTarget t = ResolveTarget(m_layer);
        SyncTerrainExtentFromMap(m_layer, t.metersPerPixel, t.sizeXMeters, t.sizeZMeters, buf.w, buf.h);
    }

    void MapPainter::StampBrush(float px, float py, float radius, float strength, bool lower, Brush brush, float value)
    {
        LayerBuffer &buf = Buf();
        const float r = std::max(0.5f, radius);
        const int x0 = std::max(0, static_cast<int>(std::floor(px - r)));
        const int x1 = std::min(buf.w - 1, static_cast<int>(std::ceil(px + r)));
        const int y0 = std::max(0, static_cast<int>(std::floor(py - r)));
        const int y1 = std::min(buf.h - 1, static_cast<int>(std::ceil(py + r)));
        if (x0 > x1 || y0 > y1)
            return;

        // Blend brushes read neighbors/originals, so work from a snapshot of the pre-stamp pixels.
        std::vector<float> before;
        if (brush == Brush::Smooth)
            before = buf.px;
        const auto sample = [&](int x, int y) -> float
        {
            x = std::clamp(x, 0, buf.w - 1);
            y = std::clamp(y, 0, buf.h - 1);
            return before[y * buf.w + x];
        };

        const float blend = std::clamp(strength / 64.0f, 0.0f, 1.0f); // strength 64 = full effect per stamp
        for (int y = y0; y <= y1; ++y)
        {
            for (int x = x0; x <= x1; ++x)
            {
                const float dx = static_cast<float>(x) - px;
                const float dy = static_cast<float>(y) - py;
                const float d = std::sqrt(dx * dx + dy * dy);
                if (d > r)
                    continue;
                const float falloff = 1.0f - d / r; // linear falloff to the rim
                float &v = buf.px[y * buf.w + x];
                float painted = v;
                switch (brush)
                {
                case Brush::Smooth:
                {
                    // 5x5 mean: on an already-smooth map a 3x3 mean barely differs from the center,
                    // so there is nothing to converge to; a wider kernel gives a real target.
                    float avg = 0.0f;
                    for (int oy = -2; oy <= 2; ++oy)
                        for (int ox = -2; ox <= 2; ++ox)
                            avg += sample(x + ox, y + oy);
                    const float smoothBlend = std::clamp(strength / 16.0f, 0.0f, 1.0f);
                    painted += (avg / 25.0f - painted) * smoothBlend * falloff;
                    break;
                }
                case Brush::Flatten:
                    painted += (m_flattenTarget - painted) * blend * falloff;
                    break;
                case Brush::Set:
                    painted += (value - painted) * blend * falloff;
                    break;
                case Brush::Raise:
                default:
                    painted += (lower ? -strength : strength) * falloff;
                    break;
                }
                v = std::clamp(painted, 0.0f, 255.0f);
            }
        }
    }

    void MapPainter::StampFeatures(float px, float py, float radius, FeatureStamp stamp)
    {
        LayerBuffer &buf = Buf();
        const float r = std::max(0.5f, radius);

        // Erase (0) and Block (solid paint) both fill the whole disk, not a jittered scatter.
        if (stamp == FeatureStamp::Erase || stamp == FeatureStamp::Block)
        {
            const uint8_t fill = stamp == FeatureStamp::Erase
                                     ? 0
                                     : static_cast<uint8_t>(kBlockPaintBase + std::clamp(m_paintBlock, 0, 191));
            const int x0 = std::max(0, static_cast<int>(std::floor(px - r)));
            const int x1 = std::min(buf.w - 1, static_cast<int>(std::ceil(px + r)));
            const int y0 = std::max(0, static_cast<int>(std::floor(py - r)));
            const int y1 = std::min(buf.h - 1, static_cast<int>(std::ceil(py + r)));
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x)
                    if ((static_cast<float>(x) - px) * (static_cast<float>(x) - px) +
                            (static_cast<float>(y) - py) * (static_cast<float>(y) - py) <=
                        r * r)
                        buf.px[y * buf.w + x] = static_cast<float>(fill);
            return;
        }

        // Jittered grid scatter: one deterministic candidate per spacing cell, so dragging over
        // the same area never densifies — the same cells always produce the same dots.
        const uint8_t id = kScatterId[std::clamp(static_cast<int>(stamp), 0, 3)];
        const int sp = std::max(2, m_featureSpacing);
        const int cx0 = FloorDiv(static_cast<int>(std::floor(px - r)), sp);
        const int cx1 = FloorDiv(static_cast<int>(std::ceil(px + r)), sp);
        const int cy0 = FloorDiv(static_cast<int>(std::floor(py - r)), sp);
        const int cy1 = FloorDiv(static_cast<int>(std::ceil(py + r)), sp);
        for (int cy = cy0; cy <= cy1; ++cy)
        {
            for (int cx = cx0; cx <= cx1; ++cx)
            {
                const uint32_t h = FeatureHash(cx, cy);
                const int jx = cx * sp + static_cast<int>(h % static_cast<uint32_t>(sp));
                const int jy = cy * sp + static_cast<int>((h >> 8) % static_cast<uint32_t>(sp));
                if (jx < 0 || jx >= buf.w || jy < 0 || jy >= buf.h)
                    continue;
                const float dx = static_cast<float>(jx) - px;
                const float dy = static_cast<float>(jy) - py;
                if (dx * dx + dy * dy > r * r)
                    continue;
                buf.px[jy * buf.w + jx] = static_cast<float>(id);
            }
        }
    }

    void MapPainter::StampScatter(LayerBuffer &buf, float px, float py, float radius, int kindId)
    {
        const float r = std::max(0.5f, radius);
        if (kindId <= 0) // erase: clear the disk
        {
            const int x0 = std::max(0, static_cast<int>(std::floor(px - r)));
            const int x1 = std::min(buf.w - 1, static_cast<int>(std::ceil(px + r)));
            const int y0 = std::max(0, static_cast<int>(std::floor(py - r)));
            const int y1 = std::min(buf.h - 1, static_cast<int>(std::ceil(py + r)));
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x)
                    if ((static_cast<float>(x) - px) * (static_cast<float>(x) - px) +
                            (static_cast<float>(y) - py) * (static_cast<float>(y) - py) <=
                        r * r)
                        buf.px[y * buf.w + x] = 0.0f;
            return;
        }
        // Same idempotent jittered-grid scatter as the Features layer: dragging never densifies.
        const int sp = std::max(2, m_featureSpacing);
        const int cx0 = FloorDiv(static_cast<int>(std::floor(px - r)), sp);
        const int cx1 = FloorDiv(static_cast<int>(std::ceil(px + r)), sp);
        const int cy0 = FloorDiv(static_cast<int>(std::floor(py - r)), sp);
        const int cy1 = FloorDiv(static_cast<int>(std::ceil(py + r)), sp);
        for (int cy = cy0; cy <= cy1; ++cy)
            for (int cx = cx0; cx <= cx1; ++cx)
            {
                const uint32_t h = FeatureHash(cx, cy);
                const int jx = cx * sp + static_cast<int>(h % static_cast<uint32_t>(sp));
                const int jy = cy * sp + static_cast<int>((h >> 8) % static_cast<uint32_t>(sp));
                if (jx < 0 || jx >= buf.w || jy < 0 || jy >= buf.h)
                    continue;
                const float dx = static_cast<float>(jx) - px;
                const float dy = static_cast<float>(jy) - py;
                if (dx * dx + dy * dy > r * r)
                    continue;
                buf.px[jy * buf.w + jx] = static_cast<float>(std::clamp(kindId, 1, 255));
            }
    }

    bool MapPainter::PushScatterLive(float px0, float py0, float px1, float py1)
    {
        const MapTarget t = ResolveTarget(kScatterLayer);
        LayerBuffer &buf = m_layers[kScatterLayer];
        auto *ts = GetGlobalSystem<terrain::TerrainSystem>();
        if (!t.valid() || buf.px.empty() || !ts || !ts->World())
            return false;
        std::vector<uint8_t> px8(buf.px.size());
        for (size_t i = 0; i < buf.px.size(); ++i)
            px8[i] = ToU8(buf.px[i]);
        // Stamped pixel rect -> world rect through the flipped terrain mapping (either corner order —
        // the world rect just min/maxes out).
        const float mpp = std::max(0.05f, t.ppScale());
        int centerCx = 0, centerCz = 0;
        if (Scene *scene = GetActiveScene(); scene && t.node)
        {
            const vec3 p = vec3(scene->GetWorldMatrix(t.node)[3]);
            centerCx = FloorDiv(static_cast<int>(std::floor(p.x)), 16);
            centerCz = FloorDiv(static_cast<int>(std::floor(p.z)), 16);
        }
        const auto worldX = [&](float px)
        {
            const float pu = static_cast<float>(buf.w) - 1.0f - px; // terrainFlip
            return (centerCx * 16 + 8) + (pu + 0.5f - static_cast<float>(buf.w) * 0.5f) * mpp;
        };
        const auto worldZ = [&](float py)
        {
            const float pv = static_cast<float>(buf.h) - 1.0f - py;
            return (centerCz * 16 + 8) + (pv + 0.5f - static_cast<float>(buf.h) * 0.5f) * mpp;
        };
        const vec2 a(worldX(px0), worldZ(py0)), b(worldX(px1), worldZ(py1));
        return ts->World()->UpdateScatterMap(px8.data(), buf.w, buf.h, glm::min(a, b), glm::max(a, b));
    }

    bool MapPainter::ScatterStrokeWorld(float worldX, float worldZ, float radiusM, int kindId)
    {
        SyncLayer(kScatterLayer);
        const MapTarget t = ResolveTarget(kScatterLayer);
        LayerBuffer &buf = m_layers[kScatterLayer];
        Scene *scene = GetActiveScene();
        if (!t.valid() || buf.px.empty() || !scene || !t.node)
            return false;
        // World -> pixel: invert the flipped terrain mapping (see TeleportCameraTo).
        const float mpp = std::max(0.05f, t.ppScale());
        const vec3 p = vec3(scene->GetWorldMatrix(t.node)[3]);
        const int centerCx = FloorDiv(static_cast<int>(std::floor(p.x)), 16);
        const int centerCz = FloorDiv(static_cast<int>(std::floor(p.z)), 16);
        const float pu = (worldX - (centerCx * 16 + 8)) / mpp - 0.5f + static_cast<float>(buf.w) * 0.5f;
        const float pv = (worldZ - (centerCz * 16 + 8)) / mpp - 0.5f + static_cast<float>(buf.h) * 0.5f;
        const float px = static_cast<float>(buf.w) - 1.0f - pu;
        const float py = static_cast<float>(buf.h) - 1.0f - pv;
        const float r = std::max(0.5f, radiusM / mpp);
        if (px < -r || px > buf.w - 1 + r || py < -r || py > buf.h - 1 + r)
            return false; // outside the map extent
        StampScatter(buf, px, py, r, kindId);
        buf.unsaved = true;
        if (m_layer == kScatterLayer)
            m_textureDirty = true;
        PushScatterLive(px - r, py - r, px + r, py + r);
        return true;
    }

    bool MapPainter::Stroke(float u, float v, float radius, float strength, bool lower, int brush, int value)
    {
        SyncLayer(m_layer);
        LayerBuffer &buf = Buf();
        if (buf.px.empty())
            return false;
        const float px = u * static_cast<float>(buf.w) - 0.5f;
        const float py = v * static_cast<float>(buf.h) - 0.5f;
        const float r = radius > 0.0f ? radius : m_brushRadius;
        if (OnScatter())
        {
            // brush = the kind id itself (1-based; 0 erases); < 0 = the widget's combo selection.
            const MapTarget t = ResolveTarget(kScatterLayer);
            const int kinds = t.scatterMeshes ? static_cast<int>(t.scatterMeshes->size()) : 0;
            const int kind = brush >= 0 ? brush : (m_scatterKind >= kinds ? 0 : m_scatterKind + 1);
            StampScatter(buf, px, py, r, kind);
            PushScatterLive(px - r, py - r, px + r, py + r);
        }
        else if (OnFeatures())
        {
            const int stamp = std::clamp(brush < 0 ? m_brushType : brush, 0, 5);
            if (stamp == static_cast<int>(FeatureStamp::Block) && value >= 0)
                m_paintBlock = value; // programmatic route passes the block id as `value`
            StampFeatures(px, py, r, static_cast<FeatureStamp>(stamp));
        }
        else
        {
            const Brush type = static_cast<Brush>(std::clamp(brush < 0 ? m_brushType : brush, 0, 3));
            if (type == Brush::Flatten) // programmatic stamps are single: flatten to the value underneath
                m_flattenTarget = buf.px[std::clamp(static_cast<int>(py), 0, buf.h - 1) * buf.w +
                                         std::clamp(static_cast<int>(px), 0, buf.w - 1)];
            StampBrush(px, py, r, strength > 0.0f ? strength : m_brushStrength, lower, type,
                       std::clamp(value < 0 ? m_setValue : static_cast<float>(value), 0.0f, 255.0f));
        }
        buf.unsaved = true;
        m_textureDirty = true;
        return true;
    }

    bool MapPainter::Save()
    {
        const MapTarget t = ResolveTarget(m_layer);
        LayerBuffer &buf = Buf();
        if (!t.valid() || buf.px.empty() || buf.loadedPath.empty())
            return false;

        std::vector<uint8_t> blob;
        if (m_layer == 0)
        {
            // Surface height map: [0,255] float buffer -> signed [-1,1] half-float (PH16).
            std::vector<float> pxf(buf.px.size());
            for (size_t i = 0; i < buf.px.size(); ++i)
                pxf[i] = SurfRangeToSigned(buf.px[i]);
            blob = voxel::EncodeHeightMapF16(pxf.data(), buf.w, buf.h);
        }
        else
        {
            const std::vector<uint8_t> rgba = GrayToRgba(buf.px);
            blob = pmcp::EncodeRGBA_PNG(rgba.data(), buf.w, buf.h);
        }
        if (blob.empty())
            return false;

        const std::filesystem::path path(buf.loadedPath);
        std::error_code ec;
        if (path.has_parent_path())
            std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream out(path, std::ios::binary);
        if (!out)
        {
            PE_WARN("MapPainter: cannot write '%s'", buf.loadedPath.c_str());
            return false;
        }
        out.write(reinterpret_cast<const char *>(blob.data()), static_cast<std::streamsize>(blob.size()));
        out.close();

        buf.unsaved = false;
        // Scatter edits apply live through UpdateScatterMap — saving is just persistence, no rebuild
        // hitch. Fall back to the rebuild flag when there is no live world (or no templates yet).
        if (OnScatter() && PushScatterLive(0.0f, 0.0f, static_cast<float>(buf.w), static_cast<float>(buf.h)))
            return true;
        SyncTerrainExtentFromMap(m_layer, t.metersPerPixel, t.sizeXMeters, t.sizeZMeters, buf.w, buf.h);
        if (t.rebuild)
            *t.rebuild = true; // same path as the inspector "Rebuild" button
        return true;
    }

    void MapPainter::UploadPreview()
    {
        LayerBuffer &buf = Buf();
        if (!m_textureDirty || buf.px.empty())
            return;
        m_textureDirty = false;

        Queue *queue = RHII.GetMainQueue();
        if (!queue)
            return;

        const bool recreate = !m_image || static_cast<int>(m_image->GetWidth()) != buf.w ||
                              static_cast<int>(m_image->GetHeight()) != buf.h;
        if (recreate)
            ReleasePreview();

        std::vector<uint8_t> rgba;
        if (OnFeatures())
        {
            // Features read badly as near-black gray ids: show them as colored dots over a dimmed
            // surface-height underlay so you can see where on the terrain you plant.
            const LayerBuffer &surf = m_layers[0];
            rgba.resize(buf.px.size() * 4);
            for (int y = 0; y < buf.h; ++y)
            {
                for (int x = 0; x < buf.w; ++x)
                {
                    const size_t i = static_cast<size_t>(y) * buf.w + x;
                    uint8_t rr = 20, gg = 20, bb = 20;
                    if (!surf.px.empty())
                    {
                        const int sx = std::clamp(x * surf.w / buf.w, 0, surf.w - 1);
                        const int sy = std::clamp(y * surf.h / buf.h, 0, surf.h - 1);
                        rr = gg = bb = ToU8(surf.px[sy * surf.w + sx] * 0.5f);
                    }
                    const int fv = static_cast<int>(std::lround(buf.px[i]));
                    if (fv == 1)
                        rr = 40, gg = 220, bb = 40; // tree
                    else if (fv == 2)
                        rr = gg = bb = 210; // rock
                    else if (fv == 4)
                        rr = 120, gg = 160, bb = 90; // olive
                    else if (fv == 5)
                        rr = 20, gg = 90, bb = 30; // cypress
                    else if (fv >= kBlockPaintBase)
                    {
                        // painted block: tint by its tile's average color (palette loaded in Update)
                        const int b = fv - kBlockPaintBase;
                        for (int pi = 0; pi < kPaletteCount && pi < static_cast<int>(m_palette.size()); ++pi)
                            if (kPalette[pi].block == b)
                            {
                                rr = m_palette[pi].r, gg = m_palette[pi].g, bb = m_palette[pi].b;
                                break;
                            }
                    }
                    rgba[i * 4 + 0] = rr;
                    rgba[i * 4 + 1] = gg;
                    rgba[i * 4 + 2] = bb;
                    rgba[i * 4 + 3] = 255;
                }
            }
        }
        else if (OnScatter())
        {
            // Kind ids read as near-black gray: show coloured dots over a dimmed surface underlay.
            const LayerBuffer &surf = m_layers[0];
            rgba.resize(buf.px.size() * 4);
            for (int y = 0; y < buf.h; ++y)
                for (int x = 0; x < buf.w; ++x)
                {
                    const size_t i = static_cast<size_t>(y) * buf.w + x;
                    uint8_t rr = 20, gg = 20, bb = 20;
                    if (!surf.px.empty())
                    {
                        const int sx = std::clamp(x * surf.w / buf.w, 0, surf.w - 1);
                        const int sy = std::clamp(y * surf.h / buf.h, 0, surf.h - 1);
                        rr = gg = bb = ToU8(surf.px[sy * surf.w + sx] * 0.5f);
                    }
                    const int kv = static_cast<int>(std::lround(buf.px[i]));
                    if (kv > 0)
                    {
                        const uint8_t *c = kScatterKindColors[(kv - 1) & 7];
                        rr = c[0], gg = c[1], bb = c[2];
                    }
                    rgba[i * 4 + 0] = rr;
                    rgba[i * 4 + 1] = gg;
                    rgba[i * 4 + 2] = bb;
                    rgba[i * 4 + 3] = 255;
                }
        }
        else
        {
            rgba = GrayToRgba(buf.px);
        }

        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();
        if (recreate)
        {
            ImageDesc desc{};
            desc.format = PE_FORMAT_R8G8B8A8_UNORM;
            desc.width = static_cast<uint32_t>(buf.w);
            desc.height = static_cast<uint32_t>(buf.h);
            desc.mipLevels = 1;
            desc.usage = PE_IMAGE_USAGE_TRANSFER_DST | PE_IMAGE_USAGE_SAMPLED;
            desc.initialLayout = PE_IMAGE_LAYOUT_UNDEFINED;
            desc.name = "MapPainter_preview";
            m_image = Image::Create(desc);
            m_image->CreateSRV(PE_IMAGE_VIEW_TYPE_2D);
            SamplerDesc samplerInfo{};
            samplerInfo.magFilter = PE_FILTER_NEAREST; // crisp map pixels when zoomed in
            samplerInfo.minFilter = PE_FILTER_NEAREST;
            samplerInfo.mipmapMode = PE_SAMPLER_MIPMAP_MODE_NEAREST;
            samplerInfo.addressModeU = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.anisotropyEnable = false;
            m_image->SetSampler(Sampler::Create(samplerInfo));
        }
        cmd->CopyDataToImageStaged(m_image, rgba.data(), rgba.size());
        ImageBarrierInfo toRead{};
        toRead.image = m_image;
        toRead.layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toRead.stageFlags = PE_STAGE_FRAGMENT_SHADER;
        toRead.accessMask = PE_ACCESS_SHADER_SAMPLED_READ;
        cmd->ImageBarrier(toRead);
        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait(); // ponytail: sync upload per edited frame; maps are tiny (<=2048^2)
        cmd->Return();

        if (recreate)
            m_textureId = m_gui ? m_gui->RegisterImageTexture(m_image) : nullptr;
    }

    void MapPainter::ReleasePreview()
    {
        if (m_textureId && m_gui)
            m_gui->ReleaseImageTexture(m_textureId);
        m_textureId = nullptr;
        Image::Destroy(m_image);
    }

    void MapPainter::LoadPalette()
    {
        if (!m_palette.empty())
            return;
        Queue *queue = RHII.GetMainQueue();
        if (!queue)
            return;
        m_palette.resize(kPaletteCount);
        std::vector<std::vector<uint8_t>> keep;
        keep.reserve(kPaletteCount); // stable .data() for the deferred copies until Wait()
        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();
        for (int i = 0; i < kPaletteCount; ++i)
        {
            const std::string path = Path::RuntimeAssets + "Textures/Voxel/" + kPalette[i].tile + ".png";
            int w = 0, h = 0, ch = 0;
            stbi_uc *data = stbi_load(path.c_str(), &w, &h, &ch, 4);
            if (!data)
                continue;
            uint64_t sr = 0, sg = 0, sb = 0;
            const int n = w * h;
            for (int p = 0; p < n; ++p)
            {
                sr += data[p * 4 + 0];
                sg += data[p * 4 + 1];
                sb += data[p * 4 + 2];
            }
            const int denom = std::max(1, n);
            m_palette[i].r = static_cast<uint8_t>(sr / denom);
            m_palette[i].g = static_cast<uint8_t>(sg / denom);
            m_palette[i].b = static_cast<uint8_t>(sb / denom);
            keep.emplace_back(data, data + static_cast<size_t>(n) * 4);
            stbi_image_free(data);

            ImageDesc desc{};
            desc.format = PE_FORMAT_R8G8B8A8_UNORM;
            desc.width = static_cast<uint32_t>(w);
            desc.height = static_cast<uint32_t>(h);
            desc.mipLevels = 1;
            desc.usage = PE_IMAGE_USAGE_TRANSFER_DST | PE_IMAGE_USAGE_SAMPLED;
            desc.initialLayout = PE_IMAGE_LAYOUT_UNDEFINED;
            desc.name = "MapPainter_tile";
            Image *img = Image::Create(desc);
            img->CreateSRV(PE_IMAGE_VIEW_TYPE_2D);
            SamplerDesc s{};
            s.magFilter = PE_FILTER_NEAREST;
            s.minFilter = PE_FILTER_NEAREST;
            s.mipmapMode = PE_SAMPLER_MIPMAP_MODE_NEAREST;
            s.addressModeU = s.addressModeV = s.addressModeW = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            s.anisotropyEnable = false;
            img->SetSampler(Sampler::Create(s));
            cmd->CopyDataToImageStaged(img, keep.back().data(), keep.back().size());
            ImageBarrierInfo b{};
            b.image = img;
            b.layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.stageFlags = PE_STAGE_FRAGMENT_SHADER;
            b.accessMask = PE_ACCESS_SHADER_SAMPLED_READ;
            cmd->ImageBarrier(b);
            m_palette[i].image = img;
        }
        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait(); // ponytail: one-time sync load of 21 tiny tiles when the Block picker first opens
        cmd->Return();
        for (int i = 0; i < kPaletteCount; ++i)
            if (m_palette[i].image)
                m_palette[i].tex = m_gui ? m_gui->RegisterImageTexture(m_palette[i].image) : nullptr;
    }

    void MapPainter::ReleasePalette()
    {
        for (Thumb &t : m_palette)
        {
            if (t.tex && m_gui)
                m_gui->ReleaseImageTexture(t.tex);
            Image::Destroy(t.image);
        }
        m_palette.clear();
    }

    bool MapPainter::DrawPalette()
    {
        LoadPalette();
        bool changed = false;
        const float sz = 34.0f;
        const float avail = ImGui::GetContentRegionAvail().x;
        const int perRow = std::max(1, static_cast<int>(avail / (sz + 8.0f)));
        for (int i = 0; i < kPaletteCount; ++i)
        {
            if (i % perRow != 0)
                ImGui::SameLine();
            ImGui::PushID(i);
            const bool selected = kPalette[i].block == m_paintBlock;
            if (selected)
                ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 210, 40, 255));
            bool clicked;
            if (m_palette[i].tex)
                clicked = ImGui::ImageButton("t", (ImTextureID)(intptr_t)m_palette[i].tex, ImVec2(sz, sz));
            else
                clicked = ImGui::Button(kPalette[i].name, ImVec2(sz + 8.0f, sz));
            if (selected)
                ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s (block %d)", kPalette[i].name, kPalette[i].block);
            if (clicked)
            {
                m_paintBlock = kPalette[i].block;
                changed = true;
            }
            ImGui::PopID();
        }
        return changed;
    }

    void MapPainter::TeleportCameraTo(float px, float py)
    {
        Scene *scene = GetActiveScene();
        const MapTarget t = ResolveTarget(m_layer);
        Camera *cam = scene ? scene->GetActiveCamera() : nullptr;
        if (!scene || !t.valid() || !t.node || !cam || Buf().px.empty())
            return;
        // Map center world column mirrors the reconcile + MapGen centerW: the owning node's position
        // column for bounded worlds, origin otherwise. Section size = 16.
        int centerCx = 0, centerCz = 0;
        if (t.bounded)
        {
            const vec3 p = vec3(scene->GetWorldMatrix(t.node)[3]);
            centerCx = FloorDiv(static_cast<int>(std::floor(p.x)), 16);
            centerCz = FloorDiv(static_cast<int>(std::floor(p.z)), 16);
        }
        const LayerBuffer &buf = Buf();
        const float bpp = std::max(0.05f, t.ppScale());
        // Invert MapGen's pixel<->world mapping: nu = (wx - centerW)/(w*bpp) + 0.5, pixel center nu = (px+0.5)/w.
        // Terrain flips both axes (col 0 = +X, row 0 = +Z), so its teleport must invert both or it lands mirrored.
        const float pu = t.terrainFlip ? (static_cast<float>(buf.w) - 1.0f - px) : px;
        const float pv = t.terrainFlip ? (static_cast<float>(buf.h) - 1.0f - py) : py;
        const float worldX = (centerCx * 16 + 8) + (pu + 0.5f - static_cast<float>(buf.w) * 0.5f) * bpp;
        const float worldZ = (centerCz * 16 + 8) + (pv + 0.5f - static_cast<float>(buf.h) * 0.5f) * bpp;
        const vec3 cur = cam->GetPosition();
        cam->SetPosition(vec3(worldX, cur.y, worldZ)); // keep the current height
    }

    void MapPainter::Update()
    {
        if (!m_open)
            return;

        ImGui::Begin(m_name.c_str(), &m_open);

        Scene *scene = GetActiveScene();
        if (!scene || (!scene->GetTerrainNode() && !scene->GetVoxelWorldNode()))
        {
            ImGui::TextDisabled(
                "Add a Terrain node (for surface height) or a Voxel World node (Hierarchy > Add) to paint maps.");
            ImGui::End();
            return;
        }

        int layer = m_layer;
        if (ImGui::Combo("Layer", &layer, kLayerNames, kLayerCount))
            SetLayer(layer);
        ui::ItemTooltip("Which input map the brush edits. Surface: the terrain height (a Terrain node when "
                        "present, else the Voxel World's heightmap). Strata: thickness of the band below the "
                        "surface (Voxel World). Features: sparse decoration dots (Voxel World). Caves: painted "
                        "underground voids (Terrain node; value = how open, roof stays intact). Scatter: painted "
                        "prop meshes baked into the terrain tiles (Terrain node; strokes apply live). Edits are "
                        "kept per layer until saved.");

        MapTarget target = ResolveTarget(m_layer);
        if (!target.valid())
        {
            ImGui::TextDisabled(m_layer == kFeaturesLayer  ? "The Features layer needs a Voxel World node."
                                : m_layer == kCavesLayer   ? "The Caves layer needs a Terrain node."
                                : m_layer == kScatterLayer ? "The Scatter layer needs a Terrain node."
                                : m_layer == 0             ? "Add a Terrain or Voxel World node to paint the surface."
                                                           : "The Strata layers need a Voxel World node.");
            ImGui::End();
            return;
        }

        ImGui::SetNextItemWidth(90.0f);
        // The owning node's field: a small texture can cover a big world. Changing it rebuilds through the
        // normal debounced reconcile - no save needed. Terrain uses a float metres/pixel; Voxel World an int.
        if (target.metersPerPixel)
        {
            if (ImGui::DragFloat("Meters / Pixel", target.metersPerPixel, 0.05f, 0.05f, 256.0f, "%.2f"))
            {
                *target.metersPerPixel = std::clamp(*target.metersPerPixel, 0.05f, 256.0f);
                scene->MarkDirty();
            }
            ui::ItemTooltip("World metres each map pixel / mesh cell spans. Terrain size derives from map size x this; "
                            "bigger spreads a small map over a large world with fewer verts.");
        }
        else if (target.blocksPerPixel && ImGui::DragInt("Blocks / Pixel", target.blocksPerPixel, 0.1f, 1, 64))
        {
            *target.blocksPerPixel = std::clamp(*target.blocksPerPixel, 1, 64);
            scene->MarkDirty();
        }

        SyncLayer(m_layer);
        if (OnFeatures() || OnScatter())
            SyncLayer(0); // surface underlay for the preview
        LayerBuffer &buf = Buf();
        const std::string configured = *target.path;

        if (buf.px.empty())
        {
            if (configured.empty())
            {
                ImGui::TextWrapped("No map file set for this layer.");
                ImGui::InputText("New Map Path", m_newPath.data(), m_newPath.size());
                ui::ItemTooltip("PNG path under the project's Assets folder.");
            }
            else
            {
                ImGui::TextWrapped("'%s' is missing or unreadable - create it here.", configured.c_str());
                if (ImGui::Button("Retry Load"))
                    buf.loadedPath.clear(); // next SyncLayer retries the file
            }
            ImGui::SetNextItemWidth(90.0f);
            ImGui::DragInt("Width", &m_newW, 1.0f, 16, 2048);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            ImGui::DragInt("Height", &m_newH, 1.0f, 16, 2048);
            ui::ItemTooltip("Map size in pixels; one pixel spans Meters/Pixel (Terrain) or Blocks/Pixel in X/Z.");
            const float ppN = target.ppScale();
            ImGui::TextDisabled("covers %.0f x %.0f %s", m_newW * ppN, m_newH * ppN,
                                target.metersPerPixel ? "m" : "blocks");
            if (m_layer == 0) // surface: signed height scaler
            {
                float f = SurfRangeToSigned(m_newValue);
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::DragFloat("Fill Value", &f, 0.002f, -1.0f, 1.0f, "%.3f"))
                    m_newValue = SurfSignedToRange(f);
                ui::ItemTooltip("Starting height scaler for every pixel: -1 = lowest, 0 = ground, +1 = highest. "
                                "Mapped into the node's Height Range around Ground Height. This is only the flat "
                                "starting level; paint hills and valleys on top afterwards.");
                ImGui::SameLine();
                const float span = *target.heightMax - *target.heightMin;
                const float cur = *target.groundHeight + *target.heightMin + ((f + 1.0f) * 0.5f) * span;
                ImGui::TextDisabled("= %.1f m   (-1..1 -> %.0f..%.0f m)", cur,
                                    *target.groundHeight + *target.heightMin, *target.groundHeight + *target.heightMax);
            }
            else if (!OnFeatures()) // strata: thickness in blocks
            {
                int iv = static_cast<int>(std::lround(m_newValue));
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::DragInt("Fill Value", &iv, 1.0f, 0, 255))
                    m_newValue = static_cast<float>(iv);
                ui::ItemTooltip("Initial thickness in blocks for every pixel of the new strata map. "
                                "This is only the flat starting level; paint on top afterwards.");
                ImGui::SameLine();
                ImGui::TextDisabled("= %d blocks", iv);
            }
            if (ImGui::Button("Create Map"))
                CreateMap();
            ImGui::End();
            return;
        }

        if (OnScatter())
        {
            const int kinds = target.scatterMeshes ? static_cast<int>(target.scatterMeshes->size()) : 0;
            if (kinds == 0)
            {
                ImGui::TextDisabled("Add Scatter Meshes on the Terrain node (inspector) to pick what to paint.");
            }
            else
            {
                std::vector<std::string> labels;
                labels.reserve(kinds + 1);
                for (int i = 0; i < kinds; ++i)
                    labels.push_back(std::to_string(i + 1) + ": " + (*target.scatterMeshes)[i]);
                labels.emplace_back("Erase");
                std::vector<const char *> items;
                for (const std::string &s : labels)
                    items.push_back(s.c_str());
                m_scatterKind = std::clamp(m_scatterKind, 0, kinds);
                ImGui::SetNextItemWidth(180.0f);
                ImGui::Combo("Type", &m_scatterKind, items.data(), static_cast<int>(items.size()));
                ui::ItemTooltip("Which Scatter Mesh the brush plants (drag freely - the scatter never "
                                "doubles up). Strokes re-mesh the touched terrain tiles live; Save just "
                                "persists the PNG. Ctrl+LMB erases with any type selected.");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(110.0f);
                ImGui::DragInt("Spacing", &m_featureSpacing, 0.2f, 2, 64);
                ui::ItemTooltip("Minimum pixels between scattered instances.");
            }
        }
        else if (OnFeatures())
        {
            LoadPalette(); // thumbnails for the Block picker + tile colors for the preview
            ImGui::SetNextItemWidth(110.0f);
            ImGui::Combo("Type", &m_brushType, kFeatureNames, 6);
            ui::ItemTooltip("Tree/Rock/Olive/Cypress scatter sparse feature dots (drag freely - the "
                            "scatter never doubles up). Block paints the picked tile solidly onto the "
                            "surface. Erase clears. Ctrl+LMB erases with any type selected.");
            if (m_brushType <= static_cast<int>(FeatureStamp::Cypress)) // scatter types use Spacing
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(110.0f);
                ImGui::DragInt("Spacing", &m_featureSpacing, 0.2f, 2, 64);
                ui::ItemTooltip("Minimum pixels between scattered features.");
            }
            if (m_brushType == static_cast<int>(FeatureStamp::Block))
                DrawPalette();
        }
        else
        {
            static const char *kBrushNames[4] = {"Raise / Lower", "Smooth", "Flatten", "Set Value"};
            ImGui::SetNextItemWidth(110.0f);
            ImGui::Combo("Type", &m_brushType, kBrushNames, 4);
            ui::ItemTooltip("Raise: add height (Shift+LMB lowers). Smooth: blend toward the neighborhood "
                            "average. Flatten: pull toward the value under the stroke start. Set: pull "
                            "toward an explicit value.");
            if (m_brushType == static_cast<int>(Brush::Set))
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(110.0f);
                if (m_layer == 0) // surface: signed height scaler
                {
                    float f = SurfRangeToSigned(m_setValue);
                    if (ImGui::DragFloat("Value", &f, 0.002f, -1.0f, 1.0f, "%.3f"))
                        m_setValue = SurfSignedToRange(f);
                    ui::ItemTooltip("Target height scaler the Set brush paints toward (-1 low, 0 ground, +1 high).");
                }
                else
                {
                    int iv = static_cast<int>(std::lround(m_setValue));
                    if (ImGui::DragInt("Value", &iv, 1.0f, 0, 255))
                        m_setValue = static_cast<float>(iv);
                    ui::ItemTooltip("Target value the Set brush paints toward.");
                }
            }
        }
        ImGui::SetNextItemWidth(110.0f);
        ImGui::DragFloat("Brush", &m_brushRadius, 0.2f, 1.0f, 128.0f, "%.0f px");
        ui::ItemTooltip("Brush radius in map pixels.");
        if (!OnFeatures())
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            ImGui::DragFloat("Strength", &m_brushStrength, 0.2f, 1.0f, 64.0f, "%.0f");
            ui::ItemTooltip("Effect per stamp at the brush center, falling off to the rim. For blend "
                            "brushes 64 = full effect in one stamp.");
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::SliderFloat("Zoom", &m_zoom, 0.1f, static_cast<float>(std::max(buf.w, buf.h)), "%.2fx",
                           ImGuiSliderFlags_Logarithmic);
        ui::ItemTooltip("Mouse wheel over the canvas zooms toward the cursor; right-drag pans.");

        if (ImGui::Button("Save + Rebuild"))
            Save();
        ui::ItemTooltip("Write the PNG and force the voxel world to regenerate from it.");
        ImGui::SameLine();
        if (ImGui::Button("Reload"))
            buf.loadedPath.clear(); // next SyncLayer reloads from disk, discarding edits
        ui::ItemTooltip("Discard unsaved edits and reload the file from disk.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60.0f);
        ImGui::DragInt("##resizeW", &m_newW, 1.0f, 16, 2048);
        ImGui::SameLine(0.0f, 2.0f);
        ImGui::TextUnformatted("x");
        ImGui::SameLine(0.0f, 2.0f);
        ImGui::SetNextItemWidth(60.0f);
        ImGui::DragInt("##resizeH", &m_newH, 1.0f, 16, 2048);
        ImGui::SameLine();
        if (ImGui::Button("Resize"))
            ResizeMap();
        ui::ItemTooltip("Resample the map to the new size (bilinear; features resample nearest). "
                        "With Blocks/Pixel unchanged this also resizes the world area the map covers.");
        ImGui::SameLine();
        const float ppW = target.ppScale();
        ImGui::Text("%s%s  %dx%d px = %.0fx%.0f %s", configured.c_str(), buf.unsaved ? " *" : "", buf.w, buf.h,
                    buf.w * ppW, buf.h * ppW, target.metersPerPixel ? "m" : "blocks");

        UploadPreview();

        ImGui::BeginChild("canvas", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), true,
                          ImGuiWindowFlags_NoScrollWithMouse);
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 origin = ImGui::GetCursorScreenPos(); // canvas top-left, fixed
        const float fit = std::max(0.02f, std::min(avail.x / static_cast<float>(buf.w),
                                                   avail.y / static_cast<float>(buf.h)));
        // A full-canvas InvisibleButton catches paint (LMB) and pan/zoom anywhere in the view; the
        // texture is drawn underneath via the draw list, offset by the manual pan (ImGui::Image is
        // not an item and clicking it would drag the window).
        ImGui::InvisibleButton("paint", avail);
        const bool canvasHovered = ImGui::IsItemHovered();

        // Mouse wheel zooms toward the cursor: keep the map point under the cursor fixed. Free from
        // fit-the-map (0.1x) out to ~one map pixel filling the view (map-dimension x).
        if (const float wheel = ImGui::GetIO().MouseWheel; canvasHovered && wheel != 0.0f)
        {
            const float scaleOld = fit * m_zoom;
            const ImVec2 mp = ImGui::GetMousePos();
            const float mapX = (mp.x - origin.x - m_panX) / scaleOld;
            const float mapY = (mp.y - origin.y - m_panY) / scaleOld;
            const float zoomMax = static_cast<float>(std::max(buf.w, buf.h));
            m_zoom = std::clamp(m_zoom * std::pow(1.2f, wheel), 0.1f, zoomMax);
            const float scaleNew = fit * m_zoom;
            m_panX -= mapX * (scaleNew - scaleOld);
            m_panY -= mapY * (scaleNew - scaleOld);
        }
        // Right-drag pans; keep panning while held even if the cursor leaves the canvas.
        if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            m_panning = true;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
            m_panning = false;
        if (m_panning)
        {
            m_panX += ImGui::GetIO().MouseDelta.x;
            m_panY += ImGui::GetIO().MouseDelta.y;
        }

        const float scale = fit * m_zoom;
        const ImVec2 imageMin(origin.x + m_panX, origin.y + m_panY);
        const ImVec2 drawSize(static_cast<float>(buf.w) * scale, static_cast<float>(buf.h) * scale);
        if (m_textureId)
            ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)m_textureId, imageMin,
                                                 ImVec2(imageMin.x + drawSize.x, imageMin.y + drawSize.y));

        int hoverValue = -1;   // -1 = not over the image; else the rounded value (feature id / thickness)
        float hoverRaw = 0.0f; // full-precision value under the cursor (surface scaler readout)
        int hoverX = 0, hoverY = 0;
        // IsItemActive keeps the stroke alive while the button is held, even if the cursor
        // momentarily leaves the canvas.
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        {
            const ImVec2 mouse = ImGui::GetMousePos();
            const float px = (mouse.x - imageMin.x) / scale;
            const float py = (mouse.y - imageMin.y) / scale;
            const bool inImage = px >= 0.0f && px < buf.w && py >= 0.0f && py < buf.h;
            hoverX = std::clamp(static_cast<int>(px), 0, buf.w - 1);
            hoverY = std::clamp(static_cast<int>(py), 0, buf.h - 1);
            if (inImage)
            {
                hoverRaw = buf.px[hoverY * buf.w + hoverX];
                hoverValue = static_cast<int>(std::lround(hoverRaw));
            }

            ImGui::GetWindowDrawList()->AddCircle(mouse, m_brushRadius * scale, IM_COL32(255, 210, 40, 220));

            // Paint only over the image (the hit target spans the whole canvas so pan/zoom work in
            // the margins); a drag that leaves the image resets so it doesn't smear back in.
            if (inImage && ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                if (ImGui::GetIO().KeyAlt) // Alt+LMB teleports the camera over the spot (keeps height)
                {
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        TeleportCameraTo(px, py);
                    m_haveLastStamp = false;
                }
                else
                {
                    const bool lower = ImGui::GetIO().KeyShift;
                    const bool ctrlErase =
                        (OnFeatures() || OnScatter()) && ImGui::GetIO().KeyCtrl; // Ctrl+LMB clears
                    const int scatterKinds =
                        target.scatterMeshes ? static_cast<int>(target.scatterMeshes->size()) : 0;
                    const auto stampAt = [&](float sx, float sy)
                    {
                        if (OnScatter())
                            StampScatter(buf, sx, sy, m_brushRadius,
                                         (ctrlErase || m_scatterKind >= scatterKinds) ? 0 : m_scatterKind + 1);
                        else if (OnFeatures())
                            StampFeatures(sx, sy, m_brushRadius,
                                          ctrlErase ? FeatureStamp::Erase
                                                    : static_cast<FeatureStamp>(std::clamp(m_brushType, 0, 5)));
                        else
                            StampBrush(sx, sy, m_brushRadius, m_brushStrength, lower,
                                       static_cast<Brush>(std::clamp(m_brushType, 0, 3)), m_setValue);
                    };
                    const float segX = m_haveLastStamp ? m_lastPx : px; // stroke-segment start for the
                    const float segY = m_haveLastStamp ? m_lastPy : py; // live scatter re-mesh rect
                    if (m_haveLastStamp)
                    {
                        // Stamp along the segment from the last position so fast strokes stay solid.
                        const float dx = px - m_lastPx;
                        const float dy = py - m_lastPy;
                        const float dist = std::sqrt(dx * dx + dy * dy);
                        const float step = std::max(0.5f, m_brushRadius * 0.5f);
                        const int steps = std::max(1, static_cast<int>(dist / step));
                        for (int i = 1; i <= steps; ++i)
                        {
                            const float t = static_cast<float>(i) / static_cast<float>(steps);
                            stampAt(m_lastPx + dx * t, m_lastPy + dy * t);
                        }
                    }
                    else
                    {
                        m_flattenTarget = hoverRaw; // flatten pulls toward the value under the stroke start
                        stampAt(px, py);
                    }
                    if (OnScatter()) // strokes apply live — touched tiles re-mesh, no rebuild
                        PushScatterLive(std::min(segX, px) - m_brushRadius, std::min(segY, py) - m_brushRadius,
                                        std::max(segX, px) + m_brushRadius, std::max(segY, py) + m_brushRadius);
                    m_lastPx = px;
                    m_lastPy = py;
                    m_haveLastStamp = true;
                    buf.unsaved = true;
                    m_textureDirty = true;
                }
            }
            else
            {
                m_haveLastStamp = false;
            }
        }
        else
        {
            m_haveLastStamp = false;
        }
        ImGui::EndChild();

        if (hoverValue >= 0)
        {
            if (OnScatter())
            {
                const int kinds = target.scatterMeshes ? static_cast<int>(target.scatterMeshes->size()) : 0;
                const char *lbl = hoverValue > 0 && hoverValue <= kinds
                                      ? (*target.scatterMeshes)[hoverValue - 1].c_str()
                                      : (hoverValue > 0 ? "?" : "-");
                ImGui::Text("%d, %d = %s  |  LMB paint, Ctrl erase, Alt teleport cam, RMB pan", hoverX, hoverY,
                            lbl);
            }
            else if (OnFeatures())
            {
                char lbl[32];
                if (hoverValue == 1)
                    snprintf(lbl, sizeof(lbl), "tree");
                else if (hoverValue == 2)
                    snprintf(lbl, sizeof(lbl), "rock");
                else if (hoverValue == 4)
                    snprintf(lbl, sizeof(lbl), "olive");
                else if (hoverValue == 5)
                    snprintf(lbl, sizeof(lbl), "cypress");
                else if (hoverValue >= kBlockPaintBase)
                    snprintf(lbl, sizeof(lbl), "block %d", hoverValue - kBlockPaintBase);
                else
                    snprintf(lbl, sizeof(lbl), "-");
                ImGui::Text("%d, %d = %s  |  LMB paint, Ctrl erase, Alt teleport cam, RMB pan", hoverX, hoverY,
                            lbl);
            }
            else if (m_layer == 0)
                ImGui::Text("%d, %d = %.3f (scaler)  |  LMB paint, Shift lower, Alt teleport cam, RMB pan", hoverX,
                            hoverY, SurfRangeToSigned(hoverRaw));
            else
                ImGui::Text("%d, %d = %d blocks thick  |  LMB paint, Shift lower, Alt teleport cam, RMB pan", hoverX,
                            hoverY, hoverValue);
        }
        else
        {
            ImGui::TextDisabled(OnFeatures() ? "LMB paint, Ctrl erase, Alt teleport cam, RMB pan"
                                             : "LMB paint, Shift lower, Alt teleport cam, RMB pan");
        }

        ImGui::End();
    }
} // namespace pe
