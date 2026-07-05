#pragma once

#include "Base/Math.h"

namespace pe
{
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
    // Heightfield terrain: a 2D surface-height grid meshed into a standard scene mesh, with an optional
    // static Jolt collider. Carved out of the voxel system (which is now cube-only) — terrain is a
    // surface, not a volume, so there is no density field, no caves/overhangs, and no memory ceiling.
    // Worldgen reuses the shared ITerrainGenerator seam (voxel::NoiseGen / voxel::MapGen); only the
    // continuous SurfaceHeight(x,z) is sampled. Brushes (Sculpt) raise/lower the height and re-mesh.
    struct TerrainConfig
    {
        // Extent in metres: a sizeXMeters * sizeZMeters patch centred on the bounds column. One grid
        // cell spans metersPerPixel metres, so the mesh has (size/metersPerPixel) cells per axis (capped)
        // — a coarse map can cover a large world cheaply. 0 on an axis with a heightmap fills its extent.
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
        float metersPerPixel = 1.0f; // world metres each heightmap pixel / grid cell spans in X/Z
        // Static Jolt triangle-mesh collider so ordinary rigidbodies collide with the terrain. Toggling
        // does NOT rebuild the mesh; friction/restitution are live-applied to the body (no re-cook).
        bool physics = false;
        float physicsFriction = 0.5f;
        float physicsRestitution = 0.3f;
    };

    class TerrainWorld
    {
    public:
        TerrainWorld();
        ~TerrainWorld();

        void Create(Scene *scene, const TerrainConfig &cfg);
        void Destroy();
        void Update(); // drains queued sculpts at the safe system-tick point

        // Height brush: raise (amount > 0) or lower (amount < 0) the surface within radius, then re-mesh.
        // Falloff is linear to the rim. No-op outside the grid.
        void Sculpt(const vec3 &center, float radius, float amount);
        // Defer a sculpt to the next Update() — safe to call from the editor/script render path where an
        // immediate re-mesh + GPU flush would run mid-frame.
        void QueueSculpt(const vec3 &center, float radius, float amount);

        // Surface world-Y at world (x, z); bilinear, edge-clamped. Very low outside the grid.
        float SampleHeight(float x, float z) const;
        // Aim a ray (usually downward) at the surface; fills the world hit point + up-facing normal.
        // False if the ray never crosses the surface within maxDist.
        bool Raycast(const vec3 &o, const vec3 &d, float maxDist, vec3 &hitPoint, vec3 &hitNormal) const;
        // Collision solidity of block-cell (x,y,z): solid when the cell centre is below the surface.
        // The predicate voxel::MoveAabb uses so non-physics movers stop on the terrain.
        bool IsSolidCell(int x, int y, int z) const;

        void SetPhysicsEnabled(bool enabled);
        void SetTerrainMaterial(float friction, float restitution);
        // Override the generator (call before Create; nullptr reverts to the default). Runs on the main
        // thread only here — the build is synchronous — but keep it thread-safe for parity with voxel.
        void SetTerrainGenerator(std::shared_ptr<voxel::ITerrainGenerator> generator);

        const TerrainConfig &Config() const { return m_cfg; }
        // False once a Scene buffer rebuild (scene load, play-stop restore) wiped the host mesh out of
        // the shared buffer — reconcile recreates instead of leaving an empty terrain.
        bool IsAlive() const;

    private:
        void BuildField();     // sample the generator's SurfaceHeight into m_height
        void RebuildMesh();    // grid-mesh m_height into tiles on the host node, replacing the old set
        void UpdateCollider(); // (re)build or drop the static Jolt collider to match cfg.physics + tiles
        float FieldAt(int i, int j) const;

        Scene *m_scene = nullptr;
        TerrainConfig m_cfg{};
        std::shared_ptr<voxel::ITerrainGenerator> m_generator;
        bool m_generatorOverridden = false;

        // Surface heights: (m_nx+1) * (m_nz+1) samples, one per block corner, at m_origin + (i, 0, j).
        std::vector<float> m_height;
        vec3 m_origin = vec3(0.0f); // world position of grid corner (0,0); origin.y unused
        int m_nx = 0;               // grid cells in X (samples = m_nx + 1)
        int m_nz = 0;
        float m_step = 1.0f; // world metres between grid cells (= cfg.metersPerPixel, clamped)

        struct PendingSculpt
        {
            vec3 center;
            float radius;
            float amount;
        };
        std::vector<PendingSculpt> m_pendingSculpts;

        std::unique_ptr<Material> m_material;
        NodeId *m_hostNode = nullptr;
        std::vector<int> m_meshIndices;
        bool m_colliderActive = false;
    };
} // namespace pe::terrain
