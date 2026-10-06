#pragma once

namespace pe
{
    // Bake inputs for one agent size, in metres, Y up (Recast's convention, and the engine's).
    struct NavMeshSettings
    {
        float cellSize = 0.2f;       // horizontal voxel size
        float cellHeight = 0.1f;     // vertical voxel size
        float agentRadius = 0.4f;    // walls and props are kept this far from the mesh edge
        float agentHeight = 1.8f;    // ceilings lower than this block
        float agentMaxClimb = 0.4f;  // steps up to this height are walkable
        float agentMaxSlope = 45.0f; // degrees
    };

    struct NavMeshStats
    {
        int polygons = 0;
        int vertices = 0;
        float bakeMs = 0.0f;
    };

    // A Recast/Detour navigation mesh behind a plain interface: no Recast or Detour type leaves NavMesh.cpp.
    // ponytail: one solo tile and one agent size, baked in memory; tiles (large worlds, dynamic obstacles), several
    // agent sizes and a saved bake come when a consumer needs them.
    class NavMesh
    {
    public:
        NavMesh();
        ~NavMesh();
        NavMesh(const NavMesh &) = delete;
        NavMesh &operator=(const NavMesh &) = delete;
        NavMesh(NavMesh &&) noexcept;
        NavMesh &operator=(NavMesh &&) noexcept;

        // World-space triangles (xyz per vertex, three indices per triangle). A triangle is walkable when its normal,
        // cross(v1 - v0, v2 - v0), points up within agentMaxSlope. Replaces any previous mesh; on failure the mesh is
        // empty and `error` says why.
        bool Build(const std::vector<float> &vertices, const std::vector<int> &triangles, const NavMeshSettings &settings,
                   std::string &error);
        void Clear();
        [[nodiscard]] bool IsReady() const;
        [[nodiscard]] const NavMeshStats &Stats() const { return m_stats; }

        // The corners of the shortest walk from start to end, both snapped onto the mesh. `complete` is false when
        // the end cannot be reached: the path then stops at the reachable point nearest to it. False when either point
        // is off the mesh.
        bool FindPath(const vec3 &start, const vec3 &end, std::vector<vec3> &path, bool &complete) const;
        // The mesh point nearest to `point` (within 2 m horizontally, 4 m vertically).
        bool NearestPoint(const vec3 &point, vec3 &out) const;
        // Walks the mesh from start toward end. False when start is off the mesh or end is invalid; otherwise true, with
        // `blocked` and `hit` (on the mesh) where an edge stops the walk.
        bool Raycast(const vec3 &start, const vec3 &end, bool &blocked, vec3 &hit) const;
        // Every polygon edge as a pair of points, for debug drawing.
        void DebugEdges(std::vector<vec3> &segments) const;

    private:
        friend class NavCrowd; // both read the Detour state (NavDetail.h)
        friend class NavSwarm;
        struct Impl;
        std::unique_ptr<Impl> m_impl;
        NavMeshStats m_stats;
    };
} // namespace pe
