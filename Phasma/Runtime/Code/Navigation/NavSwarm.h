#pragma once

#include "Navigation/NavMesh.h"

namespace pe
{
    class NavCrowd;
    struct NavSwarmMember
    {
        float radius = 0.3f;
        float speed = 3.5f;        // metres per second
        float stopDistance = 0.0f; // the member holds still this close to the target (centre to target point)
    };

    struct NavSwarmStats
    {
        int members = 0;
        int steps = 0;       // fixed steps run by the last Update
        float stepMs = 0.0f; // their mean cost
    };

    // A horde chasing one target over the navmesh. Each fixed step: a flow field over the mesh's polygons toward the
    // target (Dijkstra, recomputed when the target changes polygon) gives every member the next polygon edge to aim
    // for; reciprocal velocity obstacles (ORCA) between neighbours pick each member's velocity, so bodies flow around
    // each other instead of pushing; members move along the mesh surface and never leave it. Members within their
    // stop distance hold still and the rest route around them. Deterministic for one sequence of calls: fixed
    // 60 Hz steps, members in index order, last-step velocities read (Jacobi).
    // ponytail: one solo tile (what NavMesh bakes), 2D avoidance per floor (members over 2 m apart in height ignore
    // each other); tiled meshes and per-member heights when a consumer needs them.
    class NavSwarm
    {
    public:
        static constexpr float kStep = 1.0f / 60.0f;

        // Points into the mesh's Detour data: destroy the swarm before rebuilding or destroying the mesh.
        explicit NavSwarm(const NavMesh &mesh);
        ~NavSwarm();
        NavSwarm(const NavSwarm &) = delete;
        NavSwarm &operator=(const NavSwarm &) = delete;

        // The member's index, or -1 when the position is off the mesh.
        int Add(const vec3 &position, const NavSwarmMember &member);
        void Remove(int member);
        void SetTarget(const vec3 &target);
        // Runs the whole fixed steps that fit in `dt` (the rest carries over, at most 4 a call) and returns how many.
        // A supplied crowd steps with the swarm and shares its avoidance pass.
        int Update(float dt, NavCrowd *crowd = nullptr);
        bool Get(int member, vec3 &position, vec3 &velocity) const;
        // Within its stop distance of the target.
        [[nodiscard]] bool IsAnchored(int member) const;
        [[nodiscard]] const NavSwarmStats &Stats() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace pe
