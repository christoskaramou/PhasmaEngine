#pragma once

#include "API/Vertex.h"
#include "Base/Math.h"
#include "Terrain/ScatterTemplates.h"

namespace pe
{
    class CommandBuffer;
    class Image;
    class Material;
    class Scene;
    struct NodeId;
} // namespace pe

namespace pe::voxel
{
    class ITerrainGenerator;
    struct MapImage;
} // namespace pe::voxel

namespace pe::terrain
{
    // Streamed isosurface terrain. The world is a set of fixed TILES (budgeted ranges in the Scene's
    // shared geometry buffer, created once), re-meshed IN PLACE via Scene::UpdateStreamedMesh — no
    // geometry rebuild per sculpt or stream step. Ring 0 meshes a density field with Surface Nets
    // (voxel::SurfaceNetsTile), so 3D features work: worldgen overhangs (TerrainConfig::overhangs,
    // NoiseGen 3D FBM) and CSG sphere sculpts that can dig sideways into a cliff. With streaming on,
    // the windows follow the anchor toroidally (a tile slot is re-meshed for its new world location
    // when the window wraps) and two coarse heightfield rings extend the view distance with skirted
    // grid tiles. Colliders are per-tile static Jolt mesh bodies in a ring around the anchor,
    // re-cooked when a tile re-meshes. Worldgen reuses the shared ITerrainGenerator seam
    // (voxel::NoiseGen / voxel::MapGen).
    struct TerrainConfig
    {
        // Extent in metres: bounded worlds build a sizeXMeters * sizeZMeters patch centred on the
        // bounds column; streaming worlds use it as the FINE window size following the anchor. One
        // ring-0 grid cell spans metersPerPixel metres. 0 on an axis with a heightmap fills its extent.
        int sizeXMeters = 256;
        int sizeZMeters = 256;
        int boundsCenterCx = 0;
        int boundsCenterCz = 0;
        // Height mapping (metres): heightmap gray 0..1 -> groundHeight + lerp(heightMin, heightMax, v);
        // mid-gray sits at groundHeight and the surface may dip below y=0.
        float groundHeight = 0.0f;
        float heightMin = -32.0f;
        float heightMax = 32.0f;
        float seaLevelM = 0.0f; // surface below this world Y gets an underwater colour tint
        // Worldgen source: a grayscale heightmap when set (see MapGen.h), else procedural noise.
        std::string heightmapPath;
        // Painted caves: grayscale map (Map Painter "Caves" layer) sharing the heightmap's extent and
        // orientation; pixel value = cave openness carved below the local surface (0 = solid).
        std::string cavesPath;
        // Painted mesh scatter: map (Map Painter "Scatter" layer, same extent/orientation rules as the
        // caves map) whose pixel value is a 1-based index into scatterMeshes (0 = none). Instances are
        // baked into ring-0 tile geometry at mesh time — they stream, LOD, shadow and collide with the
        // tile, with no per-instance nodes or draws.
        std::string scatterPath;
        std::vector<std::string> scatterMeshes; // builtin "tree"/"rock"/"grass" or a model asset path
        // Triplanar texture splatting. 4 layer albedo textures (0 grass, 1 rock, 2 sand, 3 snow) are
        // sampled triplanar and blended by an optional splat map (RGBA = layer weights). Empty splat =
        // auto height/slope selection everywhere.
        std::string splatPath;
        std::array<std::string, 4> layerPaths = {"Textures/Voxel/grass.png", "Textures/Voxel/rock.png",
                                                 "Textures/Voxel/sand.png", "Textures/Voxel/snow.png"};
        // Per-layer material maps (RGB = tangent-space normal, A = roughness), same layer order. Ship
        // defaults give lit terrain micro-relief; clear a path for a flat 1x1 (geometric normal, as before).
        std::array<std::string, 4> materialPaths = {"Textures/Voxel/grass_n.png", "Textures/Voxel/rock_n.png",
                                                    "Textures/Voxel/sand_n.png", "Textures/Voxel/snow_n.png"};
        float textureScaleM = 3.0f; // metres per triplanar texture tile (live, not structural)
        float noiseFeatureScale = 96.0f;
        int noiseSeed = 0;
        float metersPerPixel = 1.0f; // world metres each ring-0 grid cell spans in X/Z (and Y)
        // Static per-tile Jolt triangle-mesh colliders so rigidbodies collide with the terrain.
        // Friction/restitution are live-applied (no re-cook).
        bool physics = false;
        float physicsFriction = 0.5f;
        float physicsRestitution = 0.3f;
        // --- Phase C ---
        bool streaming = false; // windows follow the anchor; adds two coarse view rings beyond ring 0
        // 0..1 worldgen 3D relief: cliffs undercut and hollows open (NoiseGen only — heightmaps get 3D
        // shape from sculpting). Widens each tile's vertical sampling band, so it costs meshing time.
        float overhangs = 0.0f;
        // Colliders only within this range of the anchor (0 = every live ring-0 tile, the bounded
        // default). Streamed worlds set ~96 so cook cost tracks the player, not the window.
        float collisionRadiusM = 0.0f;
    };

    class TerrainWorld
    {
    public:
        TerrainWorld();
        ~TerrainWorld();

        void Create(Scene *scene, const TerrainConfig &cfg);
        void Destroy();
        // Per-frame: drain queued sculpts, follow the anchor (streaming), re-mesh dirty tiles within a
        // budget and stage their uploads, maintain the collider ring.
        void Update();

        // Where the windows and the collider ring centre; feed the active camera per frame.
        void SetAnchor(const vec3 &anchor);

        // CSG sphere brush at a 3D point: amount < 0 digs (subtract — sideways digs undercut into
        // overhangs), amount >= 0 builds (union). Magnitude is ignored — aim deeper to dig deeper.
        void Sculpt(const vec3 &center, float radius, float amount);
        // Defer a sculpt to the next Update() — safe from script/editor paths mid-frame.
        void QueueSculpt(const vec3 &center, float radius, float amount);
        // Level brush, deferred like QueueSculpt: pulls the surface toward y = targetY inside the
        // sphere (quartic falloff to the rim). weight 1 = hard flatten per stamp; partial weights
        // toward a local surface average make a smooth brush. Ops persist like sculpt spheres.
        void QueueLevel(const vec3 &center, float radius, float targetY, float weight);
        // Persistent ops in the tag/serializer format: flat 7-float records
        // [type, cx, cy, cz, radius, a, b] — type 0 = sphere (a = 1 digs), type 1 = level
        // (a = targetY, b = weight). Set BEFORE Create (tiles apply ops as they mesh); Get syncs the
        // authored tag after sculpting.
        void SetOps(const std::vector<float> &ops);
        void GetOps(std::vector<float> &out) const;
        size_t OpCount() const { return m_ops.size(); }
        // Live scatter painting: replace the map's pixels (same size or resized) and re-mesh only the
        // ring-0 tiles overlapping the world-space rect — no rebuild. False when the world has no
        // scatter templates yet (a structural rebuild must pick the config up first).
        bool UpdateScatterMap(const uint8_t *px, int w, int h, const vec2 &worldMin, const vec2 &worldMax);
        // Live splat painting: replace the GPU splat texture (RGBA layer weights) in place — the
        // terrain re-textures next frame with no rebuild or re-mesh (the mesh already carries the splat
        // uv). False when the textured terrain pipeline is not active (BuildTerrainTextures never ran),
        // so the caller falls back to save + rebuild.
        bool UpdateSplatMap(const uint8_t *rgba, int w, int h);

        // Worldgen surface world-Y at (x, z); ignores sculpts and 3D overhangs (use Raycast for the
        // true surface). Very low when there is no generator.
        float SampleHeight(float x, float z) const;
        // March the density field (worldgen + sculpts, overhang-aware); fills hit point + normal.
        bool Raycast(const vec3 &o, const vec3 &d, float maxDist, vec3 &hitPoint, vec3 &hitNormal) const;
        // Collision solidity of block-cell (x,y,z): density at the cell centre. The predicate
        // voxel::MoveAabb uses so non-physics movers stop on the terrain.
        bool IsSolidCell(int x, int y, int z) const;

        void SetPhysicsEnabled(bool enabled);
        void SetTerrainMaterial(float friction, float restitution);
        // Live metres-per-tile change (no rebuild): updates config + the Scene value the shader reads.
        void SetTextureScale(float metersPerTile);
        // Live: the collider ring re-fits over the next Updates (no rebuild).
        void SetCollisionRadius(float radiusM) { m_cfg.collisionRadiusM = radiusM; }
        // Override the generator (call before Create; nullptr reverts to the default). Must be
        // thread-safe like the voxel one — the terrain samples it from the main thread today.
        void SetTerrainGenerator(std::shared_ptr<voxel::ITerrainGenerator> generator);

        const TerrainConfig &Config() const { return m_cfg; }
        // False once a Scene buffer rebuild (scene load, play-stop restore) wiped the host mesh out of
        // the shared buffer — reconcile recreates instead of leaving an empty terrain.
        bool IsAlive() const;

    private:
        struct SculptOp
        {
            bool level = false; // false = CSG sphere (dig/build), true = pull toward y = targetY
            vec3 center = vec3(0.0f);
            float radius = 0.0f;
            bool dig = true;      // sphere only
            float targetY = 0.0f; // level only
            float weight = 1.0f;  // level only: blend per application
        };
        struct Tile
        {
            // Fixed reserved ranges in the scene stores (set at Create; only a grow re-places them).
            uint32_t vertexOffset = 0;
            uint32_t vertexBudget = 0;
            uint32_t indexOffset = 0;
            uint32_t indexBudget = 0;
            size_t aabbVertexOffset = 0;
            int meshIndex = -1;
            uint32_t refSlot = 0; // position in the host node's meshRefs
            // Live content.
            int tx = INT_MIN, tz = INT_MIN; // world tile coord currently meshed
            bool interiorHole = false;      // coarse tile meshed empty because a finer ring covers it
            uint32_t liveVerts = 0;
            uint32_t liveIndices = 0;    // lod0 + simplified levels, contiguous from indexOffset
            uint32_t collideIndices = 0; // lod0 prefix the collider cooks (excludes no-collide scatter)
            bool dirty = false;
            bool growPending = false; // content overflowed the budget — grow re-places, then re-mesh
            bool meshing = false;     // an async mesh for this slot is in flight (don't re-dispatch)
            // Physics (ring 0 only).
            uint32_t bodyId = 0xFFFFFFFF;
            bool bodyDirty = false;
        };
        struct Ring
        {
            float cellSize = 1.0f; // metres per cell; ring r = metersPerPixel * 4^r
            int tilesX = 0;        // window is tilesX * tilesZ tiles, toroidally mapped
            int tilesZ = 0;
            int firstTile = 0;                      // index of this ring's first tile in m_tiles
            ivec2 center = ivec2(INT_MIN, INT_MIN); // window BASE: min world tile coord of the window
        };
        // Immutable per-tile snapshot handed to the (worker-safe) mesher. Captures everything the mesh
        // build reads that a streaming step can change (tx/tz/interiorHole) or that grow can change
        // (budgets) so the worker never races those main-thread writes; everything else the mesher
        // reads (generator, cfg, ops, maps, rings) only changes under a worker drain.
        struct TileJob
        {
            int tileIdx = -1;
            int ringIdx = 0;
            int tx = 0, tz = 0;
            bool interiorHole = false;
            uint32_t vertexBudget = 0;
            uint32_t indexBudget = 0;
        };
        // CPU meshing output — the worker fills this with LOCAL buffers only (no store/Tile writes);
        // the main thread commits it into the tile's store ranges. tx/tz are echoed for a staleness
        // check (discard if the slot streamed to a new world tile while the mesh was in flight).
        static constexpr int kTileLods = 4; // == Mesh::kMaxLods (static_assert in the .cpp)
        struct TileMesh
        {
            int tileIdx = -1;
            int tx = 0, tz = 0;
            bool valid = false;       // produced by the mesher this batch
            bool empty = false;       // degenerate (interior hole / outside footprint / no surface)
            bool growPending = false; // over budget — commit flags the tile, writes nothing
            std::vector<Vertex> verts;
            std::vector<uint32_t> indices; // lod0 then simplified levels, contiguous
            uint32_t lodIndexCount[kTileLods] = {};
            uint32_t lodCount = 0;
            uint32_t lod0Count = 0;
            uint32_t collideEnd = 0;
            vec3 bbMin = vec3(0.0f), bbMax = vec3(0.0f);
        };

        // Density of the shared worldgen + sculpt CSG ops. The h-cached variant is the mesher fast
        // path (one SurfaceHeight per corner column instead of per corner sample).
        float DensityAt(float x, float y, float z) const;
        float DensityLocal(float x, float y, float z, float surfaceHeight) const;
        vec3 TerrainColor(float y, float normalY) const;

        void BuildGenerator();
        void LoadCavesMap(); // cfg.cavesPath -> m_cavesMap (+ world extent), empty = none
        // cfg.scatterMeshes -> m_templates and cfg.scatterPath -> m_scatterMap, plus the per-tile
        // vertex demand histogram that feeds the ring-0 budget.
        void LoadScatter();
        void BuildRings();    // ring layout from cfg (no allocation)
        void AllocateTiles(); // reserve store ranges + register scene meshes on the host node
        // (Re)mesh a tile into a TileMesh result. const + LOCAL buffers only (no Scene store or Tile
        // writes) so it runs on the meshing worker for any number of tiles concurrently; the main
        // thread commits the result. All member reads are stable for the job's lifetime (see TileJob).
        TileMesh MeshTile(const TileJob &job) const;
        void MeshTileSurfaceNets(const Ring &ring, const TileJob &job, TileMesh &out) const;
        void MeshTileGrid(const Ring &ring, const TileJob &job, TileMesh &out) const; // coarse tile + skirt
        // Stamp scatter instances whose anchor pixel falls inside the tile rect onto the true surface
        // (deterministic per pixel). Colliding kinds first; collideEnd = index count before the
        // no-collide suffix.
        void AppendScatter(const Ring &ring, const TileJob &job, std::vector<Vertex> &verts,
                           std::vector<uint32_t> &indices, vec3 &bbMin, vec3 &bbMax, uint32_t &collideEnd) const;
        // Build the LOD chain (ring 0) and finalize a TileMesh from local verts/indices, capped by the
        // index budget; sets growPending (no LOD, caller grows) when the base mesh overflows. No writes.
        void BuildTileMesh(int ringIdx, const TileJob &job, std::vector<Vertex> &verts,
                           std::vector<uint32_t> &indices, const vec3 &bbMin, const vec3 &bbMax,
                           uint32_t collideEnd, TileMesh &out) const;
        // Commit a finished TileMesh into the tile's reserved store ranges + Mesh live fields (main
        // thread). Discards a stale result (slot streamed away) and flags grow on overflow. Returns
        // true when the tile needs a GPU upload.
        bool CommitTileMesh(Tile &tile, const TileMesh &m);
        void WriteEmptyTile(Tile &tile); // main-thread degenerate placeholder (Create + empty commits)
        void StreamWindows();            // desired world tile per slot; mark moved slots dirty
        void ProcessDirtyTiles();        // commit finished async meshes + dispatch new dirty tiles + upload
        // Background meshing worker: dispatched tile jobs are meshed off the main thread; the main
        // thread commits + uploads their results. Any mutation of data the mesher reads (ops, scatter
        // map, generator, tile offsets) must DrainMeshWorker() first — streaming's tx/tz is captured in
        // the job snapshot so it alone needs no drain.
        void StartMeshWorker();
        void StopMeshWorker();  // signal stop + join; drop uncommitted results (rebuild/teardown)
        void DrainMeshWorker(); // block until in-flight meshing finishes; drop + re-dirty pending
        void MeshWorkerLoop();
        void UpdateColliderRing();  // budgeted per-tile Jolt body add/remove/re-cook
        void GrowOverflowedTiles(); // any overflow doubles its whole ring's budgets (one flush, rare)
        void RemoveTileBody(Tile &tile);
        // Load the 4 layer albedo textures + the splat map and bind them to the Scene's terrain
        // descriptor. Returns false (terrain keeps the standard pipeline) if the layers fail to load.
        bool BuildTerrainTextures();
        void RetireSubmittedCommands(bool all);
        bool TileInteriorHole(int ringIdx, int tx, int tz) const;
        // True when a heightmap is loaded and the tile lies entirely outside its footprint (no mesh).
        bool TileOutsideHeightmapFootprint(const Ring &ring, int tx, int tz) const;
        float TileWorldSize(const Ring &ring) const;
        void MarkSculptDirty(const SculptOp &op);

        Scene *m_scene = nullptr;
        TerrainConfig m_cfg{};
        std::shared_ptr<voxel::ITerrainGenerator> m_generator;
        bool m_generatorOverridden = false;
        // Heightmap footprint in world metres (centred on the bounds column). Tiles fully outside it
        // stay empty so edge-clamped samples do not fill a configured extent larger than the map.
        bool m_hasHeightmapFootprint = false;
        float m_heightmapCenterX = 0.0f, m_heightmapCenterZ = 0.0f;
        float m_heightmapExtentX = 0.0f, m_heightmapExtentZ = 0.0f;
        // Painted caves map: heightmap-style extent (w/h * metersPerPixel, centred on the bounds
        // column, both axes flipped); zero OUTSIDE the map so streamed worlds don't tile it.
        std::unique_ptr<voxel::MapImage> m_cavesMap;
        float m_cavesCenterX = 0.0f, m_cavesCenterZ = 0.0f;
        float m_cavesExtentX = 0.0f, m_cavesExtentZ = 0.0f;
        // Painted scatter map (same extent/orientation rules as the caves map) + the prop templates
        // its pixel values index (1-based). m_scatterBudgetVerts = worst per-tile vertex demand,
        // measured from the map at load, added to the ring-0 tile budget.
        std::unique_ptr<voxel::MapImage> m_scatterMap;
        float m_scatterCenterX = 0.0f, m_scatterCenterZ = 0.0f;
        float m_scatterExtentX = 0.0f, m_scatterExtentZ = 0.0f;
        std::vector<ScatterTemplate> m_templates;
        uint32_t m_scatterBudgetVerts = 0;

        std::vector<Ring> m_rings;
        std::vector<Tile> m_tiles; // all rings, ring-major (Ring::firstTile indexes in here)
        std::vector<SculptOp> m_ops;
        // Background meshing. Dirty tiles are snapshotted into m_meshInput (main -> worker), meshed off
        // the main thread, and returned in m_meshOutput (worker -> main) for commit + upload. All three
        // + m_meshProcessing/m_meshStop are guarded by m_meshMutex. The mesher's other reads (generator,
        // cfg, ops, maps, rings, tile offsets) are only mutated under DrainMeshWorker(), so they need no
        // lock. m_tiles is main-thread-only (the worker sees tiles only through the job snapshot).
        std::thread m_meshThread;
        std::mutex m_meshMutex;
        std::condition_variable m_meshCv;
        std::vector<TileJob> m_meshInput;
        std::vector<TileMesh> m_meshOutput;
        int m_meshProcessing = 0; // jobs pulled from input, not yet in output
        bool m_meshStop = false;
        std::vector<SculptOp> m_pendingSculpts; // queued brush strokes, drained on Update()
        vec3 m_anchor = vec3(0.0f);
        bool m_anchorSet = false;

        std::unique_ptr<Material> m_material;
        NodeId *m_hostNode = nullptr;
        std::vector<CommandBuffer *> m_submittedCmds;
        // Triplanar layer albedos [0..3] + the splat map [4], kept alive while bound to the Scene's
        // terrain descriptor. Empty until BuildTerrainTextures succeeds.
        std::vector<std::shared_ptr<Image>> m_terrainTextures;

        // Grow memory: once ring 0 outgrows its estimate, recreates of the SAME worldgen (hash of
        // the mesh-affecting config) start from the grown budget instead of re-discovering the
        // overflow through repeated grow rebuilds (every recreate used to reset budgets — typing in
        // the inspector replayed the whole grow chain each time). Deliberately survives Destroy.
        uint64_t m_budgetHash = 0;
        uint32_t m_grownRing0Budget = 0;
    };
} // namespace pe::terrain
