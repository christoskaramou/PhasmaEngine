#pragma once

#include "ECS/System.h"
#include "Navigation/NavCrowd.h"
#include "Navigation/NavMesh.h"
#include "Navigation/NavSwarm.h"
#include "Scene/SceneNodeHandle.h"

namespace pe
{
    class Scene;

    // The active scene's navigation: its mesh, baked from the static, non-trigger physics colliders (Box, Sphere and
    // Capsule tessellated, Mesh and ConvexHull from the node's LOD0 triangles), and, during play, the nodes moving on
    // it: path-following agents (NavCrowd, each to its own target) and one swarm (NavSwarm, all chasing one target).
    // Movers step every frame on the time-scaled delta (gameplay slow motion slows them, like animation and scripts;
    // Jolt ignores time_scale) and write their nodes' positions; a deleted node drops out.
    // When both kinds coexist, the crowd takes the swarm's fixed steps and shares its ORCA avoidance.
    // ponytail: baked only on request (the editor's Bake, Lua nav.bake), held in memory, dropped by a scene load
    // (generation check); a bake or the end of play drops every mover. Terrain tiles and play-only trigger-zone
    // colliders are not gathered; movers do not turn their nodes (scripts read nav.get_velocity).
    class NavigationSystem : public ISystem
    {
    public:
        void Init(CommandBuffer *) override {}
        void Update() override;
        void Destroy() override;

        bool Bake(const NavMeshSettings &settings, std::string &error);
        // The baked mesh while it belongs to the active scene, else null.
        [[nodiscard]] const NavMesh *Mesh() const;
        // The baked mesh's polygon edges as point pairs (empty when Mesh() is null), cached at bake for the gizmo.
        [[nodiscard]] const std::vector<vec3> &DebugSegments() const;
        // The active scene's nav_* settings.
        static NavMeshSettings SceneSettingsForBake();
        // What Bake feeds Recast: the scene's static collider triangles in world space.
        static void CollectGeometry(Scene &scene, std::vector<float> &vertices, std::vector<int> &triangles);

        // Movers exist while IsScriptPlayMode(); RuntimePlaySession's StopPlay drops them at stop.
        void StopPlay();
        void SetPaused(bool paused) { m_paused = paused; }

        // Movers, by scene node. Each returns false (with a warning) outside play, without a mesh, or off the mesh.
        bool AddAgent(const SceneNodeHandle &node, const NavAgentParams &params);
        bool SetAgentTarget(const SceneNodeHandle &node, const vec3 &target);
        void StopAgent(const SceneNodeHandle &node);
        bool AddSwarmMember(const SceneNodeHandle &node, const NavSwarmMember &member);
        void SetSwarmTarget(const vec3 &target);
        void Remove(const SceneNodeHandle &node); // an agent or a swarm member
        bool GetVelocity(const SceneNodeHandle &node, vec3 &velocity) const;
        [[nodiscard]] bool IsAnchored(const SceneNodeHandle &node) const;
        [[nodiscard]] NavSwarmStats SwarmStats() const;

    private:
        struct Binding
        {
            SceneNodeHandle node;
            int index = -1;
        };

        bool ReadyForMovers(const char *what);
        void DropMovers();
        void Place(Scene &scene, const SceneNodeHandle &node, const vec3 &world);
        static const Binding *Find(const std::vector<Binding> &bindings, const SceneNodeHandle &node);

        NavMesh m_mesh;
        std::vector<vec3> m_debugSegments;
        const Scene *m_scene = nullptr;
        uint32_t m_generation = 0;
        bool m_paused = false;
        std::unique_ptr<NavCrowd> m_crowd;
        std::unique_ptr<NavSwarm> m_swarm;
        std::vector<Binding> m_agents, m_members;
    };
} // namespace pe
