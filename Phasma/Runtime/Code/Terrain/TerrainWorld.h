#pragma once

#include "API/Vertex.h"
#include "Base/Math.h"

namespace pe
{
    class CommandBuffer;
    class Material;
    class Scene;
    struct NodeId;
} // namespace pe

namespace pe::voxel
{
    class ITerrainGenerator;
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
        // Persistent CSG ops in the tag/serializer format: xyz = centre, |w| = radius, w < 0 digs.
        // Set BEFORE Create (tiles apply ops as they mesh); Get syncs the authored tag after sculpting.
        void SetSculptOps(const std::vector<vec4> &ops);
        void GetSculptOps(std::vector<vec4> &out) const;
        size_t SculptOpCount() const { return m_ops.size(); }

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
            vec3 center = vec3(0.0f);
            float radius = 0.0f;
            bool dig = true;
        };
        struct QueuedSculpt
        {
            vec3 center = vec3(0.0f);
            float radius = 0.0f;
            float amount = 0.0f;
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
            uint32_t liveIndices = 0; // lod0 + simplified levels, contiguous from indexOffset
            bool dirty = false;
            bool growPending = false; // content overflowed the budget — grow re-places, then re-mesh
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

        // Density of the shared worldgen + sculpt CSG ops. The h-cached variant is the mesher fast
        // path (one SurfaceHeight per corner column instead of per corner sample).
        float DensityAt(float x, float y, float z) const;
        float DensityLocal(float x, float y, float z, float surfaceHeight) const;
        vec3 TerrainColor(float y, float normalY) const;

        void BuildGenerator();
        void BuildRings();                      // ring layout from cfg (no allocation)
        void AllocateTiles();                   // reserve store ranges + register scene meshes on the host node
        void MeshTile(int ringIdx, Tile &tile); // (re)mesh tile content into its store ranges
        void MeshTileSurfaceNets(const Ring &ring, Tile &tile);
        void MeshTileGrid(const Ring &ring, Tile &tile); // coarse heightfield tile + skirt
        // Write verts/indices into the tile's reserved store ranges (+ meshopt LOD chain on ring 0),
        // update the Mesh's live fields; false = budget overflow (tile flagged for grow, content kept).
        bool WriteTileContent(int ringIdx, Tile &tile, std::vector<Vertex> &verts,
                              std::vector<uint32_t> &indices, const vec3 &bbMin, const vec3 &bbMax);
        void WriteEmptyTile(Tile &tile);
        void StreamWindows();       // desired world tile per slot; mark moved slots dirty
        void ProcessDirtyTiles();   // budgeted re-mesh + staged in-place GPU upload
        void UpdateColliderRing();  // budgeted per-tile Jolt body add/remove/re-cook
        void GrowOverflowedTiles(); // any overflow doubles its whole ring's budgets (one flush, rare)
        void RemoveTileBody(Tile &tile);
        void RetireSubmittedCommands(bool all);
        bool TileInteriorHole(int ringIdx, int tx, int tz) const;
        float TileWorldSize(const Ring &ring) const;
        void MarkSculptDirty(const SculptOp &op);

        Scene *m_scene = nullptr;
        TerrainConfig m_cfg{};
        std::shared_ptr<voxel::ITerrainGenerator> m_generator;
        bool m_generatorOverridden = false;

        std::vector<Ring> m_rings;
        std::vector<Tile> m_tiles; // all rings, ring-major (Ring::firstTile indexes in here)
        std::vector<SculptOp> m_ops;
        std::vector<QueuedSculpt> m_pendingSculpts;
        vec3 m_anchor = vec3(0.0f);
        bool m_anchorSet = false;

        std::unique_ptr<Material> m_material;
        NodeId *m_hostNode = nullptr;
        std::vector<CommandBuffer *> m_submittedCmds;
    };
} // namespace pe::terrain
