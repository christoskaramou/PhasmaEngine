#include "Terrain/TerrainWorld.h"
#include "API/Command.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "Scene/Material.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Voxel/ITerrainGenerator.h"
#include "Voxel/MapGen.h"      // reuse the shared worldgen seam (heightmap SurfaceHeight)
#include "Voxel/NoiseGen.h"    // reuse the shared worldgen seam (noise SurfaceHeight/Density)
#include "Voxel/SurfaceNets.h" // ring-0 isosurface tile mesher
#include "Voxel/VoxelTypes.h"  // kSectionDim (blocks per column)
#include "Voxel/VoxelWorld.h"  // voxel::VoxelConfig (full definition, for the generator seam)
#ifdef PE_PHYSICS
#include "ECS/Context.h"           // GetGlobalSystem
#include "Systems/PhysicsSystem.h" // per-tile static triangle-mesh colliders
#endif
#include <meshoptimizer.h> // meshopt_simplify: discrete LODs on ring-0 tiles

namespace pe::terrain
{
    namespace
    {
        constexpr int kBlocksPerColumn = voxel::kSectionDim; // 16
        constexpr int kTileCells = 32;                       // tile size in cells, every ring
        constexpr int kMaxTilesPerSide = 64;                 // bounded cap (64 * 32 = 2048 cells/side)
        constexpr int kMaxStreamTilesPerSide = 20;           // streaming fine-window cap (memory)
        constexpr int kCoarseRings = 2;                      // streaming view rings past ring 0 (x4 cell each)
        constexpr int kMeshBudgetPerUpdate = 3;              // tile re-meshes per frame
        constexpr int kCookBudgetPerUpdate = 2;              // collider cooks per frame
        // Painted-cave profile: at full paint (255) the void is kCaveHeightM tall with its roof
        // kCaveRoofDepthM below the local surface, shrinking toward its centre line as the paint
        // fades. ponytail: fixed metric profile; per-map depth/height knobs if authors ever need them.
        constexpr float kCaveRoofDepthM = 3.0f;
        constexpr float kCaveHeightM = 6.0f;
        constexpr size_t kMaxPendingCmds = 3;   // upload cmds in flight before waiting
        constexpr int kMaxFieldCellsY = 512;    // vertical sampling-band cap per tile
        constexpr float kSkirtDropCells = 3.0f; // coarse-tile skirt drop, in cells

        uint32_t RoundUpTo(uint32_t v, uint32_t a)
        {
            return (v + a - 1) / a * a;
        }
        int WrapMod(int v, int n)
        {
            return ((v % n) + n) % n;
        }
        int TileFloor(float x, float tileWorld)
        {
            return static_cast<int>(std::floor(x / tileWorld));
        }

        // MapGen/NoiseGen read a voxel::VoxelConfig; fill only the fields SurfaceHeight / Valid /
        // WorldRadiusColumns actually use. ponytail: the generator seam still lives in voxel::; a shared
        // Worldgen config is the cleaner home if a third consumer ever appears.
        voxel::VoxelConfig ToVoxelConfig(const TerrainConfig &cfg)
        {
            voxel::VoxelConfig vc{};
            vc.heightmapPath = cfg.heightmapPath;
            vc.blocksPerPixel = std::max(1, (int)std::lround(cfg.metersPerPixel));
            vc.surfaceMetersPerPixel = std::max(0.05f, cfg.metersPerPixel); // float extent for SurfaceHeight
            vc.groundHeight = cfg.groundHeight;
            vc.heightMin = cfg.heightMin;
            vc.heightMax = cfg.heightMax;
            vc.boundsCenterCx = cfg.boundsCenterCx;
            vc.boundsCenterCz = cfg.boundsCenterCz;
            vc.streaming = false;
            return vc;
        }
    } // namespace

    TerrainWorld::TerrainWorld() = default;
    TerrainWorld::~TerrainWorld()
    {
        Destroy();
    }

    void TerrainWorld::SetTerrainGenerator(std::shared_ptr<voxel::ITerrainGenerator> generator)
    {
        m_generator = std::move(generator);
        m_generatorOverridden = (m_generator != nullptr);
    }

    void TerrainWorld::SetAnchor(const vec3 &anchor)
    {
        m_anchor = anchor;
        m_anchorSet = true;
    }

    void TerrainWorld::BuildGenerator()
    {
        // Engine ships default generators; a game keeps its own via SetTerrainGenerator. A configured
        // heightmap selects MapGen (a failed load falls back to noise); both expose SurfaceHeight(x,z).
        if (m_generatorOverridden)
            return;
        m_generator.reset();
        if (!m_cfg.heightmapPath.empty())
        {
            voxel::VoxelConfig vc = ToVoxelConfig(m_cfg);
            auto mapGen = std::make_shared<voxel::MapGen>(vc);
            if (mapGen->Valid())
            {
                // 0 on an axis: fill exactly the map's extent on that axis.
                if (m_cfg.sizeXMeters == 0)
                    m_cfg.sizeXMeters = mapGen->MapBlocksX();
                if (m_cfg.sizeZMeters == 0)
                    m_cfg.sizeZMeters = mapGen->MapBlocksZ();
                m_generator = std::move(mapGen);
            }
        }
        if (!m_generator)
        {
            voxel::NoiseParams p{};
            p.groundY = 64;
            p.amplitude = 28.0f; // ponytail: fixed domain-warp strength; surface peak height is the Height Range
            p.groundHeight = m_cfg.groundHeight;
            p.heightMin = m_cfg.heightMin;
            p.heightMax = m_cfg.heightMax;
            p.featureScale = m_cfg.noiseFeatureScale;
            p.seed = m_cfg.noiseSeed;
            p.caves = false; // surface only
            p.seaLevel = -1;
            p.overhangs = m_cfg.overhangs;
            m_generator = std::make_shared<voxel::NoiseGen>(p);
        }
    }

    void TerrainWorld::LoadCavesMap()
    {
        m_cavesMap.reset();
        m_cavesExtentX = m_cavesExtentZ = 0.0f;
        if (m_cfg.cavesPath.empty())
            return;
        auto map = std::make_unique<voxel::MapImage>();
        if (!map->Load(m_cfg.cavesPath, "caves", false)) // plain 0..255 grayscale; Load warns on failure
            return;
        m_cavesCenterX = m_cfg.boundsCenterCx * kBlocksPerColumn + kBlocksPerColumn / 2.0f;
        m_cavesCenterZ = m_cfg.boundsCenterCz * kBlocksPerColumn + kBlocksPerColumn / 2.0f;
        m_cavesExtentX = map->w * m_cfg.metersPerPixel;
        m_cavesExtentZ = map->h * m_cfg.metersPerPixel;
        m_cavesMap = std::move(map);
    }

    void TerrainWorld::Create(Scene *scene, const TerrainConfig &cfg)
    {
        // Ops seeded via SetSculptOps before Create survive the reset — Create-time meshing must
        // apply them. A standalone Destroy still clears them.
        std::vector<SculptOp> ops = std::move(m_ops);
        Destroy();
        m_ops = std::move(ops);
        m_scene = scene;
        m_cfg = cfg;
        m_cfg.sizeXMeters = std::max(0, m_cfg.sizeXMeters);
        m_cfg.sizeZMeters = std::max(0, m_cfg.sizeZMeters);
        m_cfg.overhangs = std::clamp(m_cfg.overhangs, 0.0f, 1.0f);
        m_cfg.metersPerPixel = std::max(0.05f, m_cfg.metersPerPixel);

        BuildGenerator();
        LoadCavesMap();
        if (m_cfg.sizeXMeters <= 0)
            m_cfg.sizeXMeters = 256; // no map + no explicit size: a default patch
        if (m_cfg.sizeZMeters <= 0)
            m_cfg.sizeZMeters = 256;

        if (!m_anchorSet)
            m_anchor = vec3(m_cfg.boundsCenterCx * kBlocksPerColumn + kBlocksPerColumn / 2, 0.0f,
                            m_cfg.boundsCenterCz * kBlocksPerColumn + kBlocksPerColumn / 2);

        if (!m_scene)
            return;

        m_material = std::make_unique<Material>();
        m_material->name = "TerrainMaterial";
        m_material->baseColorFactor = vec4(1.0f); // white — per-vertex colour carries the terrain bands
        m_material->metallic = 0.0f;
        m_material->roughness = 1.0f;
        m_material->occlusionStrength = 1.0f;
        m_material->textureMask = 0u;
        m_material->renderType = RenderType::Opaque;
        m_material->SyncParamsFromLegacy();

        while (NodeId *stale = m_scene->FindNodeByName("TerrainHost"))
            m_scene->DeleteNode(stale);
        m_hostNode = m_scene->CreateNode("TerrainHost");
        m_scene->SetLocalMatrix(m_hostNode, mat4(1.0f), false);

        BuildRings();
        AllocateTiles();

        // Initial content, synchronous — the single Create-time flush uploads real terrain and later
        // work never rebuilds the geometry buffer (in-place streamed updates only).
        size_t totalVerts = 0, totalTris = 0;
        for (size_t r = 0; r < m_rings.size(); ++r)
        {
            Ring &ring = m_rings[r];
            for (int i = 0; i < ring.tilesX * ring.tilesZ; ++i)
            {
                Tile &tile = m_tiles[ring.firstTile + i];
                MeshTile(static_cast<int>(r), tile);
                tile.dirty = tile.growPending; // overflowed tiles re-mesh after the grow
                totalVerts += tile.liveVerts;
                totalTris += tile.liveIndices / 3;
            }
        }
        PE_INFO("Terrain: %zu tiles in %zu ring(s), %zu live verts / %zu tris (streaming=%d overhangs=%.2f)",
                m_tiles.size(), m_rings.size(), totalVerts, totalTris, m_cfg.streaming ? 1 : 0,
                m_cfg.overhangs);
        // ponytail: a runtime AddMesh here would clobber a coexisting cube-voxel GeometryArena
        // reservation; fine while terrain and cube worlds live in separate scenes.
        m_scene->SetGeometryDirty();
        m_scene->FlushPendingGpuWork();
    }

    void TerrainWorld::BuildRings()
    {
        m_rings.clear();
        const float cell0 = m_cfg.metersPerPixel;
        const float tw0 = cell0 * kTileCells;
        const float centerX = m_cfg.boundsCenterCx * kBlocksPerColumn + kBlocksPerColumn / 2.0f;
        const float centerZ = m_cfg.boundsCenterCz * kBlocksPerColumn + kBlocksPerColumn / 2.0f;

        Ring r0;
        r0.cellSize = cell0;
        if (m_cfg.streaming)
        {
            r0.tilesX = std::clamp((int)std::lround(m_cfg.sizeXMeters / tw0), 2, kMaxStreamTilesPerSide);
            r0.tilesZ = std::clamp((int)std::lround(m_cfg.sizeZMeters / tw0), 2, kMaxStreamTilesPerSide);
            const float tw = tw0;
            r0.center = ivec2(TileFloor(m_anchor.x, tw) - r0.tilesX / 2, TileFloor(m_anchor.z, tw) - r0.tilesZ / 2);
        }
        else
        {
            // Bounded: the window is the world — minimal tile cover of the configured extent.
            const float minX = centerX - m_cfg.sizeXMeters * 0.5f;
            const float minZ = centerZ - m_cfg.sizeZMeters * 0.5f;
            const int baseTx = TileFloor(minX, tw0);
            const int baseTz = TileFloor(minZ, tw0);
            int tilesX = (int)std::ceil((minX + m_cfg.sizeXMeters) / tw0) - baseTx;
            int tilesZ = (int)std::ceil((minZ + m_cfg.sizeZMeters) / tw0) - baseTz;
            if (tilesX > kMaxTilesPerSide || tilesZ > kMaxTilesPerSide)
            {
                PE_WARN("Terrain: %dx%d tiles exceeds the %d-tile cap; clamped (raise Meters/Pixel or "
                        "enable streaming for a bigger world).",
                        tilesX, tilesZ, kMaxTilesPerSide);
                tilesX = std::min(tilesX, kMaxTilesPerSide);
                tilesZ = std::min(tilesZ, kMaxTilesPerSide);
            }
            r0.tilesX = std::max(1, tilesX);
            r0.tilesZ = std::max(1, tilesZ);
            r0.center = ivec2(baseTx, baseTz);
        }
        m_rings.push_back(r0);

        if (m_cfg.streaming)
        {
            for (int r = 1; r <= kCoarseRings; ++r)
            {
                Ring ring;
                ring.cellSize = cell0 * std::pow(4.0f, (float)r);
                ring.tilesX = m_rings[0].tilesX;
                ring.tilesZ = m_rings[0].tilesZ;
                const float tw = ring.cellSize * kTileCells;
                ring.center =
                    ivec2(TileFloor(m_anchor.x, tw) - ring.tilesX / 2, TileFloor(m_anchor.z, tw) - ring.tilesZ / 2);
                m_rings.push_back(ring);
            }
        }
    }

    float TerrainWorld::TileWorldSize(const Ring &ring) const
    {
        return ring.cellSize * kTileCells;
    }

    void TerrainWorld::AllocateTiles()
    {
        std::vector<Vertex> &vertices = m_scene->GetVertexStore();
        std::vector<PositionUvVertex> &positionUvs = m_scene->GetPositionUvStore();
        std::vector<AabbVertex> &aabbVertices = m_scene->GetAabbVertexStore();
        std::vector<uint32_t> &indices = m_scene->GetIndexStore();

        m_tiles.clear();
        uint32_t refSlot = 0;
        for (size_t r = 0; r < m_rings.size(); ++r)
        {
            Ring &ring = m_rings[r];
            ring.firstTile = static_cast<int>(m_tiles.size());

            // Budgets. Ring 0 (Surface Nets): roughly one vertex per surface cell over a (cells+1)^2
            // column window; overhangs/sculpts multiply locally, and a grow event re-places any tile
            // that outruns its slack. Coarse rings are exact grid meshes (+ skirt), no LODs.
            const uint32_t baseCols = (kTileCells + 2) * (kTileCells + 2);
            uint32_t vertexBudget, indexBudget;
            if (r == 0)
            {
                // Measured: vertex count tracks surface area, so steep plain terrain runs ~1.45x the
                // column count (seed-7 noise and the Greece heightmap alike, no overhangs) and
                // overhangs=0.8 tiles up to ~3.1x (folded surfaces cross most columns 2-3 times).
                // Cliff-heavy heightmap regions have hit ~2.8x — those settle via one ring-wide grow.
                const float slack = 1.5f + 2.5f * m_cfg.overhangs;
                vertexBudget = RoundUpTo((uint32_t)(baseCols * slack), 256);
                indexBudget = vertexBudget * 6 * 2; // quads ~ cells ~ verts; x2 for the meshopt LOD chain
            }
            else
            {
                vertexBudget = RoundUpTo(baseCols + 4 * (kTileCells + 1), 128);
                indexBudget = RoundUpTo(kTileCells * kTileCells * 6 + 4 * kTileCells * 12 + 64, 128);
            }

            for (int i = 0; i < ring.tilesX * ring.tilesZ; ++i)
            {
                Tile tile;
                tile.vertexOffset = static_cast<uint32_t>(vertices.size());
                tile.vertexBudget = vertexBudget;
                vertices.resize(vertices.size() + vertexBudget);
                positionUvs.resize(positionUvs.size() + vertexBudget);
                tile.indexOffset = static_cast<uint32_t>(indices.size());
                tile.indexBudget = indexBudget;
                indices.resize(indices.size() + indexBudget, 0u);
                tile.aabbVertexOffset = aabbVertices.size();
                aabbVertices.resize(aabbVertices.size() + 8);

                Mesh m{};
                m.vertexOffset = tile.vertexOffset;
                m.vertexCount = vertexBudget; // reserved budget; live counts drive the draws
                m.indexOffset = tile.indexOffset;
                m.indexCount = 3;
                m.positionsOffset = tile.vertexOffset;
                m.aabbVertexOffset = tile.aabbVertexOffset;
                m.aabbColor = 0xFFFFFFFF;
                m.renderType = RenderType::Opaque;
                m.material = m_material.get();
                m.lodEnabled = (r == 0); // coarse rings ARE the distance LOD
                m.lodIndexOffset[0] = m.indexOffset;
                m.lodIndexCount[0] = m.indexCount;
                m.lodCount = 1;
                tile.meshIndex = m_scene->AddMesh(std::move(m));
                tile.refSlot = refSlot++;
                m_scene->AddMeshRef(m_hostNode, tile.meshIndex);

                // Desired world tile for this slot — the same toroidal congruence StreamWindows uses
                // (slot s holds the window tile with T ≡ s mod N), so the first Update re-meshes nothing.
                const int sx = i % ring.tilesX, sz = i / ring.tilesX;
                tile.tx = ring.center.x + WrapMod(sx - ring.center.x, ring.tilesX);
                tile.tz = ring.center.y + WrapMod(sz - ring.center.y, ring.tilesZ);
                tile.interiorHole = TileInteriorHole(static_cast<int>(r), tile.tx, tile.tz);
                WriteEmptyTile(tile); // valid placeholder even if the first mesh overflows into a grow
                m_tiles.push_back(tile);
            }
        }
    }

    // The toroidal slot layout never moves tile slots — a slot is re-meshed for its new world tile
    // when the window slides past it. Slot (sx, sz) of a ring holds the world tile congruent to it
    // (mod tilesPerSide) inside the window [base, base + tiles).
    void TerrainWorld::StreamWindows()
    {
        if (!m_cfg.streaming)
            return;
        for (size_t r = 0; r < m_rings.size(); ++r)
        {
            Ring &ring = m_rings[r];
            const float tw = TileWorldSize(ring);
            ring.center = ivec2(TileFloor(m_anchor.x, tw) - ring.tilesX / 2, TileFloor(m_anchor.z, tw) - ring.tilesZ / 2);
            for (int i = 0; i < ring.tilesX * ring.tilesZ; ++i)
            {
                Tile &tile = m_tiles[ring.firstTile + i];
                const int sx = i % ring.tilesX, sz = i / ring.tilesX;
                const int dtx = ring.center.x + WrapMod(sx - ring.center.x, ring.tilesX);
                const int dtz = ring.center.y + WrapMod(sz - ring.center.y, ring.tilesZ);
                const bool hole = TileInteriorHole(static_cast<int>(r), dtx, dtz);
                if (dtx != tile.tx || dtz != tile.tz || hole != tile.interiorHole)
                {
                    tile.tx = dtx;
                    tile.tz = dtz;
                    tile.interiorHole = hole;
                    tile.dirty = true;
                    RemoveTileBody(tile); // old-location collider is invalid immediately
                }
            }
        }
    }

    // A coarse ring holes out tiles the next-finer ring already covers, keeping a one-tile overlap
    // band so the fine window's rim (whose outermost stitch cells have no loaded neighbour) always
    // has coarse terrain underneath it.
    bool TerrainWorld::TileInteriorHole(int ringIdx, int tx, int tz) const
    {
        if (ringIdx <= 0)
            return false;
        const Ring &ring = m_rings[ringIdx];
        const Ring &inner = m_rings[ringIdx - 1];
        const float tw = TileWorldSize(ring);
        const float itw = TileWorldSize(inner);
        const float minX = tx * tw, maxX = (tx + 1) * tw;
        const float minZ = tz * tw, maxZ = (tz + 1) * tw;
        const float iMinX = inner.center.x * itw + tw;
        const float iMaxX = (inner.center.x + inner.tilesX) * itw - tw;
        const float iMinZ = inner.center.y * itw + tw;
        const float iMaxZ = (inner.center.y + inner.tilesZ) * itw - tw;
        return minX >= iMinX && maxX <= iMaxX && minZ >= iMinZ && maxZ <= iMaxZ;
    }

    float TerrainWorld::DensityLocal(float x, float y, float z, float surfaceHeight) const
    {
        float d = m_generator ? m_generator->DensityAtHeight(x, y, z, surfaceHeight)
                              : (surfaceHeight - y);
        // Painted caves: a void centred kCaveRoofDepthM + halfH below the LOCAL surface, half-height
        // kCaveHeightM/2 scaled by the map value — bilinear paint pinches it closed at the edges.
        // Zero outside the map extent (edge-clamping would tile caves across a streamed world).
        // Applied before sculpt ops so sculpting can open entrances or fill painted caves back in.
        if (m_cavesMap)
        {
            const float nu = 0.5f - (x - m_cavesCenterX) / m_cavesExtentX; // both axes flip, like the
            const float nv = 0.5f - (z - m_cavesCenterZ) / m_cavesExtentZ; // terrain heightmap sampling
            if (nu >= 0.0f && nu <= 1.0f && nv >= 0.0f && nv <= 1.0f)
            {
                const float v = m_cavesMap->SampleNorm(nu, nv) * (1.0f / 255.0f);
                if (v > 0.02f) // skip hairline slits from near-zero paint
                {
                    const float yc = surfaceHeight - (kCaveRoofDepthM + 0.5f * kCaveHeightM);
                    const float s = 0.5f * kCaveHeightM * v - std::abs(y - yc); // > 0 inside the void
                    if (s > 0.0f)
                        d = std::min(d, -s);
                }
            }
        }
        // CSG sphere ops, in stroke order. The in-sphere-only application keeps the zero set exact and
        // every tile computes the identical field; the (cheap) price is slightly faceted crater rims
        // versus a full SDF blend. ponytail: linear scan; bucket ops per tile if counts ever hurt.
        for (const SculptOp &op : m_ops)
        {
            const float dx = x - op.center.x, dy = y - op.center.y, dz = z - op.center.z;
            const float dd = dx * dx + dy * dy + dz * dz;
            if (dd > op.radius * op.radius)
                continue;
            const float s = op.radius - std::sqrt(dd); // > 0 inside the sphere
            d = op.dig ? std::min(d, -s) : std::max(d, s);
        }
        return d;
    }

    float TerrainWorld::DensityAt(float x, float y, float z) const
    {
        return DensityLocal(x, y, z, m_generator ? m_generator->SurfaceHeight(x, z) : m_cfg.groundHeight);
    }

    // Colour bands normalized to the CONFIGURED height range (stable across tiles and streaming, no
    // per-mesh normalization). GBufferPS multiplies albedo * input.color — no shader change.
    // ponytail: fixed bands; triplanar atlas later.
    vec3 TerrainWorld::TerrainColor(float y, float normalY) const
    {
        const float ymin = m_cfg.groundHeight + m_cfg.heightMin;
        const float yspan = std::max(1.0f, m_cfg.heightMax - m_cfg.heightMin);
        const float f = (y - ymin) / yspan;
        vec3 c = f < 0.08f   ? vec3(0.76f, 0.70f, 0.50f)
                 : f < 0.58f ? vec3(0.30f, 0.52f, 0.24f)
                 : f < 0.90f ? vec3(0.45f, 0.42f, 0.38f)
                             : vec3(0.92f, 0.94f, 0.96f);
        if (normalY < 0.5f && f < 0.90f)
            c = vec3(0.42f, 0.39f, 0.36f);
        if (y < m_cfg.seaLevelM)
            c = glm::mix(c, vec3(0.16f, 0.26f, 0.40f), 0.6f);
        return c;
    }

    void TerrainWorld::MeshTile(int ringIdx, Tile &tile)
    {
        if (tile.interiorHole)
        {
            WriteEmptyTile(tile);
            return;
        }
        if (ringIdx == 0)
            MeshTileSurfaceNets(m_rings[0], tile);
        else
            MeshTileGrid(m_rings[ringIdx], tile);
        tile.bodyDirty = true;
    }

    void TerrainWorld::MeshTileSurfaceNets(const Ring &ring, Tile &tile)
    {
        const float cell = ring.cellSize;
        // One-cell negative apron stitches quads across tile seams (see SurfaceNetsTile); the missing
        // apron at bounded world edges just drops the outermost cell ring of the rim.
        const ivec3 apron(1, 0, 1);
        const int cellsXZ = kTileCells + 1;
        const int gx0 = tile.tx * kTileCells - apron.x;
        const int gz0 = tile.tz * kTileCells - apron.z;

        // Column surface heights over the corner lattice + guard ring: local corner l in [-1, cells+2)
        // stored at l + 1, sampled at world = float(globalCorner) * cell so neighbouring tiles get
        // bit-identical densities on shared corners.
        const int hnx = cellsXZ + 3, hnz = cellsXZ + 3;
        std::vector<float> heights(static_cast<size_t>(hnx) * hnz);
        float minH = 1e30f, maxH = -1e30f;
        for (int k = 0; k < hnz; ++k)
            for (int i = 0; i < hnx; ++i)
            {
                const float wx = static_cast<float>(gx0 + i - 1) * cell;
                const float wz = static_cast<float>(gz0 + k - 1) * cell;
                const float h = m_generator ? m_generator->SurfaceHeight(wx, wz) : m_cfg.groundHeight;
                heights[static_cast<size_t>(i) + static_cast<size_t>(hnx) * k] = h;
                minH = std::min(minH, h);
                maxH = std::max(maxH, h);
            }

        // Vertical band: the surface plus everything that can displace it — the overhang FBM's proven
        // fixed-point bound, and any sculpt sphere overlapping this tile (XZ, guard included).
        float minY = minH, maxY = maxH;
        if (m_cfg.overhangs > 0.0f)
        {
            const float band = 0.5f * std::max(2.0f, m_cfg.heightMax - m_cfg.heightMin);
            const float c = m_cfg.overhangs;
            const float relStar = (-1.0f + std::sqrt(1.0f + 4.0f * c * c)) / (2.0f * c);
            minY -= band * relStar;
            maxY += band * relStar;
        }
        const float rMinX = static_cast<float>(gx0 - 1) * cell, rMaxX = static_cast<float>(gx0 + cellsXZ + 2) * cell;
        const float rMinZ = static_cast<float>(gz0 - 1) * cell, rMaxZ = static_cast<float>(gz0 + cellsXZ + 2) * cell;
        // Painted caves live within kCaveRoofDepthM + kCaveHeightM below the local surface — extend the
        // band for tiles overlapping the caves map. ponytail: extent test only (not painted-pixel max),
        // costs ~(9m / cell) extra field rows on overlapping tiles.
        if (m_cavesMap && rMaxX > m_cavesCenterX - m_cavesExtentX * 0.5f &&
            rMinX < m_cavesCenterX + m_cavesExtentX * 0.5f && rMaxZ > m_cavesCenterZ - m_cavesExtentZ * 0.5f &&
            rMinZ < m_cavesCenterZ + m_cavesExtentZ * 0.5f)
            minY = std::min(minY, minH - (kCaveRoofDepthM + kCaveHeightM + 1.0f));
        for (const SculptOp &op : m_ops)
        {
            if (op.center.x + op.radius < rMinX || op.center.x - op.radius > rMaxX ||
                op.center.z + op.radius < rMinZ || op.center.z - op.radius > rMaxZ)
                continue;
            minY = std::min(minY, op.center.y - op.radius);
            maxY = std::max(maxY, op.center.y + op.radius);
        }
        minY -= 2.0f * cell;
        maxY += 2.0f * cell;
        const int gy0 = (int)std::floor(minY / cell) - 1;
        int cellsY = (int)std::ceil(maxY / cell) + 1 - gy0;
        if (cellsY > kMaxFieldCellsY)
        {
            PE_WARN("Terrain: tile (%d,%d) vertical band %d cells exceeds the %d cap; clamped.",
                    tile.tx, tile.tz, cellsY, kMaxFieldCellsY);
            cellsY = kMaxFieldCellsY;
        }
        cellsY = std::max(cellsY, 4);

        const ivec3 cells(cellsXZ, cellsY, cellsXZ);
        const ivec3 gridMin(gx0, gy0, gz0);
        const int fnx = cells.x + 3, fny = cells.y + 3, fnz = cells.z + 3;
        std::vector<float> field(static_cast<size_t>(fnx) * fny * fnz);
        for (int k = 0; k < fnz; ++k)
            for (int j = 0; j < fny; ++j)
                for (int i = 0; i < fnx; ++i)
                {
                    const float wx = static_cast<float>(gridMin.x + i - 1) * cell;
                    const float wy = static_cast<float>(gridMin.y + j - 1) * cell;
                    const float wz = static_cast<float>(gridMin.z + k - 1) * cell;
                    const float h = heights[static_cast<size_t>(i) + static_cast<size_t>(hnx) * k];
                    field[static_cast<size_t>(i) + static_cast<size_t>(fnx) * (j + static_cast<size_t>(fny) * k)] =
                        DensityLocal(wx, wy, wz, h);
                }

        voxel::SmoothMeshData mesh = voxel::SurfaceNetsTile(field, gridMin, cells, apron, cell);
        if (mesh.vertices.empty() || mesh.indices.empty())
        {
            WriteEmptyTile(tile);
            return;
        }
        for (Vertex &v : mesh.vertices)
        {
            const vec3 c = TerrainColor(v.position[1], v.normals[1]);
            v.color[0] = c.x;
            v.color[1] = c.y;
            v.color[2] = c.z;
            v.color[3] = 1.0f;
            v.uv[0] = v.position[0] / cell;
            v.uv[1] = v.position[2] / cell;
        }
        WriteTileContent(0, tile, mesh.vertices, mesh.indices, mesh.aabbMin, mesh.aabbMax);
    }

    void TerrainWorld::MeshTileGrid(const Ring &ring, Tile &tile)
    {
        const float cell = ring.cellSize;
        const int gx0 = tile.tx * kTileCells;
        const int gz0 = tile.tz * kTileCells;
        const int cx = kTileCells + 1; // corner count per axis

        // Corner heights + one guard ring for central-difference normals; global-integer sampling
        // keeps within-ring seams exact (shared corners, identical floats).
        const int hn = cx + 2;
        std::vector<float> h(static_cast<size_t>(hn) * hn);
        for (int k = 0; k < hn; ++k)
            for (int i = 0; i < hn; ++i)
                h[static_cast<size_t>(i) + static_cast<size_t>(hn) * k] =
                    m_generator ? m_generator->SurfaceHeight(static_cast<float>(gx0 + i - 1) * cell,
                                                             static_cast<float>(gz0 + k - 1) * cell)
                                : m_cfg.groundHeight;
        auto H = [&](int i, int k)
        { return h[static_cast<size_t>(i + 1) + static_cast<size_t>(hn) * (k + 1)]; };

        std::vector<Vertex> verts;
        std::vector<uint32_t> indices;
        verts.reserve(static_cast<size_t>(cx) * cx + 4 * cx);
        indices.reserve(static_cast<size_t>(kTileCells) * kTileCells * 6 + 4 * kTileCells * 12);
        vec3 bbMin(1e30f), bbMax(-1e30f);
        // Coarse rings sit a fraction of a cell BELOW the true surface: where a coarse tile's overlap
        // band double-covers finer terrain, the fine surface always wins the depth test instead of
        // coarse patches poking through on slopes. Each ring sinks 4x more than the finer one, and the
        // skirts (3 cells) comfortably cover the offset at ring boundaries. Invisible at ring distances.
        const float coarseBias = 0.35f * cell;
        for (int k = 0; k < cx; ++k)
            for (int i = 0; i < cx; ++i)
            {
                const float wy = H(i, k) - coarseBias;
                // Neighbours are `cell` metres apart, so the gradient denominator scales with it.
                const vec3 n = glm::normalize(vec3(H(i - 1, k) - H(i + 1, k), 2.0f * cell, H(i, k - 1) - H(i, k + 1)));
                Vertex v{};
                v.position[0] = static_cast<float>(gx0 + i) * cell;
                v.position[1] = wy;
                v.position[2] = static_cast<float>(gz0 + k) * cell;
                v.normals[0] = n.x;
                v.normals[1] = n.y;
                v.normals[2] = n.z;
                v.uv[0] = v.position[0] / cell;
                v.uv[1] = v.position[2] / cell;
                v.tangent[0] = 1.0f;
                v.tangent[3] = 1.0f;
                const vec3 c = TerrainColor(wy, n.y);
                v.color[0] = c.x;
                v.color[1] = c.y;
                v.color[2] = c.z;
                v.color[3] = 1.0f;
                bbMin = glm::min(bbMin, vec3(v.position[0], wy, v.position[2]));
                bbMax = glm::max(bbMax, vec3(v.position[0], wy, v.position[2]));
                verts.push_back(v);
            }
        for (int k = 0; k < kTileCells; ++k)
            for (int i = 0; i < kTileCells; ++i)
            {
                const uint32_t a = static_cast<uint32_t>(k * cx + i);
                const uint32_t b = a + 1;
                const uint32_t c = a + cx;
                const uint32_t d = c + 1;
                // CCW seen from above (engine FrontFace=CW + cull FRONT keeps CCW-from-outside).
                indices.push_back(a);
                indices.push_back(c);
                indices.push_back(b);
                indices.push_back(b);
                indices.push_back(c);
                indices.push_back(d);
            }

        // Skirt: perimeter corners duplicated `kSkirtDropCells` cells down, quads emitted two-sided —
        // hides the height mismatch where this coarse ring meets the finer ring / the next ring out.
        const float drop = kSkirtDropCells * cell;
        auto addSkirtQuad = [&](uint32_t top0, uint32_t top1)
        {
            const uint32_t s0 = static_cast<uint32_t>(verts.size());
            Vertex v0 = verts[top0];
            Vertex v1 = verts[top1];
            v0.position[1] -= drop;
            v1.position[1] -= drop;
            bbMin.y = std::min({bbMin.y, v0.position[1], v1.position[1]});
            verts.push_back(v0);
            verts.push_back(v1);
            const uint32_t idx[12] = {top0, top1, s0 + 1, top0, s0 + 1, s0, // one side
                                      top1, top0, s0, top1, s0, s0 + 1};    // the other
            indices.insert(indices.end(), idx, idx + 12);
        };
        for (int i = 0; i < kTileCells; ++i)
        {
            addSkirtQuad(static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1));                                       // -Z edge
            addSkirtQuad(static_cast<uint32_t>(kTileCells * cx + i), static_cast<uint32_t>(kTileCells * cx + i + 1));   // +Z edge
            addSkirtQuad(static_cast<uint32_t>(i * cx), static_cast<uint32_t>((i + 1) * cx));                           // -X edge
            addSkirtQuad(static_cast<uint32_t>(i * cx + kTileCells), static_cast<uint32_t>((i + 1) * cx + kTileCells)); // +X edge
        }

        WriteTileContent(1, tile, verts, indices, bbMin, bbMax);
    }

    void TerrainWorld::WriteEmptyTile(Tile &tile)
    {
        std::vector<Vertex> &vertices = m_scene->GetVertexStore();
        std::vector<PositionUvVertex> &positionUvs = m_scene->GetPositionUvStore();
        std::vector<AabbVertex> &aabbVertices = m_scene->GetAabbVertexStore();
        std::vector<uint32_t> &indices = m_scene->GetIndexStore();

        // One degenerate triangle far below the world: the mesh slot keeps indexCount >= 3 (an empty
        // mesh would lose its indirect/constants slot on the next full rebuild) and the AABB culls it.
        Vertex v{};
        v.position[1] = -100000.0f;
        vertices[tile.vertexOffset] = v;
        PositionUvVertex pv{};
        pv.position[1] = -100000.0f;
        positionUvs[tile.vertexOffset] = pv;
        indices[tile.indexOffset] = 0;
        indices[tile.indexOffset + 1] = 0;
        indices[tile.indexOffset + 2] = 0;
        for (int c = 0; c < 8; ++c)
        {
            AabbVertex av{};
            av.position[1] = -100000.0f;
            aabbVertices[tile.aabbVertexOffset + c] = av;
        }

        Mesh &m = m_scene->GetMesh(tile.meshIndex);
        m.indexCount = 3;
        m.lodIndexOffset[0] = tile.indexOffset;
        m.lodIndexCount[0] = 3;
        m.lodCount = 1;
        m.boundingBox = {vec3(-0.5f, -100000.5f, -0.5f), vec3(0.5f, -99999.5f, 0.5f)};
        tile.liveVerts = 1;
        tile.liveIndices = 3;
    }

    bool TerrainWorld::WriteTileContent(int ringIdx, Tile &tile, std::vector<Vertex> &verts,
                                        std::vector<uint32_t> &indices, const vec3 &bbMin, const vec3 &bbMax)
    {
        if (verts.empty() || indices.size() < 3)
        {
            WriteEmptyTile(tile);
            return true;
        }
        if (verts.size() > tile.vertexBudget || indices.size() > tile.indexBudget)
        {
            if (!tile.growPending)
                PE_WARN("Terrain: tile (%d,%d) overflowed its budget (%zu/%u verts, %zu/%u idx) — growing.",
                        tile.tx, tile.tz, verts.size(), tile.vertexBudget, indices.size(), tile.indexBudget);
            tile.growPending = true;
            return false; // keep the old content until the grow re-places the ranges
        }

        Mesh &m = m_scene->GetMesh(tile.meshIndex);
        const uint32_t lod0Count = static_cast<uint32_t>(indices.size());
        m.lodIndexOffset[0] = tile.indexOffset;
        m.lodIndexCount[0] = lod0Count;
        m.lodCount = 1;

        // Ring-0 LOD chain via meshopt (border-locked so tile seams stay matched at every level),
        // appended after lod0 inside the tile's index budget; levels that no longer fit are dropped.
        if (ringIdx == 0 && lod0Count >= 256)
        {
            const float *positions = reinterpret_cast<const float *>(verts.data());
            static constexpr float kRatios[Mesh::kMaxLods] = {1.0f, 0.5f, 0.25f, 0.12f};
            std::vector<uint32_t> simplified(indices.size());
            uint32_t prevCount = lod0Count;
            for (uint32_t lod = 1; lod < Mesh::kMaxLods; ++lod)
            {
                const size_t target = (static_cast<size_t>(lod0Count * kRatios[lod]) / 3) * 3;
                if (target < 12 || indices.size() + target > tile.indexBudget)
                    break;
                float err = 0.0f;
                const size_t resCount =
                    meshopt_simplify(simplified.data(), indices.data(), lod0Count, positions, verts.size(),
                                     sizeof(Vertex), target, 0.1f, meshopt_SimplifyLockBorder, &err);
                if (resCount == 0 || resCount >= static_cast<size_t>(prevCount * 0.95f) ||
                    indices.size() + resCount > tile.indexBudget)
                    break;
                m.lodIndexOffset[lod] = tile.indexOffset + static_cast<uint32_t>(indices.size());
                m.lodIndexCount[lod] = static_cast<uint32_t>(resCount);
                m.lodCount = lod + 1;
                indices.insert(indices.end(), simplified.begin(), simplified.begin() + resCount);
                prevCount = static_cast<uint32_t>(resCount);
            }
        }

        std::vector<Vertex> &vertexStore = m_scene->GetVertexStore();
        std::vector<PositionUvVertex> &positionUvStore = m_scene->GetPositionUvStore();
        std::vector<AabbVertex> &aabbStore = m_scene->GetAabbVertexStore();
        std::vector<uint32_t> &indexStore = m_scene->GetIndexStore();
        for (size_t i = 0; i < verts.size(); ++i)
        {
            const Vertex &v = verts[i];
            vertexStore[tile.vertexOffset + i] = v;
            PositionUvVertex pv{};
            pv.position[0] = v.position[0];
            pv.position[1] = v.position[1];
            pv.position[2] = v.position[2];
            pv.uv[0] = v.uv[0];
            pv.uv[1] = v.uv[1];
            positionUvStore[tile.vertexOffset + i] = pv;
        }
        std::copy(indices.begin(), indices.end(), indexStore.begin() + tile.indexOffset);
        for (int c = 0; c < 8; ++c)
        {
            AabbVertex av{};
            av.position[0] = (c & 1) ? bbMax.x : bbMin.x;
            av.position[1] = (c & 2) ? bbMax.y : bbMin.y;
            av.position[2] = (c & 4) ? bbMax.z : bbMin.z;
            aabbStore[tile.aabbVertexOffset + c] = av;
        }

        m.indexCount = lod0Count;
        m.boundingBox = {bbMin, bbMax};
        tile.liveVerts = static_cast<uint32_t>(verts.size());
        tile.liveIndices = static_cast<uint32_t>(indices.size());
        return true;
    }

    void TerrainWorld::Update()
    {
        if (!m_scene || m_tiles.empty())
            return;
        RetireSubmittedCommands(false);

        if (!m_pendingSculpts.empty())
        {
            std::vector<QueuedSculpt> sculpts;
            sculpts.swap(m_pendingSculpts);
            for (const QueuedSculpt &s : sculpts)
                Sculpt(s.center, s.radius, s.amount);
        }
        StreamWindows();
        GrowOverflowedTiles();
        ProcessDirtyTiles();
        UpdateColliderRing();
    }

    void TerrainWorld::ProcessDirtyTiles()
    {
        struct Item
        {
            float key;
            int ringIdx;
            int tileIdx;
        };
        std::vector<Item> queue;
        for (size_t r = 0; r < m_rings.size(); ++r)
        {
            const Ring &ring = m_rings[r];
            const float tw = TileWorldSize(ring);
            for (int i = 0; i < ring.tilesX * ring.tilesZ; ++i)
            {
                const int t = ring.firstTile + i;
                const Tile &tile = m_tiles[t];
                if (!tile.dirty || tile.growPending)
                    continue;
                const float cxw = (tile.tx + 0.5f) * tw, czw = (tile.tz + 0.5f) * tw;
                const float dx = cxw - m_anchor.x, dz = czw - m_anchor.z;
                queue.push_back({std::sqrt(dx * dx + dz * dz) + static_cast<float>(r) * 1e6f,
                                 static_cast<int>(r), t});
            }
        }
        if (queue.empty())
            return;
        std::sort(queue.begin(), queue.end(), [](const Item &a, const Item &b)
                  { return a.key < b.key; });

        std::vector<int> uploaded;
        const int budget = std::min<int>(kMeshBudgetPerUpdate, static_cast<int>(queue.size()));
        for (int i = 0; i < budget; ++i)
        {
            Tile &tile = m_tiles[queue[i].tileIdx];
            MeshTile(queue[i].ringIdx, tile);
            tile.dirty = tile.growPending; // a grown tile re-meshes after its ranges are re-placed
            if (!tile.growPending)
                uploaded.push_back(queue[i].tileIdx);
        }
        if (uploaded.empty())
            return;

        Queue *q = RHII.GetMainQueue();
        if (!q)
            return;
        CommandBuffer *cmd = q->AcquireCommandBuffer();
        cmd->Begin();
        for (int t : uploaded)
        {
            Tile &tile = m_tiles[t];
            m_scene->UpdateStreamedMesh(m_hostNode, tile.refSlot, tile.meshIndex, tile.liveVerts,
                                        tile.liveIndices, cmd);
        }
        cmd->End();
        q->Submit(1, &cmd, nullptr, nullptr);
        m_submittedCmds.push_back(cmd);
    }

    // An overflow means the ring's shared budget estimate lost to this worldgen region — and slots are
    // reused toroidally, so every other slot of the ring is one stream step from the same overflow and
    // its own rebuild. Grow the WHOLE ring: doubled ranges appended to the stores (the old ranges leak
    // until the next scene load — bounded, logged, rare), live content copied across so only the
    // overflowed tiles re-mesh, and ONE geometry rebuild picks the layout up.
    void TerrainWorld::GrowOverflowedTiles()
    {
        bool any = false;
        for (const Tile &tile : m_tiles)
            any |= tile.growPending;
        if (!any)
            return;

        std::vector<Vertex> &vertices = m_scene->GetVertexStore();
        std::vector<PositionUvVertex> &positionUvs = m_scene->GetPositionUvStore();
        std::vector<uint32_t> &indices = m_scene->GetIndexStore();
        for (size_t r = 0; r < m_rings.size(); ++r)
        {
            const Ring &ring = m_rings[r];
            const int tileCount = ring.tilesX * ring.tilesZ;
            bool ringGrow = false;
            for (int i = 0; i < tileCount; ++i)
                ringGrow |= m_tiles[ring.firstTile + i].growPending;
            if (!ringGrow)
                continue;
            for (int i = 0; i < tileCount; ++i)
            {
                Tile &tile = m_tiles[ring.firstTile + i];
                const uint32_t oldVertexOffset = tile.vertexOffset;
                const uint32_t oldIndexOffset = tile.indexOffset;
                tile.vertexBudget *= 2;
                tile.indexBudget *= 2;
                tile.vertexOffset = static_cast<uint32_t>(vertices.size());
                vertices.resize(vertices.size() + tile.vertexBudget);
                positionUvs.resize(positionUvs.size() + tile.vertexBudget);
                tile.indexOffset = static_cast<uint32_t>(indices.size());
                indices.resize(indices.size() + tile.indexBudget, 0u);
                Mesh &m = m_scene->GetMesh(tile.meshIndex);
                m.vertexOffset = tile.vertexOffset;
                m.positionsOffset = tile.vertexOffset;
                m.vertexCount = tile.vertexBudget;
                m.indexOffset = tile.indexOffset;
                if (tile.growPending)
                {
                    tile.growPending = false;
                    WriteEmptyTile(tile); // valid placeholder in the new ranges for the flush
                    tile.dirty = true;    // real content streams back in next Update
                }
                else
                {
                    // Keep the tile's live content: copy the old ranges (indices are mesh-local, so
                    // they move verbatim) and re-base the LOD chain onto the new index range.
                    std::copy_n(vertices.begin() + oldVertexOffset, tile.liveVerts,
                                vertices.begin() + tile.vertexOffset);
                    std::copy_n(positionUvs.begin() + oldVertexOffset, tile.liveVerts,
                                positionUvs.begin() + tile.vertexOffset);
                    std::copy_n(indices.begin() + oldIndexOffset, tile.liveIndices,
                                indices.begin() + tile.indexOffset);
                    for (uint32_t lod = 0; lod < m.lodCount; ++lod)
                        m.lodIndexOffset[lod] = tile.indexOffset + (m.lodIndexOffset[lod] - oldIndexOffset);
                }
            }
            PE_INFO("Terrain: ring %zu outgrew its tile budgets — doubled all %d tiles (one geometry rebuild).",
                    r, tileCount);
        }
        m_scene->SetGeometryDirty();
        m_scene->FlushPendingGpuWork();
    }

    void TerrainWorld::Sculpt(const vec3 &center, float radius, float amount)
    {
        if (radius <= 0.0f || m_tiles.empty())
            return;
        const SculptOp op{center, radius, amount < 0.0f};
        m_ops.push_back(op);
        MarkSculptDirty(op);
    }

    void TerrainWorld::QueueSculpt(const vec3 &center, float radius, float amount)
    {
        if (radius > 0.0f)
            m_pendingSculpts.push_back({center, radius, amount});
    }

    void TerrainWorld::SetSculptOps(const std::vector<vec4> &ops)
    {
        m_ops.clear();
        m_ops.reserve(ops.size());
        for (const vec4 &o : ops)
            if (std::abs(o.w) > 0.0f)
                m_ops.push_back({vec3(o.x, o.y, o.z), std::abs(o.w), o.w < 0.0f});
    }

    void TerrainWorld::GetSculptOps(std::vector<vec4> &out) const
    {
        out.clear();
        out.reserve(m_ops.size());
        for (const SculptOp &op : m_ops)
            out.emplace_back(op.center, op.dig ? -op.radius : op.radius);
    }

    // Ring 0 tiles whose sampling window (extent + apron + guard) the op's sphere overlaps. Coarse
    // rings ignore sculpts (heightfield-only); a big dig pops at the fine window's rim, the same trade
    // the voxel LOD bands make with distant cave mouths. Ops on unloaded tiles apply when they stream
    // back in — the op list is the persistent truth, not the meshes.
    void TerrainWorld::MarkSculptDirty(const SculptOp &op)
    {
        const Ring &ring = m_rings[0];
        const float tw = TileWorldSize(ring);
        const float margin = 3.0f * ring.cellSize;
        const int tx0 = TileFloor(op.center.x - op.radius - margin, tw);
        const int tx1 = TileFloor(op.center.x + op.radius + margin, tw);
        const int tz0 = TileFloor(op.center.z - op.radius - margin, tw);
        const int tz1 = TileFloor(op.center.z + op.radius + margin, tw);
        for (int tz = tz0; tz <= tz1; ++tz)
            for (int tx = tx0; tx <= tx1; ++tx)
            {
                const int sx = WrapMod(tx, ring.tilesX);
                const int sz = WrapMod(tz, ring.tilesZ);
                Tile &tile = m_tiles[ring.firstTile + sz * ring.tilesX + sx];
                if (tile.tx == tx && tile.tz == tz) // that world tile is actually loaded in this slot
                {
                    tile.dirty = true;
                    tile.bodyDirty = true;
                }
            }
    }

    float TerrainWorld::SampleHeight(float x, float z) const
    {
        if (!m_generator)
            return m_cfg.groundHeight + m_cfg.heightMin;
        return m_generator->SurfaceHeight(x, z);
    }

    bool TerrainWorld::Raycast(const vec3 &o, const vec3 &d, float maxDist, vec3 &hitPoint, vec3 &hitNormal) const
    {
        if (!m_generator || glm::length(d) < 1e-6f)
            return false;
        const vec3 dir = glm::normalize(d);
        // ponytail: fixed half-cell march + linear crossing refine over the density field, so sculpted
        // craters and overhang undersides raycast correctly (a pure height march would not).
        const float step = 0.5f * std::max(0.05f, m_cfg.metersPerPixel);
        float prevT = 0.0f;
        float prevD = DensityAt(o.x, o.y, o.z);
        for (float t = step; t <= maxDist; t += step)
        {
            const vec3 p = o + dir * t;
            const float dEnd = DensityAt(p.x, p.y, p.z);
            if (prevD < 0.0f && dEnd >= 0.0f) // crossed air -> solid between prevT and t
            {
                const float f = prevD / (prevD - dEnd);
                hitPoint = o + dir * (prevT + f * (t - prevT));
                const float e = 0.5f * step;
                hitNormal = glm::normalize(vec3(
                    DensityAt(hitPoint.x - e, hitPoint.y, hitPoint.z) - DensityAt(hitPoint.x + e, hitPoint.y, hitPoint.z),
                    DensityAt(hitPoint.x, hitPoint.y - e, hitPoint.z) - DensityAt(hitPoint.x, hitPoint.y + e, hitPoint.z),
                    DensityAt(hitPoint.x, hitPoint.y, hitPoint.z - e) - DensityAt(hitPoint.x, hitPoint.y, hitPoint.z + e)));
                return true;
            }
            prevD = dEnd;
            prevT = t;
        }
        return false;
    }

    bool TerrainWorld::IsSolidCell(int x, int y, int z) const
    {
        return DensityAt((float)x + 0.5f, (float)y + 0.5f, (float)z + 0.5f) > 0.0f;
    }

    void TerrainWorld::UpdateColliderRing()
    {
#ifdef PE_PHYSICS
        auto *physics = GetGlobalSystem<PhysicsSystem>();
        if (!physics || m_rings.empty())
            return;
        const Ring &ring = m_rings[0];
        const float tw = TileWorldSize(ring);
        int cookBudget = kCookBudgetPerUpdate;
        const std::vector<Vertex> &vertexStore = m_scene->GetVertexStore();
        const std::vector<uint32_t> &indexStore = m_scene->GetIndexStore();
        for (int i = 0; i < ring.tilesX * ring.tilesZ; ++i)
        {
            Tile &tile = m_tiles[ring.firstTile + i];
            const float cxw = (tile.tx + 0.5f) * tw, czw = (tile.tz + 0.5f) * tw;
            const float dx = cxw - m_anchor.x, dz = czw - m_anchor.z;
            const bool inRange = m_cfg.collisionRadiusM <= 0.0f ||
                                 std::sqrt(dx * dx + dz * dz) <= m_cfg.collisionRadiusM + tw;
            const bool want = m_cfg.physics && inRange && !tile.dirty && !tile.interiorHole && tile.liveVerts > 1;
            if (!want)
            {
                if (tile.bodyId != 0xFFFFFFFF)
                    RemoveTileBody(tile);
                continue;
            }
            if ((tile.bodyId == 0xFFFFFFFF || tile.bodyDirty) && cookBudget > 0)
            {
                RemoveTileBody(tile);
                const Mesh &m = m_scene->GetMesh(tile.meshIndex);
                tile.bodyId = physics->AddStaticMeshBody(vertexStore.data() + tile.vertexOffset, tile.liveVerts,
                                                         indexStore.data() + tile.indexOffset,
                                                         m.indexCount, // lod0 only
                                                         m_cfg.physicsFriction, m_cfg.physicsRestitution);
                tile.bodyDirty = false;
                --cookBudget;
            }
        }
#endif
    }

    void TerrainWorld::RemoveTileBody(Tile &tile)
    {
#ifdef PE_PHYSICS
        if (tile.bodyId == 0xFFFFFFFF)
            return;
        if (auto *physics = GetGlobalSystem<PhysicsSystem>())
            physics->RemoveStaticMeshBody(tile.bodyId);
        tile.bodyId = 0xFFFFFFFF;
#else
        (void)tile;
#endif
    }

    void TerrainWorld::SetPhysicsEnabled(bool enabled)
    {
        m_cfg.physics = enabled;
        if (!enabled)
            for (Tile &tile : m_tiles)
                RemoveTileBody(tile);
        // Enabled: the collider ring fills back up over the next Updates (budgeted cooks).
    }

    void TerrainWorld::SetTerrainMaterial(float friction, float restitution)
    {
        m_cfg.physicsFriction = friction;
        m_cfg.physicsRestitution = restitution;
#ifdef PE_PHYSICS
        if (auto *physics = GetGlobalSystem<PhysicsSystem>())
            for (Tile &tile : m_tiles)
                if (tile.bodyId != 0xFFFFFFFF)
                    physics->SetStaticMeshBodyMaterial(tile.bodyId, friction, restitution);
#endif
    }

    void TerrainWorld::RetireSubmittedCommands(bool all)
    {
        while (!m_submittedCmds.empty() && (all || m_submittedCmds.size() > kMaxPendingCmds))
        {
            CommandBuffer *cmd = m_submittedCmds.front();
            cmd->Wait();
            cmd->Return();
            m_submittedCmds.erase(m_submittedCmds.begin());
        }
    }

    bool TerrainWorld::IsAlive() const
    {
        if (!m_scene)
            return false;
        return m_hostNode != nullptr && m_scene->IsNodeAlive(m_hostNode) && !m_tiles.empty() &&
               m_scene->IsValidMeshIndex(m_tiles.front().meshIndex);
    }

    void TerrainWorld::Destroy()
    {
        RetireSubmittedCommands(true);
        for (Tile &tile : m_tiles)
        {
            RemoveTileBody(tile);
            if (m_scene && tile.meshIndex >= 0 && m_scene->IsValidMeshIndex(tile.meshIndex))
                m_scene->GetMesh(tile.meshIndex).material = nullptr;
        }
        if (m_scene && m_hostNode && m_scene->IsNodeAlive(m_hostNode))
            m_scene->DeleteNode(m_hostNode);

        m_material.reset();
        m_hostNode = nullptr;
        m_tiles.clear();
        m_rings.clear();
        m_ops.clear();
        m_pendingSculpts.clear();
        m_cavesMap.reset();
        m_cavesExtentX = m_cavesExtentZ = 0.0f;
        m_anchorSet = false;
        m_scene = nullptr;
    }
} // namespace pe::terrain
