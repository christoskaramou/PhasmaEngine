#include "Terrain/TerrainWorld.h"
#include "API/Command.h"
#include "API/Image.h" // triplanar layer/splat textures
#include "API/Queue.h"
#include "API/RHI.h"
#include "Scene/Material.h"
#include "Scene/ModelAsset.h" // scatter templates baked from model assets
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Voxel/ColumnChunkStore.h" // ResolveRoot: Assets-relative or absolute asset paths
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
        // Scatter caps: a tile stops stamping instances at this many prop verts (and the budget
        // histogram clamps to it), so a dense paint can never grow-loop the ring. Template meshes
        // above the vert cap are rejected — props must be low-poly, they are baked per instance.
        constexpr uint32_t kMaxScatterVertsPerTile = 8192;
        constexpr uint32_t kMaxScatterTemplateVerts = 2500;

        // Deterministic per-pixel hash driving scatter yaw/scale (MapGen::FeatureHash's constants).
        uint32_t ScatterHash(int x, int z)
        {
            return static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(z) * 19349663u;
        }

        // Hash of the config fields that shape the MESH (and therefore per-tile vertex demand) — the
        // key for the grow memory. Scatter/physics/sea-level are excluded on purpose: changing them
        // recreates the world but leaves the demand (and the remembered budget) valid.
        uint64_t BudgetHash(const TerrainConfig &c)
        {
            uint64_t h = 1469598103934665603ull;
            const auto mix = [&h](uint64_t v)
            { h = (h ^ v) * 1099511628211ull; };
            const auto mixF = [&](float f)
            { mix(static_cast<uint64_t>(static_cast<int64_t>(f * 16.0f))); };
            const auto mixS = [&](const std::string &s)
            {
                mix(s.size());
                for (char ch : s)
                    mix(static_cast<uint8_t>(ch));
            };
            mix(static_cast<uint64_t>(static_cast<int64_t>(c.sizeXMeters)));
            mix(static_cast<uint64_t>(static_cast<int64_t>(c.sizeZMeters)));
            mix(static_cast<uint64_t>(static_cast<int64_t>(c.boundsCenterCx)));
            mix(static_cast<uint64_t>(static_cast<int64_t>(c.boundsCenterCz)));
            mixF(c.groundHeight);
            mixF(c.heightMin);
            mixF(c.heightMax);
            mixS(c.heightmapPath);
            mixS(c.cavesPath);
            mixF(c.noiseFeatureScale);
            mix(static_cast<uint64_t>(static_cast<int64_t>(c.noiseSeed)));
            mixF(c.metersPerPixel);
            mix(c.streaming ? 1u : 0u);
            mixF(c.overhangs);
            return h;
        }
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

        // Scatter kind -> template mesh: a builtin procedural prop, else a model asset baked to
        // template space (node transforms + material base colour folded into the vertices; base
        // dropped to y = 0 so it sits on the surface). Empty template = kind is skipped.
        ScatterTemplate BuildTemplateFor(const std::string &name)
        {
            ScatterTemplate t = BuildScatterTemplate(name);
            if (!t.vertices.empty() || name.empty())
                return t;
            ModelAsset *model = ModelAsset::Load(voxel::ColumnChunkStore::ResolveRoot(name));
            if (!model)
                return t; // Load already warned
            const std::vector<Vertex> &mv = model->GetVertices();
            const std::vector<uint32_t> &mi = model->GetIndices();
            for (int n = 0; n < model->GetNodeCount(); ++n)
            {
                const int meshIdx = model->GetNodeMesh(n);
                const MeshInfo *info = meshIdx >= 0 ? model->GetMeshInfo(meshIdx) : nullptr;
                if (!info || info->vertexOffset + info->verticesCount > mv.size() ||
                    info->indexOffset + info->indicesCount > mi.size())
                    continue;
                mat4 world = model->GetNodeLocalMatrix(n);
                for (int p = model->GetNodeParentIndex(n); p >= 0; p = model->GetNodeParentIndex(p))
                    world = model->GetNodeLocalMatrix(p) * world;
                const mat3 nrm = mat3(world); // approximate under non-uniform scale
                const vec4 tint = info->material ? info->material->baseColorFactor : vec4(1.0f);
                const uint32_t base = static_cast<uint32_t>(t.vertices.size());
                for (uint32_t i = 0; i < info->verticesCount; ++i)
                {
                    const Vertex &src = mv[info->vertexOffset + i];
                    Vertex v = src;
                    const vec3 wp = vec3(world * vec4(src.position[0], src.position[1], src.position[2], 1.0f));
                    vec3 wn = nrm * vec3(src.normals[0], src.normals[1], src.normals[2]);
                    wn = glm::length(wn) > 1e-6f ? glm::normalize(wn) : vec3(0.0f, 1.0f, 0.0f);
                    for (int c = 0; c < 3; ++c)
                    {
                        v.position[c] = wp[c];
                        v.normals[c] = wn[c];
                    }
                    for (int c = 0; c < 4; ++c)
                    {
                        v.color[c] = src.color[c] * tint[c];
                        v.joints[c] = 0;
                        v.weights[c] = 0.0f;
                    }
                    t.vertices.push_back(v);
                }
                for (uint32_t i = 0; i < info->indicesCount; ++i)
                    t.indices.push_back(base + mi[info->indexOffset + i]);
            }
            delete model;
            if (t.vertices.size() > kMaxScatterTemplateVerts)
            {
                PE_WARN("Terrain: scatter mesh '%s' has %zu verts (cap %u) — use a low-poly prop, it is "
                        "baked per painted instance.",
                        name.c_str(), t.vertices.size(), kMaxScatterTemplateVerts);
                return {};
            }
            if (!t.vertices.empty())
            {
                float minY = 1e30f;
                for (const Vertex &v : t.vertices)
                    minY = std::min(minY, v.position[1]);
                for (Vertex &v : t.vertices)
                    v.position[1] -= minY;
            }
            t.collide = true;
            return t;
        }
    } // namespace

    TerrainWorld::TerrainWorld() = default;
    TerrainWorld::~TerrainWorld()
    {
        Destroy();
    }

    void TerrainWorld::SetTerrainGenerator(std::shared_ptr<voxel::ITerrainGenerator> generator)
    {
        DrainMeshWorker(); // the mesher samples m_generator — quiesce before swapping it
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
        m_hasHeightmapFootprint = false;
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
                m_heightmapCenterX = m_cfg.boundsCenterCx * kBlocksPerColumn + kBlocksPerColumn / 2.0f;
                m_heightmapCenterZ = m_cfg.boundsCenterCz * kBlocksPerColumn + kBlocksPerColumn / 2.0f;
                m_heightmapExtentX = static_cast<float>(mapGen->MapBlocksX());
                m_heightmapExtentZ = static_cast<float>(mapGen->MapBlocksZ());
                m_hasHeightmapFootprint = m_heightmapExtentX > 0.0f && m_heightmapExtentZ > 0.0f;
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

    void TerrainWorld::LoadScatter()
    {
        m_scatterMap.reset();
        m_templates.clear();
        m_scatterBudgetVerts = 0;
        m_scatterExtentX = m_scatterExtentZ = 0.0f;
        if (m_cfg.scatterPath.empty() || m_cfg.scatterMeshes.empty())
            return;
        auto map = std::make_unique<voxel::MapImage>();
        if (!map->Load(m_cfg.scatterPath, "scatter", false)) // plain 0..255 ids; Load warns on failure
            return;
        for (const std::string &name : m_cfg.scatterMeshes)
            m_templates.push_back(BuildTemplateFor(name));
        m_scatterCenterX = m_cfg.boundsCenterCx * kBlocksPerColumn + kBlocksPerColumn / 2.0f;
        m_scatterCenterZ = m_cfg.boundsCenterCz * kBlocksPerColumn + kBlocksPerColumn / 2.0f;
        m_scatterExtentX = map->w * m_cfg.metersPerPixel;
        m_scatterExtentZ = map->h * m_cfg.metersPerPixel;

        // Worst per-tile vertex demand feeds the shared ring-0 budget (tiles of a ring share one
        // estimate) — measured from the actual paint, so ordinary maps never grow at load.
        const float tw = m_cfg.metersPerPixel * kTileCells;
        std::unordered_map<uint64_t, uint32_t> demand;
        size_t instances = 0;
        for (int pz = 0; pz < map->h; ++pz)
            for (int px = 0; px < map->w; ++px)
            {
                const uint8_t id = map->px[static_cast<size_t>(pz) * map->w + px];
                if (id == 0 || id > m_templates.size() || m_templates[id - 1].vertices.empty())
                    continue;
                const float ax = m_scatterCenterX + m_scatterExtentX * (0.5f - (px + 0.5f) / map->w);
                const float az = m_scatterCenterZ + m_scatterExtentZ * (0.5f - (pz + 0.5f) / map->h);
                const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(TileFloor(ax, tw))) << 32) |
                                     static_cast<uint32_t>(TileFloor(az, tw));
                demand[key] += static_cast<uint32_t>(m_templates[id - 1].vertices.size());
                ++instances;
            }
        for (const auto &[key, verts] : demand)
            m_scatterBudgetVerts = std::max(m_scatterBudgetVerts, verts);
        if (m_scatterBudgetVerts > kMaxScatterVertsPerTile)
        {
            PE_WARN("Terrain: scatter paint demands up to %u verts in one tile; capped at %u — the "
                    "densest tiles will drop instances.",
                    m_scatterBudgetVerts, kMaxScatterVertsPerTile);
            m_scatterBudgetVerts = kMaxScatterVertsPerTile;
        }
        m_scatterMap = std::move(map);
        PE_INFO("Terrain: scatter map %dx%d, %zu instances, +%u verts/tile budget.", m_scatterMap->w,
                m_scatterMap->h, instances, m_scatterBudgetVerts);
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
        // Ops seeded via SetOps before Create survive the reset — Create-time meshing must
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
        m_cfg.textureScaleM = std::max(0.05f, m_cfg.textureScaleM);
        if (m_scene)
            m_scene->SetTerrainTexScale(m_cfg.textureScaleM);

        BuildGenerator();
        LoadCavesMap();
        LoadScatter();
        if (m_cfg.sizeXMeters <= 0)
            m_cfg.sizeXMeters = 256; // no map + no explicit size: a default patch
        if (m_cfg.sizeZMeters <= 0)
            m_cfg.sizeZMeters = 256;

        if (!m_anchorSet)
            m_anchor = vec3(m_cfg.boundsCenterCx * kBlocksPerColumn + kBlocksPerColumn / 2, 0.0f,
                            m_cfg.boundsCenterCz * kBlocksPerColumn + kBlocksPerColumn / 2);

        if (!m_scene)
            return;

        // A different worldgen invalidates the grow memory; the same one reuses its grown budget.
        const uint64_t budgetHash = BudgetHash(m_cfg);
        if (budgetHash != m_budgetHash)
        {
            m_budgetHash = budgetHash;
            m_grownRing0Budget = 0;
        }

        m_material = std::make_unique<Material>();
        m_material->name = "TerrainMaterial";
        m_material->baseColorFactor = vec4(1.0f); // white — vertex colour is a tint over the layers
        m_material->metallic = 0.0f;
        m_material->roughness = 1.0f;
        m_material->occlusionStrength = 1.0f;
        m_material->textureMask = 0u;
        m_material->renderType = RenderType::Opaque;

        // Triplanar splat: the terrain draws through the dedicated TerrainGBufferPS (the mesher packs
        // normalized height into color.a for auto layer selection). Two conditions gate it — the shader
        // assets must be deployed AND the layer textures must load; either failing keeps terrain=false
        // and the standard pipeline + per-vertex band colours. Checking the passinfo/PS actually exist
        // (not just relying on the GbufferPass PE_WARN) matters: CullingCS pulls terrain=true meshes out
        // of the opaque buckets (editorFlags 0x10) into bucket 8, so if the shader never built, bucket 8
        // is undrawn and terrain would silently vanish instead of falling back.
        const bool terrainShaderPresent =
            AssetFileExists(Path::RuntimeAssets + "Shaders/Terrain/terrain_gbuffer.passinfo") &&
            AssetFileExists(Path::RuntimeAssets + "Shaders/Terrain/TerrainGBufferPS.hlsl");
        if (terrainShaderPresent && BuildTerrainTextures())
        {
            m_material->terrain = true;
            // Ray tracing has no triplanar path — its closest-hit shades terrain as color.rgb *
            // baseColorFactor with no albedo texture, and color.rgb is now the raster tint (~white).
            // Give the RT/reflection view a representative ground albedo here; TerrainGBufferPS ignores
            // baseColorFactor, so the raster view is unchanged.
            m_material->baseColorFactor = vec4(0.38f, 0.36f, 0.28f, 1.0f);
        }
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
                const TileJob job{ring.firstTile + i, static_cast<int>(r), tile.tx, tile.tz,
                                  tile.interiorHole, tile.vertexBudget, tile.indexBudget};
                CommitTileMesh(tile, MeshTile(job));
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

        // Everything the mesher reads is now built; later streaming/sculpt meshing runs off the main
        // thread (Update commits + uploads the results). Create-time meshing above stayed synchronous.
        StartMeshWorker();
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
                float slack = 1.5f + 2.5f * m_cfg.overhangs;
                // Painted caves fold the surface into extra sheets (floor + ceiling + walls ≈ 3
                // surfaces, each tracking its own slope) which the flat-surface estimate cannot
                // see — cave scenes used to grow at every create. Measured: a full-paint cave tile
                // runs ~3.6x the column count.
                if (m_cavesMap)
                    slack += 2.5f;
                // Scatter props share the tile ranges: the measured worst-tile demand rides on top.
                vertexBudget = RoundUpTo((uint32_t)(baseCols * slack) + m_scatterBudgetVerts, 256);
                // Grow memory: a world this session already grew starts from its grown budget.
                vertexBudget = std::max(vertexBudget, m_grownRing0Budget);
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

    bool TerrainWorld::TileOutsideHeightmapFootprint(const Ring &ring, int tx, int tz) const
    {
        if (!m_hasHeightmapFootprint)
            return false;
        const float tw = TileWorldSize(ring);
        const float tMinX = static_cast<float>(tx) * tw;
        const float tMaxX = static_cast<float>(tx + 1) * tw;
        const float tMinZ = static_cast<float>(tz) * tw;
        const float tMaxZ = static_cast<float>(tz + 1) * tw;
        const float mMinX = m_heightmapCenterX - m_heightmapExtentX * 0.5f;
        const float mMaxX = m_heightmapCenterX + m_heightmapExtentX * 0.5f;
        const float mMinZ = m_heightmapCenterZ - m_heightmapExtentZ * 0.5f;
        const float mMaxZ = m_heightmapCenterZ + m_heightmapExtentZ * 0.5f;
        return tMaxX <= mMinX || tMinX >= mMaxX || tMaxZ <= mMinZ || tMinZ >= mMaxZ;
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
        // Brush ops, in stroke order. Spheres: the in-sphere-only application keeps the zero set exact
        // and every tile computes the identical field; the (cheap) price is slightly faceted crater
        // rims versus a full SDF blend. Level ops blend the density toward the plane y = targetY with
        // a quartic falloff (C1 at the rim), so flatten/smooth strokes leave no seam.
        // ponytail: linear scan; bucket ops per tile if counts ever hurt.
        for (const SculptOp &op : m_ops)
        {
            const float dx = x - op.center.x, dy = y - op.center.y, dz = z - op.center.z;
            const float dd = dx * dx + dy * dy + dz * dz;
            const float rr = op.radius * op.radius;
            if (dd > rr)
                continue;
            if (op.level)
            {
                const float q = 1.0f - dd / rr;
                d += ((op.targetY - y) - d) * (q * q * op.weight);
            }
            else
            {
                const float s = op.radius - std::sqrt(dd); // > 0 inside the sphere
                d = op.dig ? std::min(d, -s) : std::max(d, s);
            }
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

    TerrainWorld::TileMesh TerrainWorld::MeshTile(const TileJob &job) const
    {
        TileMesh out;
        out.tileIdx = job.tileIdx;
        out.tx = job.tx;
        out.tz = job.tz;
        out.valid = true;
        if (job.interiorHole || TileOutsideHeightmapFootprint(m_rings[job.ringIdx], job.tx, job.tz))
        {
            out.empty = true;
            return out;
        }
        if (job.ringIdx == 0)
            MeshTileSurfaceNets(m_rings[0], job, out);
        else
            MeshTileGrid(m_rings[job.ringIdx], job, out);
        return out;
    }

    void TerrainWorld::MeshTileSurfaceNets(const Ring &ring, const TileJob &job, TileMesh &out) const
    {
        const float cell = ring.cellSize;
        // One-cell negative apron stitches quads across tile seams (see SurfaceNetsTile); the missing
        // apron at bounded world edges just drops the outermost cell ring of the rim.
        const ivec3 apron(1, 0, 1);
        const int cellsXZ = kTileCells + 1;
        const int gx0 = job.tx * kTileCells - apron.x;
        const int gz0 = job.tz * kTileCells - apron.z;

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
            if (op.level) // the surface can move all the way to the level target
            {
                minY = std::min(minY, op.targetY - 2.0f);
                maxY = std::max(maxY, op.targetY + 2.0f);
            }
        }
        minY -= 2.0f * cell;
        maxY += 2.0f * cell;
        const int gy0 = (int)std::floor(minY / cell) - 1;
        int cellsY = (int)std::ceil(maxY / cell) + 1 - gy0;
        if (cellsY > kMaxFieldCellsY)
        {
            PE_WARN("Terrain: tile (%d,%d) vertical band %d cells exceeds the %d cap; clamped.",
                    job.tx, job.tz, cellsY, kMaxFieldCellsY);
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
            out.empty = true;
            return;
        }
        // Triplanar splat path (m_material->terrain): vertex colour is a tint, uv is the splat coord
        // over the terrain footprint (both axes flipped, like the caves/scatter maps), and color.a
        // packs the normalized surface height for the shader's auto layer selection.
        const bool textured = m_material && m_material->terrain;
        const float splatCx = m_cfg.boundsCenterCx * kBlocksPerColumn + kBlocksPerColumn / 2.0f;
        const float splatCz = m_cfg.boundsCenterCz * kBlocksPerColumn + kBlocksPerColumn / 2.0f;
        const float splatInvX = 1.0f / std::max(1.0f, static_cast<float>(m_cfg.sizeXMeters));
        const float splatInvZ = 1.0f / std::max(1.0f, static_cast<float>(m_cfg.sizeZMeters));
        const float heightBase = m_cfg.groundHeight + m_cfg.heightMin;
        const float heightSpan = std::max(1.0f, m_cfg.heightMax - m_cfg.heightMin);
        for (Vertex &v : mesh.vertices)
        {
            // Local surface height, bilinear from the corner-column cache (heights[i,k] holds the
            // column at world (gx0 + i - 1) * cell).
            const float li = std::clamp(v.position[0] / cell - gx0 + 1.0f, 0.0f, static_cast<float>(hnx - 1));
            const float lk = std::clamp(v.position[2] / cell - gz0 + 1.0f, 0.0f, static_cast<float>(hnz - 1));
            const int i0 = std::min(static_cast<int>(li), hnx - 2), k0 = std::min(static_cast<int>(lk), hnz - 2);
            const float fx = li - i0, fz = lk - k0;
            const auto Hc = [&](int i, int k)
            { return heights[static_cast<size_t>(i) + static_cast<size_t>(hnx) * k]; };
            const float hSurf = (Hc(i0, k0) * (1.0f - fx) + Hc(i0 + 1, k0) * fx) * (1.0f - fz) +
                                (Hc(i0, k0 + 1) * (1.0f - fx) + Hc(i0 + 1, k0 + 1) * fx) * fz;

            // Interior detection: cave floors/walls (solid overhead) and undersides read as rock, not
            // the height band — painted caves and grottos were grass-green inside.
            const float underBlend = std::clamp((-v.normals[1] - 0.1f) / 0.5f, 0.0f, 1.0f);
            float rockBlend = underBlend;
            const float depth = hSurf - v.position[1];
            if (depth > 0.5f &&
                (DensityLocal(v.position[0], v.position[1] + 4.0f, v.position[2], hSurf) > 0.0f ||
                 DensityLocal(v.position[0], v.position[1] + 8.0f, v.position[2], hSurf) > 0.0f))
                rockBlend = std::max(rockBlend, std::clamp((depth - 0.5f) / 2.0f, 0.0f, 1.0f));

            if (textured)
            {
                // TerrainGBufferPS textures the surface; vertex colour is only a TINT (white default,
                // darkened for cave interiors, blued underwater). uv = 0..1 splat coord over the
                // terrain footprint (both axes flipped, like the caves/scatter maps).
                vec3 tint(1.0f);
                if (rockBlend > 0.0f)
                    tint = glm::mix(tint, vec3(0.5f, 0.46f, 0.42f), rockBlend);
                if (v.position[1] < m_cfg.seaLevelM)
                    tint = glm::mix(tint, vec3(0.40f, 0.55f, 0.75f), 0.5f);
                v.color[0] = tint.x;
                v.color[1] = tint.y;
                v.color[2] = tint.z;
                // >= 0.5 marks a ground vertex (scatter props set < 0.5); the fraction carries the
                // normalized surface height the shader keys its auto layer selection on.
                const float fNorm = std::clamp((v.position[1] - heightBase) / heightSpan, 0.0f, 1.0f);
                v.color[3] = 0.5f + 0.5f * fNorm;
                v.uv[0] = 0.5f - (v.position[0] - splatCx) * splatInvX;
                v.uv[1] = 0.5f - (v.position[2] - splatCz) * splatInvZ;
            }
            else
            {
                vec3 c = TerrainColor(v.position[1], v.normals[1]);
                if (rockBlend > 0.0f)
                    c = glm::mix(c, vec3(0.33f, 0.29f, 0.25f), rockBlend);
                v.color[0] = c.x;
                v.color[1] = c.y;
                v.color[2] = c.z;
                v.color[3] = 1.0f;
                v.uv[0] = v.position[0] / cell;
                v.uv[1] = v.position[2] / cell;
            }
        }
        uint32_t collideEnd = static_cast<uint32_t>(mesh.indices.size());
        AppendScatter(ring, job, mesh.vertices, mesh.indices, mesh.aabbMin, mesh.aabbMax, collideEnd);
        BuildTileMesh(0, job, mesh.vertices, mesh.indices, mesh.aabbMin, mesh.aabbMax, collideEnd, out);
    }

    void TerrainWorld::AppendScatter(const Ring &ring, const TileJob &job, std::vector<Vertex> &verts,
                                     std::vector<uint32_t> &indices, vec3 &bbMin, vec3 &bbMax,
                                     uint32_t &collideEnd) const
    {
        collideEnd = static_cast<uint32_t>(indices.size());
        if (!m_scatterMap || m_templates.empty() || !m_generator)
            return;
        // TerrainGBufferPS keys ground-vs-prop off color.a: scatter props get < 0.5 so the shader
        // keeps their baked colour instead of texturing them as ground.
        const bool textured = m_material && m_material->terrain;
        const float tw = TileWorldSize(ring);
        const float x0 = job.tx * tw, x1 = x0 + tw;
        const float z0 = job.tz * tw, z1 = z0 + tw;
        // Inverse of the map transform (both axes flipped, texel centres at (p + 0.5) / dim): the
        // pixel range whose centres can fall inside the tile rect.
        const int w = m_scatterMap->w, h = m_scatterMap->h;
        const auto pxOfX = [&](float x)
        { return (0.5f - (x - m_scatterCenterX) / m_scatterExtentX) * w - 0.5f; };
        const auto pzOfZ = [&](float z)
        { return (0.5f - (z - m_scatterCenterZ) / m_scatterExtentZ) * h - 0.5f; };
        const int pxMin = std::max(0, static_cast<int>(std::ceil(std::min(pxOfX(x0), pxOfX(x1)))));
        const int pxMax = std::min(w - 1, static_cast<int>(std::floor(std::max(pxOfX(x0), pxOfX(x1)))));
        const int pzMin = std::max(0, static_cast<int>(std::ceil(std::min(pzOfZ(z0), pzOfZ(z1)))));
        const int pzMax = std::min(h - 1, static_cast<int>(std::floor(std::max(pzOfZ(z0), pzOfZ(z1)))));
        if (pxMin > pxMax || pzMin > pzMax)
            return;

        const size_t vertsBase = verts.size();
        bool capped = false;
        // Overhang band bound for the surface-snap march (same fixed point the mesher band uses).
        float bandTop = 2.0f;
        if (m_cfg.overhangs > 0.0f)
        {
            const float band = 0.5f * std::max(2.0f, m_cfg.heightMax - m_cfg.heightMin);
            const float cRel = m_cfg.overhangs;
            bandTop += band * (-1.0f + std::sqrt(1.0f + 4.0f * cRel * cRel)) / (2.0f * cRel);
        }
        const float bandBottom = bandTop + kCaveRoofDepthM + kCaveHeightM + 12.0f;
        const float step = 0.5f * ring.cellSize;

        // Colliding kinds first, so the tile collider cooks a contiguous lod0 prefix.
        for (int pass = 0; pass < 2 && !capped; ++pass)
        {
            for (int pz = pzMin; pz <= pzMax && !capped; ++pz)
                for (int px = pxMin; px <= pxMax; ++px)
                {
                    const uint8_t id = m_scatterMap->px[static_cast<size_t>(pz) * w + px];
                    if (id == 0 || id > m_templates.size())
                        continue;
                    const ScatterTemplate &tmpl = m_templates[id - 1];
                    if (tmpl.vertices.empty() || tmpl.collide != (pass == 0))
                        continue;
                    const float ax = m_scatterCenterX + m_scatterExtentX * (0.5f - (px + 0.5f) / w);
                    const float az = m_scatterCenterZ + m_scatterExtentZ * (0.5f - (pz + 0.5f) / h);
                    if (ax < x0 || ax >= x1 || az < z0 || az >= z1) // anchor tile owns the instance
                        continue;
                    if (verts.size() - vertsBase + tmpl.vertices.size() > kMaxScatterVertsPerTile)
                    {
                        PE_WARN("Terrain: tile (%d,%d) hit the %u scatter-vert cap — dropping instances.",
                                job.tx, job.tz, kMaxScatterVertsPerTile);
                        capped = true;
                        break;
                    }

                    // Snap to the TRUE surface: march the density down through overhang/cave/sculpt
                    // space. No crossing = the ground here was carved away; skip the prop.
                    const float hSurf = m_generator->SurfaceHeight(ax, az);
                    float ground = hSurf;
                    bool found = false;
                    float prevD = DensityLocal(ax, hSurf + bandTop, az, hSurf);
                    for (float y = hSurf + bandTop - step; y >= hSurf - bandBottom; y -= step)
                    {
                        const float d = DensityLocal(ax, y, az, hSurf);
                        if (prevD < 0.0f && d >= 0.0f)
                        {
                            const float f = prevD / (prevD - d);
                            ground = y + step - f * step;
                            found = true;
                            break;
                        }
                        prevD = d;
                    }
                    if (!found || ground < m_cfg.seaLevelM)
                        continue; // carved away or underwater

                    const uint32_t hash = ScatterHash(px, pz);
                    const float yaw = static_cast<float>(hash & 0xFFFFu) * (6.2831853f / 65536.0f);
                    const float scale = 0.8f + static_cast<float>((hash >> 16) & 0x3FFu) * (0.5f / 1024.0f);
                    const float cy = std::cos(yaw), sy = std::sin(yaw);
                    const float baseY = ground - 0.15f * scale; // sink a touch so slopes don't float it
                    const uint32_t base = static_cast<uint32_t>(verts.size());
                    for (const Vertex &tv : tmpl.vertices)
                    {
                        Vertex v = tv;
                        const float lx = tv.position[0] * scale, ly = tv.position[1] * scale,
                                    lz = tv.position[2] * scale;
                        v.position[0] = ax + cy * lx + sy * lz;
                        v.position[1] = baseY + ly;
                        v.position[2] = az - sy * lx + cy * lz;
                        v.normals[0] = cy * tv.normals[0] + sy * tv.normals[2];
                        v.normals[2] = -sy * tv.normals[0] + cy * tv.normals[2];
                        v.tangent[0] = cy * tv.tangent[0] + sy * tv.tangent[2];
                        v.tangent[2] = -sy * tv.tangent[0] + cy * tv.tangent[2];
                        if (textured)
                            v.color[3] = 0.0f; // prop marker for TerrainGBufferPS
                        bbMin = glm::min(bbMin, vec3(v.position[0], v.position[1], v.position[2]));
                        bbMax = glm::max(bbMax, vec3(v.position[0], v.position[1], v.position[2]));
                        verts.push_back(v);
                    }
                    for (const uint32_t idx : tmpl.indices)
                        indices.push_back(base + idx);
                }
            if (pass == 0)
                collideEnd = static_cast<uint32_t>(indices.size());
        }
    }

    void TerrainWorld::MeshTileGrid(const Ring &ring, const TileJob &job, TileMesh &out) const
    {
        const float cell = ring.cellSize;
        const int gx0 = job.tx * kTileCells;
        const int gz0 = job.tz * kTileCells;
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
        // Splat-terrain packing invariants, hoisted out of the per-vertex loop (matches the fine ring).
        const bool textured = m_material && m_material->terrain;
        const float heightBase = m_cfg.groundHeight + m_cfg.heightMin;
        const float heightSpan = std::max(1.0f, m_cfg.heightMax - m_cfg.heightMin);
        const float splatCx = m_cfg.boundsCenterCx * kBlocksPerColumn + kBlocksPerColumn / 2.0f;
        const float splatCz = m_cfg.boundsCenterCz * kBlocksPerColumn + kBlocksPerColumn / 2.0f;
        const float splatInvX = 1.0f / std::max(1.0f, static_cast<float>(m_cfg.sizeXMeters));
        const float splatInvZ = 1.0f / std::max(1.0f, static_cast<float>(m_cfg.sizeZMeters));
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
                v.tangent[0] = 1.0f;
                v.tangent[3] = 1.0f;
                if (textured)
                {
                    vec3 tint(1.0f);
                    if (wy < m_cfg.seaLevelM)
                        tint = glm::mix(tint, vec3(0.40f, 0.55f, 0.75f), 0.5f);
                    v.color[0] = tint.x;
                    v.color[1] = tint.y;
                    v.color[2] = tint.z;
                    v.color[3] = 0.5f + 0.5f * std::clamp((wy - heightBase) / heightSpan, 0.0f, 1.0f);
                    v.uv[0] = 0.5f - (v.position[0] - splatCx) * splatInvX;
                    v.uv[1] = 0.5f - (v.position[2] - splatCz) * splatInvZ;
                }
                else
                {
                    const vec3 c = TerrainColor(wy, n.y);
                    v.color[0] = c.x;
                    v.color[1] = c.y;
                    v.color[2] = c.z;
                    v.color[3] = 1.0f;
                    v.uv[0] = v.position[0] / cell;
                    v.uv[1] = v.position[2] / cell;
                }
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

        BuildTileMesh(1, job, verts, indices, bbMin, bbMax, static_cast<uint32_t>(indices.size()), out);
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
        tile.collideIndices = 3;
    }

    void TerrainWorld::BuildTileMesh(int ringIdx, const TileJob &job, std::vector<Vertex> &verts,
                                     std::vector<uint32_t> &indices, const vec3 &bbMin, const vec3 &bbMax,
                                     uint32_t collideEnd, TileMesh &out) const
    {
        static_assert(kTileLods == Mesh::kMaxLods, "TileMesh lod array must match Mesh::kMaxLods");
        if (verts.empty() || indices.size() < 3)
        {
            out.empty = true;
            return;
        }
        if (verts.size() > job.vertexBudget || indices.size() > job.indexBudget)
        {
            out.growPending = true; // commit flags the tile + warns once; no content built here
            return;
        }

        const uint32_t lod0Count = static_cast<uint32_t>(indices.size());
        out.lod0Count = lod0Count;
        out.lodIndexCount[0] = lod0Count;
        out.lodCount = 1;

        // Ring-0 LOD chain via meshopt (border-locked so tile seams stay matched at every level),
        // appended after lod0 inside the tile's index budget; levels that no longer fit are dropped.
        // Only the per-level COUNTS are recorded; the commit turns them into offsets from indexOffset.
        if (ringIdx == 0 && lod0Count >= 256)
        {
            const float *positions = reinterpret_cast<const float *>(verts.data());
            static constexpr float kRatios[Mesh::kMaxLods] = {1.0f, 0.5f, 0.25f, 0.12f};
            std::vector<uint32_t> simplified(indices.size());
            uint32_t prevCount = lod0Count;
            for (uint32_t lod = 1; lod < Mesh::kMaxLods; ++lod)
            {
                const size_t target = (static_cast<size_t>(lod0Count * kRatios[lod]) / 3) * 3;
                if (target < 12 || indices.size() + target > job.indexBudget)
                    break;
                float err = 0.0f;
                const size_t resCount =
                    meshopt_simplify(simplified.data(), indices.data(), lod0Count, positions, verts.size(),
                                     sizeof(Vertex), target, 0.1f, meshopt_SimplifyLockBorder, &err);
                if (resCount == 0 || resCount >= static_cast<size_t>(prevCount * 0.95f) ||
                    indices.size() + resCount > job.indexBudget)
                    break;
                out.lodIndexCount[lod] = static_cast<uint32_t>(resCount);
                out.lodCount = lod + 1;
                indices.insert(indices.end(), simplified.begin(), simplified.begin() + resCount);
                prevCount = static_cast<uint32_t>(resCount);
            }
        }

        out.collideEnd = collideEnd;
        out.bbMin = bbMin;
        out.bbMax = bbMax;
        out.verts = std::move(verts);
        out.indices = std::move(indices);
    }

    bool TerrainWorld::CommitTileMesh(Tile &tile, const TileMesh &m)
    {
        // Stale: the slot streamed to a different world tile while this mesh was in flight. Discard —
        // the tile stays dirty and re-meshes for its current location. (Never fires when meshing is
        // synchronous; the guard makes the async path safe.)
        if (tile.tx != m.tx || tile.tz != m.tz)
            return false;
        tile.bodyDirty = true; // re-cook the collider (matches the old unconditional MeshTile set)

        if (m.empty)
        {
            WriteEmptyTile(tile);
            return true;
        }
        if (m.growPending)
        {
            if (!tile.growPending)
                PE_WARN("Terrain: tile (%d,%d) overflowed its budget (%zu verts, %zu idx) — growing.",
                        tile.tx, tile.tz, m.verts.size(), m.indices.size());
            tile.growPending = true;
            return false; // keep the old content until the grow re-places the ranges
        }

        Mesh &mesh = m_scene->GetMesh(tile.meshIndex);
        mesh.lodCount = m.lodCount;
        uint32_t off = tile.indexOffset;
        for (uint32_t lod = 0; lod < m.lodCount; ++lod)
        {
            mesh.lodIndexOffset[lod] = off;
            mesh.lodIndexCount[lod] = m.lodIndexCount[lod];
            off += m.lodIndexCount[lod];
        }

        std::vector<Vertex> &vertexStore = m_scene->GetVertexStore();
        std::vector<PositionUvVertex> &positionUvStore = m_scene->GetPositionUvStore();
        std::vector<AabbVertex> &aabbStore = m_scene->GetAabbVertexStore();
        std::vector<uint32_t> &indexStore = m_scene->GetIndexStore();
        for (size_t i = 0; i < m.verts.size(); ++i)
        {
            const Vertex &v = m.verts[i];
            vertexStore[tile.vertexOffset + i] = v;
            PositionUvVertex pv{};
            pv.position[0] = v.position[0];
            pv.position[1] = v.position[1];
            pv.position[2] = v.position[2];
            pv.uv[0] = v.uv[0];
            pv.uv[1] = v.uv[1];
            positionUvStore[tile.vertexOffset + i] = pv;
        }
        std::copy(m.indices.begin(), m.indices.end(), indexStore.begin() + tile.indexOffset);
        for (int c = 0; c < 8; ++c)
        {
            AabbVertex av{};
            av.position[0] = (c & 1) ? m.bbMax.x : m.bbMin.x;
            av.position[1] = (c & 2) ? m.bbMax.y : m.bbMin.y;
            av.position[2] = (c & 4) ? m.bbMax.z : m.bbMin.z;
            aabbStore[tile.aabbVertexOffset + c] = av;
        }

        mesh.indexCount = m.lod0Count;
        mesh.boundingBox = {m.bbMin, m.bbMax};
        tile.liveVerts = static_cast<uint32_t>(m.verts.size());
        tile.liveIndices = static_cast<uint32_t>(m.indices.size());
        tile.collideIndices = std::min(m.collideEnd, m.lod0Count);
        return true;
    }

    void TerrainWorld::Update()
    {
        if (!m_scene || m_tiles.empty())
            return;
        RetireSubmittedCommands(false);

        if (!m_pendingSculpts.empty())
        {
            DrainMeshWorker(); // m_ops is about to change — no in-flight mesh may read it mid-change
            std::vector<SculptOp> ops;
            ops.swap(m_pendingSculpts);
            for (const SculptOp &op : ops)
            {
                m_ops.push_back(op);
                MarkSculptDirty(op);
            }
        }
        StreamWindows();
        GrowOverflowedTiles();
        ProcessDirtyTiles();
        UpdateColliderRing();
    }

    // Worker loop: pull a batch of jobs, mesh them concurrently into local TileMesh results (off the
    // main thread), hand them back. Its whole read path is stable while un-drained (see DrainMeshWorker).
    void TerrainWorld::MeshWorkerLoop()
    {
        for (;;)
        {
            std::vector<TileJob> batch;
            {
                std::unique_lock<std::mutex> lk(m_meshMutex);
                m_meshCv.wait(lk, [this]
                              { return m_meshStop || !m_meshInput.empty(); });
                if (m_meshStop && m_meshInput.empty())
                    return;
                batch.swap(m_meshInput);
                m_meshProcessing = static_cast<int>(batch.size());
            }
            std::vector<TileMesh> results(batch.size());
            auto meshTile = [this](const TileJob &j)
            { return MeshTile(j); };
#if defined(__cpp_lib_parallel_algorithm)
            std::transform(std::execution::par, batch.begin(), batch.end(), results.begin(), meshTile);
#else
            std::transform(batch.begin(), batch.end(), results.begin(), meshTile);
#endif
            {
                std::lock_guard<std::mutex> lk(m_meshMutex);
                for (TileMesh &r : results)
                    m_meshOutput.push_back(std::move(r));
                m_meshProcessing = 0;
            }
            m_meshCv.notify_all(); // wake a DrainMeshWorker waiter
        }
    }

    void TerrainWorld::StartMeshWorker()
    {
        if (m_meshThread.joinable())
            return;
        m_meshStop = false;
        m_meshThread = std::thread(&TerrainWorld::MeshWorkerLoop, this);
    }

    void TerrainWorld::StopMeshWorker()
    {
        if (!m_meshThread.joinable())
            return;
        {
            std::lock_guard<std::mutex> lk(m_meshMutex);
            m_meshStop = true;
            m_meshInput.clear(); // start no new batch; the in-flight one finishes before join returns
        }
        m_meshCv.notify_all();
        m_meshThread.join();
        m_meshStop = false;
        m_meshOutput.clear(); // uncommitted results dropped (world is being rebuilt / torn down)
        m_meshProcessing = 0;
    }

    void TerrainWorld::DrainMeshWorker()
    {
        if (!m_meshThread.joinable())
            return;
        std::unique_lock<std::mutex> lk(m_meshMutex);
        m_meshCv.wait(lk, [this]
                      { return m_meshInput.empty() && m_meshProcessing == 0; });
        // Un-committed results can be stale in ways the per-tile tx/tz guard can't see once the caller
        // mutates ops / the scatter map / tile offsets. Drop them and re-dirty their tiles so they
        // re-mesh against the new state (they were dirty when dispatched — over-meshing is only waste).
        for (const TileMesh &r : m_meshOutput)
            if (r.tileIdx >= 0 && r.tileIdx < static_cast<int>(m_tiles.size()))
            {
                m_tiles[r.tileIdx].meshing = false;
                m_tiles[r.tileIdx].dirty = true;
            }
        m_meshOutput.clear();
    }

    void TerrainWorld::ProcessDirtyTiles()
    {
        // 1. Commit meshes the worker finished since last frame (main thread: store writes + upload).
        std::vector<TileMesh> done;
        {
            std::lock_guard<std::mutex> lk(m_meshMutex);
            done.swap(m_meshOutput);
        }
        std::vector<int> uploaded;
        for (TileMesh &mesh : done)
        {
            if (mesh.tileIdx < 0 || mesh.tileIdx >= static_cast<int>(m_tiles.size()))
                continue;
            Tile &tile = m_tiles[mesh.tileIdx];
            tile.meshing = false;
            if (CommitTileMesh(tile, mesh))
            {
                uploaded.push_back(mesh.tileIdx);
                tile.dirty = false;
            }
            else if (tile.growPending)
                tile.dirty = true; // re-mesh after the grow re-places the ranges
            // else stale: StreamWindows already re-set tile.dirty for the new location.
        }

        // 2. Dispatch newly-dirty tiles (not already in flight) to the worker, nearest-first. Streaming
        // may have changed tx/tz since a job was queued; the job snapshot below captures the current
        // location so the worker never races StreamWindows.
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
                if (!tile.dirty || tile.growPending || tile.meshing)
                    continue;
                const float cxw = (tile.tx + 0.5f) * tw, czw = (tile.tz + 0.5f) * tw;
                const float dx = cxw - m_anchor.x, dz = czw - m_anchor.z;
                queue.push_back({std::sqrt(dx * dx + dz * dz) + static_cast<float>(r) * 1e6f,
                                 static_cast<int>(r), t});
            }
        }
        if (!queue.empty())
        {
            std::sort(queue.begin(), queue.end(), [](const Item &a, const Item &b)
                      { return a.key < b.key; });
            const int budget = std::min<int>(kMeshBudgetPerUpdate, static_cast<int>(queue.size()));
            std::vector<TileJob> jobs;
            jobs.reserve(budget);
            for (int i = 0; i < budget; ++i)
            {
                Tile &tile = m_tiles[queue[i].tileIdx];
                jobs.push_back({queue[i].tileIdx, queue[i].ringIdx, tile.tx, tile.tz, tile.interiorHole,
                                tile.vertexBudget, tile.indexBudget});
                tile.meshing = true; // in flight until its result commits (don't re-dispatch)
            }
            {
                std::lock_guard<std::mutex> lk(m_meshMutex);
                for (TileJob &j : jobs)
                    m_meshInput.push_back(j);
            }
            m_meshCv.notify_all();
        }

        // 3. Upload the tiles committed above (staged, submitted-not-waited on the main queue).
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
        DrainMeshWorker(); // tile store offsets are about to be re-placed — no in-flight mesh may commit

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
            if (r == 0) // budgets stay uniform per ring, so any tile carries the grown value
                m_grownRing0Budget = m_tiles[ring.firstTile].vertexBudget;
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
        DrainMeshWorker(); // mutates m_ops directly — quiesce the mesher first
        SculptOp op;
        op.center = center;
        op.radius = radius;
        op.dig = amount < 0.0f;
        m_ops.push_back(op);
        MarkSculptDirty(op);
    }

    void TerrainWorld::QueueSculpt(const vec3 &center, float radius, float amount)
    {
        if (radius <= 0.0f)
            return;
        SculptOp op;
        op.center = center;
        op.radius = radius;
        op.dig = amount < 0.0f;
        m_pendingSculpts.push_back(op);
    }

    void TerrainWorld::QueueLevel(const vec3 &center, float radius, float targetY, float weight)
    {
        if (radius <= 0.0f || weight <= 0.0f)
            return;
        SculptOp op;
        op.level = true;
        op.center = center;
        op.radius = radius;
        op.targetY = targetY;
        op.weight = std::min(weight, 1.0f);
        m_pendingSculpts.push_back(op);
    }

    void TerrainWorld::SetOps(const std::vector<float> &ops)
    {
        DrainMeshWorker(); // replaces m_ops wholesale — no in-flight mesh may read it mid-swap
        m_ops.clear();
        m_ops.reserve(ops.size() / 7);
        for (size_t i = 0; i + 7 <= ops.size(); i += 7)
        {
            SculptOp op;
            op.level = ops[i] != 0.0f;
            op.center = vec3(ops[i + 1], ops[i + 2], ops[i + 3]);
            op.radius = ops[i + 4];
            if (op.level)
            {
                op.targetY = ops[i + 5];
                op.weight = std::clamp(ops[i + 6], 0.0f, 1.0f);
            }
            else
            {
                op.dig = ops[i + 5] != 0.0f;
            }
            if (op.radius > 0.0f)
                m_ops.push_back(op);
        }
    }

    void TerrainWorld::GetOps(std::vector<float> &out) const
    {
        out.clear();
        out.reserve(m_ops.size() * 7);
        for (const SculptOp &op : m_ops)
        {
            out.push_back(op.level ? 1.0f : 0.0f);
            out.push_back(op.center.x);
            out.push_back(op.center.y);
            out.push_back(op.center.z);
            out.push_back(op.radius);
            out.push_back(op.level ? op.targetY : (op.dig ? 1.0f : 0.0f));
            out.push_back(op.level ? op.weight : 0.0f);
        }
    }

    bool TerrainWorld::UpdateScatterMap(const uint8_t *px, int w, int h, const vec2 &worldMin,
                                        const vec2 &worldMax)
    {
        if (!px || w <= 0 || h <= 0 || m_templates.empty() || m_rings.empty())
            return false;  // no templates configured = a rebuild (structural change) is the only route in
        DrainMeshWorker(); // the mesher reads m_scatterMap in AppendScatter — quiesce before replacing it
        if (!m_scatterMap)
            m_scatterMap = std::make_unique<voxel::MapImage>();
        m_scatterMap->w = w;
        m_scatterMap->h = h;
        m_scatterMap->isFloat = false;
        m_scatterMap->px.assign(px, px + static_cast<size_t>(w) * h);
        m_scatterMap->pxf.clear();
        m_scatterCenterX = m_cfg.boundsCenterCx * kBlocksPerColumn + kBlocksPerColumn / 2.0f;
        m_scatterCenterZ = m_cfg.boundsCenterCz * kBlocksPerColumn + kBlocksPerColumn / 2.0f;
        m_scatterExtentX = w * m_cfg.metersPerPixel;
        m_scatterExtentZ = h * m_cfg.metersPerPixel;

        // Re-mesh only the ring-0 tiles the painted world rect touches (+ prop footprint margin) —
        // the streamed-update path uploads them over the next frames, no rebuild.
        const Ring &ring = m_rings[0];
        const float tw = TileWorldSize(ring);
        const float margin = 3.0f;
        const int tx0 = TileFloor(worldMin.x - margin, tw), tx1 = TileFloor(worldMax.x + margin, tw);
        const int tz0 = TileFloor(worldMin.y - margin, tw), tz1 = TileFloor(worldMax.y + margin, tw);
        for (int tz = tz0; tz <= tz1; ++tz)
            for (int tx = tx0; tx <= tx1; ++tx)
            {
                const int sx = WrapMod(tx, ring.tilesX);
                const int sz = WrapMod(tz, ring.tilesZ);
                Tile &tile = m_tiles[ring.firstTile + sz * ring.tilesX + sx];
                if (tile.tx == tx && tile.tz == tz)
                {
                    tile.dirty = true;
                    tile.bodyDirty = true;
                }
            }
        return true;
    }

    bool TerrainWorld::UpdateSplatMap(const uint8_t *rgba, int w, int h)
    {
        // Only meaningful once the textured terrain pipeline is live: BuildTerrainTextures owns the 4
        // layers + the splat at m_terrainTextures[4], and the mesh already carries the splat uv. No
        // texture pipeline (layers failed to load) = a rebuild can't help either; caller falls back.
        if (!rgba || w <= 0 || h <= 0 || !m_scene || !m_material || !m_material->terrain ||
            m_terrainTextures.size() < 5)
            return false;
        Queue *queue = RHII.GetMainQueue();
        if (!queue)
            return false;

        Image *cur = m_terrainTextures[4].get();
        // The default splat is a 1x1 zero texel; first paint (or a resize) needs a full-size texture.
        const bool recreate =
            !cur || static_cast<int>(cur->GetWidth()) != w || static_cast<int>(cur->GetHeight()) != h;

        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();
        Image *target = cur;
        if (recreate)
        {
            target = Image::LoadRawFromMemory(cmd, const_cast<uint8_t *>(rgba), static_cast<uint32_t>(w),
                                              static_cast<uint32_t>(h), PE_FORMAT_R8G8B8A8_UNORM, "TerrainSplatPainted");
        }
        else
        {
            // Re-upload into the existing texture so the SRV (and GbufferPass binding) stays put —
            // ponytail: full-texture copy, maps are tiny (<=2048^2); switch to a dirty-rect
            // sub-region copy if paint on huge maps ever hitches.
            cmd->CopyDataToImageStaged(target, const_cast<uint8_t *>(rgba), static_cast<size_t>(w) * h * 4);
            ImageBarrierInfo toRead{};
            toRead.image = target;
            toRead.layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toRead.stageFlags = PE_STAGE_FRAGMENT_SHADER;
            toRead.accessMask = PE_ACCESS_SHADER_SAMPLED_READ;
            cmd->ImageBarrier(toRead);
        }
        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();

        if (recreate)
        {
            if (!target)
                return false;
            m_terrainTextures[4] = std::shared_ptr<Image>(
                target, [](Image *p)
                { RHII.AddToDeletionQueue([p]() mutable
                                          { Image *img = p; Image::Destroy(img); }); });
            m_scene->SetTerrainSplatView(m_terrainTextures[4]->GetSRV()); // GbufferPass rebinds on the SRV change
        }
        return true;
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
                tile.bodyId = physics->AddStaticMeshBody(vertexStore.data() + tile.vertexOffset, tile.liveVerts,
                                                         indexStore.data() + tile.indexOffset,
                                                         std::max(3u, tile.collideIndices), // lod0 prefix:
                                                         // terrain + colliding props, no-collide scatter
                                                         // (grass) excluded
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

    void TerrainWorld::SetTextureScale(float metersPerTile)
    {
        m_cfg.textureScaleM = std::max(0.05f, metersPerTile);
        if (m_scene)
            m_scene->SetTerrainTexScale(m_cfg.textureScaleM); // shader reads it via the terrain push constant
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

    bool TerrainWorld::BuildTerrainTextures()
    {
        m_terrainTextures.clear();
        if (!m_scene)
            return false;
        Queue *queue = RHII.GetMainQueue();
        if (!queue)
        {
            PE_WARN("[Terrain] Cannot build terrain textures without a main queue.");
            return false;
        }

        // RuntimeAssets holds the default layer textures; a user path resolves against project Assets.
        auto resolve = [](const std::string &p) -> std::string
        {
            if (p.empty())
                return p;
            const std::string rt = Path::RuntimeAssets + p;
            if (AssetFileExists(rt))
                return rt;
            return voxel::ColumnChunkStore::ResolveRoot(p).string();
        };

        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();

        // UNORM, not SRGB: Image::LoadRGBA adds a STORAGE flag for GPU mip generation, and SRGB +
        // STORAGE is illegal on DX12. The shader converts each sample sRGB->linear instead.
        Image *layers[4] = {nullptr, nullptr, nullptr, nullptr};
        for (int i = 0; i < 4; ++i)
            layers[i] = Image::LoadRGBA8(cmd, resolve(m_cfg.layerPaths[i]));

        // Splat map: RGBA layer weights (UNORM, not colour). Empty/failed = a 1x1 zero texel so the
        // shader's auto height/slope selection runs everywhere.
        Image *splat = nullptr;
        if (!m_cfg.splatPath.empty())
            splat = Image::LoadRGBA8(cmd, resolve(m_cfg.splatPath));
        if (!splat)
        {
            uint8_t zero[4] = {0, 0, 0, 0};
            splat = Image::LoadRawFromMemory(cmd, zero, 1, 1, PE_FORMAT_R8G8B8A8_UNORM, "TerrainSplatDefault");
        }

        // Per-layer material maps (RGB = tangent normal, A = roughness). Absent = a 1x1 flat-normal /
        // full-roughness texel (128,128,255,255): the shader's whiteout blend collapses it to the
        // geometric normal, so unauthored layers render exactly as before (and the 1x1 is bandwidth-free).
        Image *mats[4] = {nullptr, nullptr, nullptr, nullptr};
        for (int i = 0; i < 4; ++i)
        {
            if (!m_cfg.materialPaths[i].empty())
                mats[i] = Image::LoadRGBA8(cmd, resolve(m_cfg.materialPaths[i]));
            if (!mats[i])
            {
                uint8_t flat[4] = {128, 128, 255, 255};
                mats[i] = Image::LoadRawFromMemory(cmd, flat, 1, 1, PE_FORMAT_R8G8B8A8_UNORM, "TerrainMatDefault");
            }
        }

        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();

        if (!layers[0] || !layers[1] || !layers[2] || !layers[3] || !splat || !mats[0] || !mats[1] || !mats[2] ||
            !mats[3])
        {
            PE_WARN("[Terrain] Failed to load a terrain layer/splat texture — using vertex-colour bands.");
            for (Image *img : layers)
                if (img)
                    Image::Destroy(img);
            if (splat)
                Image::Destroy(splat);
            for (Image *img : mats)
                if (img)
                    Image::Destroy(img);
            return false;
        }

        auto own = [&](Image *img)
        {
            m_terrainTextures.emplace_back(img, [](Image *p)
                                           { RHII.AddToDeletionQueue([p]() mutable
                                                                     { Image *img = p; Image::Destroy(img); }); });
            return img;
        };
        for (int i = 0; i < 4; ++i)
            m_scene->SetTerrainLayerView(i, own(layers[i])->GetSRV());
        m_scene->SetTerrainSplatView(own(splat)->GetSRV()); // splat stays m_terrainTextures[4] (UpdateSplatMap)
        for (int i = 0; i < 4; ++i)
            m_scene->SetTerrainMaterialView(i, own(mats[i])->GetSRV());
        return true;
    }

    void TerrainWorld::Destroy()
    {
        StopMeshWorker(); // join the mesher before any state it reads is freed
        RetireSubmittedCommands(true);
        for (Tile &tile : m_tiles)
        {
            RemoveTileBody(tile);
            if (m_scene && tile.meshIndex >= 0 && m_scene->IsValidMeshIndex(tile.meshIndex))
                m_scene->GetMesh(tile.meshIndex).material = nullptr;
        }
        if (m_scene && m_hostNode && m_scene->IsNodeAlive(m_hostNode))
            m_scene->DeleteNode(m_hostNode);

        if (m_scene)
        {
            m_scene->SetTerrainSplatView(nullptr);
            for (int i = 0; i < 4; ++i)
            {
                m_scene->SetTerrainLayerView(i, nullptr);
                m_scene->SetTerrainMaterialView(i, nullptr);
            }
        }
        m_terrainTextures.clear();
        m_material.reset();
        m_hostNode = nullptr;
        m_tiles.clear();
        m_rings.clear();
        m_ops.clear();
        m_pendingSculpts.clear();
        m_cavesMap.reset();
        m_cavesExtentX = m_cavesExtentZ = 0.0f;
        m_scatterMap.reset();
        m_templates.clear();
        m_scatterBudgetVerts = 0;
        m_scatterExtentX = m_scatterExtentZ = 0.0f;
        m_anchorSet = false;
        m_scene = nullptr;
    }
} // namespace pe::terrain
